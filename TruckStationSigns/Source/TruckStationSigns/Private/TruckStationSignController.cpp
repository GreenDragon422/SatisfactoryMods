#include "TruckStationSignController.h"

#include "TruckStationSignPolicy.h"
#include "TruckStationSignStationAccess.h"

#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "FGSaveInterface.h"
#include "HAL/PlatformTime.h"
#include "TruckStationSignsLog.h"
#include "UObject/TextProperty.h"
#include "UObject/UnrealType.h"
#include "WheeledVehicles/FGDockingStationIdentifier.h"

namespace TruckStationSignStationAccess
{
	static FProperty* FindProperty(UClass* owner, const TCHAR* name)
	{
		FProperty* property = FindFProperty<FProperty>(owner, FName(name));
		UE_LOG(LogTruckStationSigns, VeryVerbose,
			TEXT("TSS_LAYOUT_DIAGNOSTIC owner=%s property=%s type=%s runtime_offset=%d compiled_offset=unavailable"),
			*owner->GetName(), name, property ? *property->GetClass()->GetName() : TEXT("missing"),
			property ? property->GetOffset_ForInternal() : -1);
		return property;
	}

	static FObjectProperty* FindObjectProperty(UClass* owner, const TCHAR* name, UClass* expectedClass)
	{
		FObjectProperty* property = CastField<FObjectProperty>(FindProperty(owner, name));
		if (!property || property->PropertyClass != expectedClass || property->ArrayDim != 1)
		{
			UE_LOG(LogTruckStationSigns, Warning, TEXT("TSS cannot read %s.%s: unexpected reflected property type or class."), *owner->GetName(), name);
			return nullptr;
		}
		return property;
	}

	AFGDockingStationIdentifier* GetIdentifier(const AFGBuildableDockingStation* station)
	{
		static FObjectProperty* property = FindObjectProperty(AFGBuildableDockingStation::StaticClass(), TEXT("mStationIdentifier"), AFGDockingStationIdentifier::StaticClass());
		if (!IsValid(station) || !property) return nullptr;
		UObject* value = property->GetObjectPropertyValue_InContainer(station);
		return IsValid(value) ? Cast<AFGDockingStationIdentifier>(value) : nullptr;
	}

	AFGBuildableDockingStation* GetStation(const AFGDockingStationIdentifier* identifier)
	{
		static FObjectProperty* property = FindObjectProperty(AFGDockingStationIdentifier::StaticClass(), TEXT("mStation"), AFGBuildableDockingStation::StaticClass());
		if (!IsValid(identifier) || !property) return nullptr;
		UObject* value = property->GetObjectPropertyValue_InContainer(identifier);
		return IsValid(value) ? Cast<AFGBuildableDockingStation>(value) : nullptr;
	}

	bool GetName(const AFGDockingStationIdentifier* identifier, FText& name)
	{
		static FTextProperty* property = []()
		{
			FTextProperty* result = CastField<FTextProperty>(FindProperty(AFGDockingStationIdentifier::StaticClass(), TEXT("mStationName")));
			if (!result || result->ArrayDim != 1)
			{
				UE_LOG(LogTruckStationSigns, Warning, TEXT("TSS cannot read mStationName: unexpected reflected property type."));
				return static_cast<FTextProperty*>(nullptr);
			}
			return result;
		}();
		if (!IsValid(identifier) || !property) return false;
		name = property->GetPropertyValue_InContainer(identifier);
		return true;
	}
}

namespace TruckStationSignControllerConstants
{
	const TCHAR* StandardStationClassPath =
		TEXT("/Game/FactoryGame/Buildable/Factory/TruckStation/Build_TruckStation.Build_TruckStation_C");
	const TCHAR* FluidStationClassPath =
		TEXT("/Game/FactoryGame/Buildable/Factory/TruckStation/Build_FluidTruckStation.Build_FluidTruckStation_C");
	const TCHAR* SignClassPath =
		TEXT("/Game/FactoryGame/Buildable/Factory/SignDigital/Build_StandaloneWidgetSign_SmallVeryWide.Build_StandaloneWidgetSign_SmallVeryWide_C");
	const TCHAR* LayoutClassPath =
		TEXT("/Game/FactoryGame/Interface/UI/InGame/Signs/SignLayouts/BPW_Sign4x1_1.BPW_Sign4x1_1_C");
	const TCHAR* DescriptorClassPath =
		TEXT("/Game/FactoryGame/Buildable/Factory/-Shared/SignTypes/SignTypeDesc_8x1.SignTypeDesc_8x1_C");
	constexpr double InitializationFrameBudgetSeconds = 0.001;
}

