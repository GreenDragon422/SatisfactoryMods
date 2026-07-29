#include "TruckStationSignController.h"

#include "TruckStationSignPolicy.h"

#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGSaveInterface.h"
#include "TruckStationSignsLog.h"
#include "WheeledVehicles/FGDockingStationIdentifier.h"

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
	ownershipRegistry.Reset();
	appliedNames.Reset();
	automaticSignGenerationEnabled = false;
	standardTruckStationClass.Reset();
	fluidTruckStationClass.Reset();
	signClass.Reset();
	prefabLayoutClass.Reset();
	signTypeDescriptorClass.Reset();
}

int32 FTruckStationSignController::RefreshWorld(UWorld* world)
{
	if (world == nullptr)
	{
		return 0;
	}

	RemoveLegacyGeneratedSigns(world);

	ownershipRegistry.RemoveInvalidEntries();
	TMap<TWeakObjectPtr<AFGBuildableWidgetSign>, FString>::TIterator nameIterator = appliedNames.CreateIterator();
	while (nameIterator)
	{
		if (!nameIterator.Key().IsValid())
		{
			nameIterator.RemoveCurrent();
		}
		++nameIterator;
	}

	int32 activeSignCount = 0;
	for (TActorIterator<AFGBuildableDockingStation> stationIterator(world); stationIterator; ++stationIterator)
	{
		if (EnsureSign(*stationIterator) != nullptr)
		{
			++activeSignCount;
		}
	}

	TArray<AFGBuildableWidgetSign*> orphanedSigns;
	for (TActorIterator<AFGBuildableWidgetSign> signIterator(world); signIterator; ++signIterator)
	{
		AFGBuildableWidgetSign* sign = *signIterator;
		if (!FTruckStationSignPolicy::IsCurrentGeneratedSign(sign))
		{
			continue;
		}

		AFGBuildableDockingStation* station = Cast<AFGBuildableDockingStation>(sign->GetOwner());
		if (!IsSupportedStation(station))
		{
			orphanedSigns.Add(sign);
		}
	}

	for (AFGBuildableWidgetSign* sign : orphanedSigns)
	{
		if (IsValid(sign) && sign->HasAuthority())
		{
			appliedNames.Remove(sign);
			sign->Destroy();
		}
	}

	return activeSignCount;
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
			if (attachedSign != trackedSign && FTruckStationSignPolicy::IsGeneratedSign(attachedSign))
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

	AFGBuildableDockingStation* station = identifier->GetStation();
	AFGBuildableWidgetSign* sign = EnsureSign(station);
	if (sign != nullptr)
	{
		ApplyNameToSign(sign, identifier->GetStationName());
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
	if (sign == nullptr)
	{
		sign = SpawnSign(station);
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
		sign->SetActorRelativeTransform(FTruckStationSignPolicy::GetFrontSignRelativeTransform());
		AFGDockingStationIdentifier* identifier = station->GetStationIdentifier();
		const FText stationName = identifier != nullptr ? identifier->GetStationName() : FText::GetEmpty();
		ApplyNameToSign(sign, stationName);
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

AFGBuildableWidgetSign* FTruckStationSignController::SpawnSign(AFGBuildableDockingStation* station)
{
	UWorld* world = station->GetWorld();
	if (world == nullptr || !signClass.IsValid())
	{
		return nullptr;
	}

	const FTransform relativeTransform = FTruckStationSignPolicy::GetFrontSignRelativeTransform();
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

void FTruckStationSignController::RemoveLegacyGeneratedSigns(UWorld* world)
{
	TArray<AFGBuildableWidgetSign*> legacySigns;
	for (TActorIterator<AFGBuildableWidgetSign> signIterator(world); signIterator; ++signIterator)
	{
		AFGBuildableWidgetSign* sign = *signIterator;
		if (FTruckStationSignPolicy::IsGeneratedSign(sign) &&
			!FTruckStationSignPolicy::IsCurrentGeneratedSign(sign))
		{
			legacySigns.Add(sign);
		}
	}

	for (AFGBuildableWidgetSign* legacySign : legacySigns)
	{
		DestroyGeneratedSign(legacySign);
	}
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
	sign->SetPrefabSignData(signData, false);
	appliedNames.Add(sign, requestedName);
}