struct FTruckStationSignInitializationState final
{
	TArray<TWeakObjectPtr<AFGBuildableDockingStation>> pendingStations;
	FTSTicker::FDelegateHandle tickerHandle;
	int32 nextStationIndex = 0;
	int32 activeSignCount = 0;
	double startedAtSeconds = FPlatformTime::Seconds();
};

FTruckStationSignController::~FTruckStationSignController()
{
	StopStationInitialization();
}

bool FTruckStationSignController::Initialize()
{
	standardTruckStationClass.Reset(LoadClass<AFGBuildableDockingStation>(
		nullptr,
		TruckStationSignControllerConstants::StandardStationClassPath));
	fluidTruckStationClass.Reset(LoadClass<AFGBuildableDockingStation>(
		nullptr,
		TruckStationSignControllerConstants::FluidStationClassPath));
	signClass.Reset(LoadClass<AFGBuildableWidgetSign>(
		nullptr,
		TruckStationSignControllerConstants::SignClassPath));
	prefabLayoutClass.Reset(LoadClass<UFGSignPrefabWidget>(
		nullptr,
		TruckStationSignControllerConstants::LayoutClassPath));
	signTypeDescriptorClass.Reset(LoadClass<UFGSignTypeDescriptor>(
		nullptr,
		TruckStationSignControllerConstants::DescriptorClassPath));

	automaticSignGenerationEnabled = standardTruckStationClass.IsValid() &&
		fluidTruckStationClass.IsValid() &&
		signClass.IsValid() &&
		prefabLayoutClass.IsValid() &&
		signTypeDescriptorClass.IsValid();
	return automaticSignGenerationEnabled;
}

void FTruckStationSignController::Reset()
{
	StopStationInitialization();
	ownershipRegistry.Reset();
	appliedNames.Reset();
	automaticSignGenerationEnabled = false;
	standardTruckStationClass.Reset();
	fluidTruckStationClass.Reset();
	signClass.Reset();
	prefabLayoutClass.Reset();
	signTypeDescriptorClass.Reset();
}

void FTruckStationSignController::CancelPendingStationInitialization()
{
	StopStationInitialization();
}

void FTruckStationSignController::EnqueueStationInitialization(AFGBuildableDockingStation* station)
{
	if (!IsValid(station))
	{
		return;
	}

	if (!initializationState.IsValid())
	{
		initializationState = MakeUnique<FTruckStationSignInitializationState>();
		initializationState->tickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FTruckStationSignController::TickStationInitialization));
		UE_LOG(
			LogTruckStationSigns,
			Verbose,
			TEXT("Started event-driven Truck Station sign initialization with a %.3f ms per-frame budget."),
			TruckStationSignControllerConstants::InitializationFrameBudgetSeconds * 1000.0);
	}

	initializationState->pendingStations.Add(station);
}

bool FTruckStationSignController::TickStationInitialization(float)
{
	if (!initializationState.IsValid())
	{
		return false;
	}

	const double frameStartSeconds = FPlatformTime::Seconds();
	while (initializationState->nextStationIndex < initializationState->pendingStations.Num())
	{
		AFGBuildableDockingStation* station =
			initializationState->pendingStations[initializationState->nextStationIndex].Get();
		++initializationState->nextStationIndex;
		if (EnsureSign(station) != nullptr)
		{
			++initializationState->activeSignCount;
		}
		if (FPlatformTime::Seconds() - frameStartSeconds >=
			TruckStationSignControllerConstants::InitializationFrameBudgetSeconds)
		{
			break;
		}
	}

	if (initializationState->nextStationIndex < initializationState->pendingStations.Num())
	{
		return true;
	}

	const int32 activeSignCount = initializationState->activeSignCount;
	const double elapsedMilliseconds =
		(FPlatformTime::Seconds() - initializationState->startedAtSeconds) * 1000.0;
	initializationState->tickerHandle.Reset();
	initializationState.Reset();
	UE_LOG(
		LogTruckStationSigns,
		Verbose,
		TEXT("Completed event-driven initialization of %d transient Truck Station signs in %.3f ms."),
		activeSignCount,
		elapsedMilliseconds);
	return false;
}

void FTruckStationSignController::StopStationInitialization()
{
	if (!initializationState.IsValid())
	{
		return;
	}

	if (initializationState->tickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(initializationState->tickerHandle);
		initializationState->tickerHandle.Reset();
	}
	initializationState.Reset();
}

void FTruckStationSignController::OnStationEndPlay(
	AFGBuildableDockingStation* station,
	EEndPlayReason::Type reason)
{
	if (station == nullptr)
	{
		return;
	}

	AFGBuildableWidgetSign* trackedSign = Cast<AFGBuildableWidgetSign>(ownershipRegistry.Remove(station));
	if (FTruckStationSignPolicy::ShouldDeleteGeneratedSign(reason))
	{
		DestroyGeneratedSign(trackedSign);

		TArray<AActor*> attachedActors;
		station->GetAttachedActors(attachedActors, true, false);
		for (AActor* attachedActor : attachedActors)
		{
			AFGBuildableWidgetSign* attachedSign = Cast<AFGBuildableWidgetSign>(attachedActor);
			if (attachedSign != trackedSign && FTruckStationSignPolicy::IsCurrentGeneratedSign(attachedSign))
			{
				DestroyGeneratedSign(attachedSign);
			}
		}
	}
}

void FTruckStationSignController::OnStationNameChanged(AFGDockingStationIdentifier* identifier)
{
	if (!IsValid(identifier))
	{
		return;
	}

	AFGBuildableDockingStation* station = TruckStationSignStationAccess::GetStation(identifier);
	AFGBuildableWidgetSign* sign = EnsureSign(station);
	FText stationName;
	if (sign != nullptr && TruckStationSignStationAccess::GetName(identifier, stationName))
	{
		ApplyNameToSign(sign, stationName);
	}
}

AFGBuildableWidgetSign* FTruckStationSignController::EnsureSign(AFGBuildableDockingStation* station)
{
	if (!automaticSignGenerationEnabled ||
		!IsValid(station) ||
		!station->HasAuthority() ||
		!IsSupportedStation(station))
	{
		return nullptr;
	}

	AFGBuildableWidgetSign* sign = FindTrackedOrAttachedSign(station);
	const FTransform signRelativeTransform = FTruckStationSignPolicy::GetSignRelativeTransform(
		station->GetClass(),
		standardTruckStationClass.Get(),
		fluidTruckStationClass.Get());
	if (sign == nullptr)
	{
		sign = SpawnSign(station, signRelativeTransform);
	}

	if (IsValid(sign) && IFGSaveInterface::Execute_ShouldSave(sign))
	{
		UE_LOG(
			LogTruckStationSigns,
			Error,
			TEXT("Generated sign %s still reports ShouldSave=true; destroying it and disabling automatic sign generation to protect the save."),
			*sign->GetPathName());
		automaticSignGenerationEnabled = false;
		DestroyGeneratedSign(sign);
		return nullptr;
	}

	if (sign != nullptr)
	{
		sign->SetActorRelativeTransform(signRelativeTransform);
		FText stationName;
		if (TruckStationSignStationAccess::GetName(TruckStationSignStationAccess::GetIdentifier(station), stationName))
		{
			ApplyNameToSign(sign, stationName);
		}
	}

	return sign;
}
void FTruckStationSignController::DisableInteraction(AFGBuildableWidgetSign* sign) const
{
	if (!IsValid(sign))
	{
		return;
	}

	AFGBuildableDockingStation* station = Cast<AFGBuildableDockingStation>(sign->GetOwner());
	if (!IsSupportedStation(station))
	{
		return;
	}

	sign->SetActorEnableCollision(false);
	sign->SetCanBeDamaged(false);
}

bool FTruckStationSignController::IsSupportedStation(const AFGBuildableDockingStation* station) const
{
	return IsValid(station) && FTruckStationSignPolicy::IsSupportedStationClass(
		station->GetClass(),
		standardTruckStationClass.Get(),
		fluidTruckStationClass.Get());
}

AFGBuildableWidgetSign* FTruckStationSignController::RecreateSignAtRelativeTransformForVisualTest(
	AFGBuildableDockingStation* station,
	const FTransform& relativeTransform)
{
	if (!automaticSignGenerationEnabled ||
		!IsValid(station) ||
		!station->HasAuthority() ||
		!IsSupportedStation(station))
	{
		return nullptr;
	}

	FText stationName;
	if (!TruckStationSignStationAccess::GetName(TruckStationSignStationAccess::GetIdentifier(station), stationName))
	{
		return nullptr;
	}

	AFGBuildableWidgetSign* existingSign = FindTrackedOrAttachedSign(station);
	ownershipRegistry.Remove(station);
	DestroyGeneratedSign(existingSign);

	AFGBuildableWidgetSign* recreatedSign = SpawnSign(station, relativeTransform);
	if (!IsValid(recreatedSign))
	{
		return nullptr;
	}

	ApplyNameToSign(recreatedSign, stationName);
	return recreatedSign;
}

AFGBuildableWidgetSign* FTruckStationSignController::FindTrackedOrAttachedSign(
	AFGBuildableDockingStation* station)
{
	AFGBuildableWidgetSign* trackedSign = Cast<AFGBuildableWidgetSign>(ownershipRegistry.Find(station));
	if (IsValid(trackedSign))
	{
		return trackedSign;
	}

	TArray<AActor*> attachedActors;
	station->GetAttachedActors(attachedActors, true, false);
	for (AActor* attachedActor : attachedActors)
	{
		AFGBuildableWidgetSign* attachedSign = Cast<AFGBuildableWidgetSign>(attachedActor);
		if (IsValid(attachedSign) &&
			FTruckStationSignPolicy::IsCurrentGeneratedSign(attachedSign))
		{
			ownershipRegistry.TryAdd(station, attachedSign);
			return attachedSign;
		}
	}

	return nullptr;
}

AFGBuildableWidgetSign* FTruckStationSignController::SpawnSign(
	AFGBuildableDockingStation* station,
	const FTransform& relativeTransform)
{
	UWorld* world = station->GetWorld();
	if (world == nullptr || world->bIsTearingDown || !signClass.IsValid())
	{
		return nullptr;
	}

	const FTransform worldTransform = relativeTransform * station->GetActorTransform();
	FActorSpawnParameters spawnParameters;
	spawnParameters.Owner = station;
	spawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	spawnParameters.ObjectFlags |= RF_Transient;
	spawnParameters.bDeferConstruction = true;

	AFGBuildableWidgetSign* sign = world->SpawnActor<AFGBuildableWidgetSign>(
		signClass.Get(),
		worldTransform,
		spawnParameters);
	if (!IsValid(sign))
	{
		return nullptr;
	}

	sign->Tags.AddUnique(FTruckStationSignPolicy::GetCurrentGeneratedSignTag());
	sign->AttachToActor(station, FAttachmentTransformRules::KeepWorldTransform);
	sign->SetReplicates(true);
	sign->SetReplicateMovement(true);

	if (!ownershipRegistry.TryAdd(station, sign))
	{
		sign->Destroy();
		return Cast<AFGBuildableWidgetSign>(ownershipRegistry.Find(station));
	}

	sign->FinishSpawning(worldTransform);
	if (!IsValid(sign))
	{
		ownershipRegistry.Remove(station);
		return nullptr;
	}
	DisableInteraction(sign);

	return sign;
}

void FTruckStationSignController::DestroyGeneratedSign(AFGBuildableWidgetSign* sign)
{
	if (!IsValid(sign) || !sign->HasAuthority())
	{
		return;
	}

	appliedNames.Remove(sign);
	sign->Destroy();
}

void FTruckStationSignController::ApplyNameToSign(
	AFGBuildableWidgetSign* sign,
	const FText& stationName)
{
	if (!IsValid(sign) || !sign->HasAuthority())
	{
		return;
	}

	const FString requestedName = stationName.ToString();
	const FString* appliedName = appliedNames.Find(sign);
	if (appliedName != nullptr && *appliedName == requestedName)
	{
		return;
	}

	FPrefabSignData signData = FTruckStationSignPolicy::CreateSignData(
		stationName,
		TSubclassOf<UFGSignPrefabWidget>(prefabLayoutClass.Get()),
		TSubclassOf<UFGSignTypeDescriptor>(signTypeDescriptorClass.Get()));
	sign->SetPrefabSignData(signData);
	appliedNames.Add(sign, requestedName);
}
