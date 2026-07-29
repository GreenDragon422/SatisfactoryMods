#include "TruckStationSignConsoleCommands.h"

#include "TruckStationSignController.h"
#include "TruckStationSignPolicy.h"

#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "Components/SceneComponent.h"
#include "EngineUtils.h"
#include "FGSaveInterface.h"
#include "FGSaveSession.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "WheeledVehicles/FGDockingStationIdentifier.h"

FTruckStationSignConsoleCommands::FTruckStationSignConsoleCommands(FTruckStationSignController* Controller)
	: Controller(Controller)
{
}

void FTruckStationSignConsoleCommands::Register()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.Help"),
		TEXT("List TruckStationSigns diagnostic commands."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::Help),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.ListStations"),
		TEXT("List every supported truck station with name, location, and player distance."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::ListStations),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.ShowNearestStationSign"),
		TEXT("Show the nearest truck station and its automatic sign."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::ShowNearestStationSign),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.InspectNearby"),
		TEXT("Inspect the player, nearest station, and widget signs within 30 metres of that station."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::InspectNearby),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.ListMatchingNamedSigns"),
		TEXT("List every widget sign containing the nearest station's exact name."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::ListMatchingNamedSigns),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.ShowDefaultSignPosition"),
		TEXT("Show the position and rotation used for automatic truck-station signs."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::ShowDefaultSignPosition),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.MovePlayerRelative"),
		TEXT("Move the player relative to the nearest station: X Y Z Yaw."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::MovePlayerRelative),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.ResetNearestNamedSigns"),
		TEXT("Delete every widget sign containing the nearest station's exact name, then recreate its transient sign."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::ResetNearestNamedSigns),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.ResetAllNamedSigns"),
		TEXT("Delete every widget sign containing any supported station's exact name, then recreate all transient signs."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::ResetAllNamedSigns),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.DeleteSign"),
		TEXT("Delete one widget sign by exact actor name."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::DeleteSign),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.EnsureNearest"),
		TEXT("Ensure the nearest station has its current transient generated sign."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::EnsureNearest),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.SaveCurrentGame"),
		TEXT("Save the current world to the exact file name supplied, without an extension."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::SaveCurrentGame),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.SetNearestTransform"),
		TEXT("Set the nearest generated sign transform: X Y Z Pitch Yaw Roll."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::SetNearestTransform),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.RefreshWorld"),
		TEXT("Run and time the TruckStationSigns world refresh."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::RefreshWorld),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("TruckStationSigns.PurgeNearestGenerated"),
		TEXT("Destroy tagged generated signs attached to the nearest supported station."),
		FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(this, &FTruckStationSignConsoleCommands::PurgeNearestGenerated),
		ECVF_Default));
}

void FTruckStationSignConsoleCommands::Unregister()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject* ConsoleObject : RegisteredCommands)
	{
		if (ConsoleObject != nullptr)
		{
			ConsoleManager.UnregisterConsoleObject(ConsoleObject);
		}
	}
	RegisteredCommands.Empty();
}

void FTruckStationSignConsoleCommands::Help(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	Output.Log(TEXT("TruckStationSigns.Help"));
	Output.Log(TEXT("TruckStationSigns.ListStations"));
	Output.Log(TEXT("TruckStationSigns.ShowNearestStationSign"));
	Output.Log(TEXT("TruckStationSigns.InspectNearby"));
	Output.Log(TEXT("TruckStationSigns.ListMatchingNamedSigns"));
	Output.Log(TEXT("TruckStationSigns.ShowDefaultSignPosition"));
	Output.Log(TEXT("TruckStationSigns.MovePlayerRelative X Y Z Yaw"));
	Output.Log(TEXT("TruckStationSigns.ResetNearestNamedSigns"));
	Output.Log(TEXT("TruckStationSigns.ResetAllNamedSigns"));
	Output.Log(TEXT("TruckStationSigns.DeleteSign ActorName"));
	Output.Log(TEXT("TruckStationSigns.EnsureNearest"));
	Output.Log(TEXT("TruckStationSigns.SaveCurrentGame FileName"));
	Output.Log(TEXT("TruckStationSigns.SetNearestTransform X Y Z Pitch Yaw Roll"));
	Output.Log(TEXT("TruckStationSigns.RefreshWorld"));
	Output.Log(TEXT("TruckStationSigns.PurgeNearestGenerated"));
}

void FTruckStationSignConsoleCommands::ListStations(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	if (World == nullptr || Controller == nullptr)
	{
		Output.Log(TEXT("error: controller or world unavailable"));
		return;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	APawn* Pawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	const FVector ReferenceLocation = Pawn != nullptr ? Pawn->GetActorLocation() : FVector::ZeroVector;
	int32 StationIndex = 0;
	for (TActorIterator<AFGBuildableDockingStation> StationIterator(World); StationIterator; ++StationIterator)
	{
		AFGBuildableDockingStation* Station = *StationIterator;
		if (!Controller->IsSupportedStation(Station))
		{
			continue;
		}

		const FVector Location = Station->GetActorLocation();
		const double DistanceCentimeters = Pawn != nullptr
			? FVector::Distance(ReferenceLocation, Location)
			: -1.0;
		Output.Logf(
			TEXT("station index=%d actor=%s stationName=%s location=(%.3f,%.3f,%.3f) playerDistanceCentimeters=%.3f"),
			StationIndex,
			*Station->GetName(),
			*GetStationName(Station),
			Location.X,
			Location.Y,
			Location.Z,
			DistanceCentimeters);
		++StationIndex;
	}

	Output.Logf(TEXT("supportedStations=%d"), StationIndex);
}

void FTruckStationSignConsoleCommands::ShowNearestStationSign(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	AFGBuildableDockingStation* Station = FindNearestStation(World);
	if (Station == nullptr)
	{
		Output.Log(TEXT("error: no supported truck station found"));
		return;
	}
	WriteStationState(Station, Output);
}

void FTruckStationSignConsoleCommands::InspectNearby(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	AFGBuildableDockingStation* Station = FindNearestStation(World);
	if (Station == nullptr)
	{
		Output.Log(TEXT("error: no supported truck station found"));
		return;
	}

	WritePlayerState(World, Output);
	WriteStationState(Station, Output);

	constexpr double SearchRadiusCentimeters = 3000.0;
	const double SearchRadiusSquared = SearchRadiusCentimeters * SearchRadiusCentimeters;
	int32 NearbySignIndex = 0;
	for (TActorIterator<AFGBuildableWidgetSign> SignIterator(World); SignIterator; ++SignIterator)
	{
		AFGBuildableWidgetSign* Sign = *SignIterator;
		if (!IsValid(Sign) ||
			FVector::DistSquared(Sign->GetActorLocation(), Station->GetActorLocation()) > SearchRadiusSquared)
		{
			continue;
		}

		WriteSignState(Sign, NearbySignIndex, Output, Station);
		++NearbySignIndex;
	}

	Output.Logf(TEXT("nearbyWidgetSigns=%d radiusCentimeters=%.0f"), NearbySignIndex, SearchRadiusCentimeters);
}

void FTruckStationSignConsoleCommands::ListMatchingNamedSigns(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	AFGBuildableDockingStation* Station = FindNearestStation(World);
	const FString StationName = GetStationName(Station);
	if (Station == nullptr || StationName.IsEmpty())
	{
		Output.Log(TEXT("error: nearest station or station name unavailable"));
		return;
	}

	int32 MatchingSignIndex = 0;
	for (TActorIterator<AFGBuildableWidgetSign> SignIterator(World); SignIterator; ++SignIterator)
	{
		AFGBuildableWidgetSign* Sign = *SignIterator;
		if (!SignContainsExactText(Sign, StationName))
		{
			continue;
		}

		WriteSignState(Sign, MatchingSignIndex, Output, Station);
		++MatchingSignIndex;
	}

	Output.Logf(TEXT("matchingNamedSigns=%d stationName=%s"), MatchingSignIndex, *StationName);
}

void FTruckStationSignConsoleCommands::ShowDefaultSignPosition(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	const FTransform Transform = FTruckStationSignPolicy::GetFrontSignRelativeTransform();
	const FVector Location = Transform.GetTranslation();
	const FRotator Rotation = Transform.Rotator();
	Output.Logf(
		TEXT("defaultSign tag=%s position=(%.3f,%.3f,%.3f) rotation=(P=%.3f,Y=%.3f,R=%.3f)"),
		*FTruckStationSignPolicy::GetCurrentGeneratedSignTag().ToString(),
		Location.X,
		Location.Y,
		Location.Z,
		Rotation.Pitch,
		Rotation.Yaw,
		Rotation.Roll);
}

void FTruckStationSignConsoleCommands::MovePlayerRelative(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	if (Arguments.Num() != 4)
	{
		Output.Log(TEXT("error: expected X Y Z Yaw"));
		return;
	}

	float Values[4];
	for (int32 ArgumentIndex = 0; ArgumentIndex < 4; ++ArgumentIndex)
	{
		if (!LexTryParseString(Values[ArgumentIndex], *Arguments[ArgumentIndex]))
		{
			Output.Logf(TEXT("error: argument %d is not numeric"), ArgumentIndex + 1);
			return;
		}
	}

	AFGBuildableDockingStation* Station = FindNearestStation(World);
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	if (Station == nullptr || Pawn == nullptr)
	{
		Output.Log(TEXT("error: nearest station or player pawn unavailable"));
		return;
	}

	const FVector RelativeLocation(Values[0], Values[1], Values[2]);
	const FVector WorldLocation = Station->GetActorTransform().TransformPosition(RelativeLocation);
	const FRotator WorldRotation(0.0f, Station->GetActorRotation().Yaw + Values[3], 0.0f);
	if (!Pawn->TeleportTo(WorldLocation, WorldRotation, false, true))
	{
		Output.Log(TEXT("error: player teleport failed"));
		return;
	}

	PlayerController->SetControlRotation(WorldRotation);
	Output.Logf(
		TEXT("player moved stationRelativeLocation=(%.3f,%.3f,%.3f) stationRelativeYaw=%.3f"),
		RelativeLocation.X,
		RelativeLocation.Y,
		RelativeLocation.Z,
		Values[3]);
	WritePlayerState(World, Output);
}

void FTruckStationSignConsoleCommands::ResetNearestNamedSigns(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	AFGBuildableDockingStation* Station = FindNearestStation(World);
	const FString StationName = GetStationName(Station);
	if (Station == nullptr || StationName.IsEmpty())
	{
		Output.Log(TEXT("error: nearest station or station name unavailable"));
		return;
	}

	TArray<AFGBuildableWidgetSign*> MatchingSigns;
	for (TActorIterator<AFGBuildableWidgetSign> SignIterator(World); SignIterator; ++SignIterator)
	{
		AFGBuildableWidgetSign* Sign = *SignIterator;
		if (!IsValid(Sign))
		{
			continue;
		}

		if (SignContainsExactText(Sign, StationName))
		{
			MatchingSigns.Add(Sign);
		}
	}

	int32 DestroyedCount = 0;
	for (AFGBuildableWidgetSign* MatchingSign : MatchingSigns)
	{
		if (IsValid(MatchingSign) && MatchingSign->HasAuthority() && MatchingSign->Destroy())
		{
			++DestroyedCount;
		}
	}

	AFGBuildableWidgetSign* RecreatedSign = Controller != nullptr ? Controller->EnsureSign(Station) : nullptr;
	Output.Logf(
		TEXT("reset stationName=%s matchingSigns=%d destroyedSigns=%d recreated=%s"),
		*StationName,
		MatchingSigns.Num(),
		DestroyedCount,
		IsValid(RecreatedSign) ? TEXT("true") : TEXT("false"));
	WriteStationState(Station, Output);
}

void FTruckStationSignConsoleCommands::ResetAllNamedSigns(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	if (World == nullptr || Controller == nullptr)
	{
		Output.Log(TEXT("error: controller or world unavailable"));
		return;
	}

	TArray<AFGBuildableDockingStation*> Stations;
	TSet<FString> StationNames;
	for (TActorIterator<AFGBuildableDockingStation> StationIterator(World); StationIterator; ++StationIterator)
	{
		AFGBuildableDockingStation* Station = *StationIterator;
		if (!Controller->IsSupportedStation(Station))
		{
			continue;
		}

		Stations.Add(Station);
		const FString StationName = GetStationName(Station);
		if (!StationName.IsEmpty())
		{
			StationNames.Add(StationName);
		}
	}

	TArray<AFGBuildableWidgetSign*> MatchingSigns;
	for (TActorIterator<AFGBuildableWidgetSign> SignIterator(World); SignIterator; ++SignIterator)
	{
		AFGBuildableWidgetSign* Sign = *SignIterator;
		if (!IsValid(Sign))
		{
			continue;
		}

		FPrefabSignData SignData;
		Sign->GetSignPrefabData(SignData);
		for (const TPair<FString, FString>& TextElement : SignData.TextElementData)
		{
			if (StationNames.Contains(TextElement.Value))
			{
				MatchingSigns.Add(Sign);
				break;
			}
		}
	}

	int32 DestroyedCount = 0;
	for (AFGBuildableWidgetSign* MatchingSign : MatchingSigns)
	{
		if (IsValid(MatchingSign) && MatchingSign->HasAuthority() && MatchingSign->Destroy())
		{
			++DestroyedCount;
		}
	}

	int32 RecreatedCount = 0;
	for (AFGBuildableDockingStation* Station : Stations)
	{
		if (Controller->EnsureSign(Station) != nullptr)
		{
			++RecreatedCount;
		}
	}

	Output.Logf(
		TEXT("resetAll supportedStations=%d uniqueStationNames=%d matchingSigns=%d destroyedSigns=%d recreatedSigns=%d"),
		Stations.Num(),
		StationNames.Num(),
		MatchingSigns.Num(),
		DestroyedCount,
		RecreatedCount);
}

void FTruckStationSignConsoleCommands::DeleteSign(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	if (World == nullptr || Arguments.Num() != 1)
	{
		Output.Log(TEXT("error: expected ActorName"));
		return;
	}

	for (TActorIterator<AFGBuildableWidgetSign> SignIterator(World); SignIterator; ++SignIterator)
	{
		AFGBuildableWidgetSign* Sign = *SignIterator;
		if (IsValid(Sign) && Sign->GetName().Equals(Arguments[0], ESearchCase::CaseSensitive))
		{
			const bool WasDestroyed = Sign->HasAuthority() && Sign->Destroy();
			Output.Logf(
				TEXT("delete actor=%s destroyed=%s"),
				*Arguments[0],
				WasDestroyed ? TEXT("true") : TEXT("false"));
			return;
		}
	}

	Output.Logf(TEXT("error: widget sign actor not found actor=%s"), *Arguments[0]);
}

void FTruckStationSignConsoleCommands::EnsureNearest(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	AFGBuildableDockingStation* Station = FindNearestStation(World);
	AFGBuildableWidgetSign* Sign = Station != nullptr && Controller != nullptr
		? Controller->EnsureSign(Station)
		: nullptr;
	Output.Logf(
		TEXT("ensure station=%s sign=%s result=%s"),
		Station != nullptr ? *Station->GetName() : TEXT("none"),
		Sign != nullptr ? *Sign->GetName() : TEXT("none"),
		IsValid(Sign) ? TEXT("ok") : TEXT("failed"));
	if (Station != nullptr)
	{
		WriteStationState(Station, Output);
	}
}

void FTruckStationSignConsoleCommands::SaveCurrentGame(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	if (World == nullptr || Arguments.Num() != 1 || Arguments[0].IsEmpty())
	{
		Output.Log(TEXT("error: expected FileName without extension"));
		return;
	}

	UFGSaveSession* SaveSession = UFGSaveSession::Get(World);
	if (SaveSession == nullptr)
	{
		Output.Log(TEXT("error: save session unavailable"));
		return;
	}

	SaveSession->SaveGame(Arguments[0], FOnSaveGameComplete(), nullptr);
	Output.Logf(TEXT("save requested fileName=%s"), *Arguments[0]);
}

void FTruckStationSignConsoleCommands::SetNearestTransform(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	if (Arguments.Num() != 6)
	{
		Output.Log(TEXT("error: expected X Y Z Pitch Yaw Roll"));
		return;
	}

	float Values[6];
	for (int32 ArgumentIndex = 0; ArgumentIndex < 6; ++ArgumentIndex)
	{
		if (!LexTryParseString(Values[ArgumentIndex], *Arguments[ArgumentIndex]))
		{
			Output.Logf(TEXT("error: argument %d is not numeric"), ArgumentIndex + 1);
			return;
		}
	}

	AFGBuildableDockingStation* Station = FindNearestStation(World);
	if (Station == nullptr)
	{
		Output.Log(TEXT("error: no supported truck station found"));
		return;
	}

	AFGBuildableWidgetSign* Sign = FindCurrentGeneratedSign(Station);
	if (Sign == nullptr && Controller != nullptr)
	{
		Sign = Controller->EnsureSign(Station);
	}
	if (!IsValid(Sign))
	{
		Output.Log(TEXT("error: generated sign is unavailable"));
		return;
	}

	const FTransform RequestedTransform(
		FRotator(Values[3], Values[4], Values[5]),
		FVector(Values[0], Values[1], Values[2]));
	USceneComponent* RootComponent = Sign->GetRootComponent();
	if (RootComponent == nullptr)
	{
		Output.Log(TEXT("error: generated sign root component unavailable"));
		return;
	}

	TArray<USceneComponent*> DescendantComponents;
	RootComponent->GetChildrenComponents(true, DescendantComponents);
	TArray<EComponentMobility::Type> OriginalDescendantMobilities;
	OriginalDescendantMobilities.Reserve(DescendantComponents.Num());
	for (USceneComponent* DescendantComponent : DescendantComponents)
	{
		OriginalDescendantMobilities.Add(DescendantComponent->Mobility);
	}

	const EComponentMobility::Type OriginalMobility = RootComponent->Mobility;
	RootComponent->SetMobility(EComponentMobility::Movable);
	RootComponent->SetRelativeTransform(RequestedTransform, false, nullptr, ETeleportType::TeleportPhysics);
	RootComponent->UpdateComponentToWorld();
	RootComponent->SetMobility(OriginalMobility);
	for (int32 ComponentIndex = 0; ComponentIndex < DescendantComponents.Num(); ++ComponentIndex)
	{
		USceneComponent* DescendantComponent = DescendantComponents[ComponentIndex];
		const EComponentMobility::Type OriginalDescendantMobility = OriginalDescendantMobilities[ComponentIndex];
		if (DescendantComponent->Mobility != OriginalDescendantMobility)
		{
			DescendantComponent->SetMobility(OriginalDescendantMobility);
		}
	}
	Sign->ForceNetUpdate();

	const FTransform AppliedTransform = RootComponent->GetRelativeTransform();
	const FVector AppliedLocation = AppliedTransform.GetTranslation();
	const FRotator AppliedRotation = AppliedTransform.Rotator();
	const bool WasApplied = AppliedLocation.Equals(RequestedTransform.GetTranslation(), KINDA_SMALL_NUMBER) &&
		AppliedTransform.GetRotation().Equals(RequestedTransform.GetRotation(), KINDA_SMALL_NUMBER);
	Output.Logf(
		TEXT("transform requestedLocation=(%.3f,%.3f,%.3f) requestedRotation=(P=%.3f,Y=%.3f,R=%.3f) appliedLocation=(%.3f,%.3f,%.3f) appliedRotation=(P=%.3f,Y=%.3f,R=%.3f) applied=%s originalMobility=%d"),
		Values[0],
		Values[1],
		Values[2],
		Values[3],
		Values[4],
		Values[5],
		AppliedLocation.X,
		AppliedLocation.Y,
		AppliedLocation.Z,
		AppliedRotation.Pitch,
		AppliedRotation.Yaw,
		AppliedRotation.Roll,
		WasApplied ? TEXT("true") : TEXT("false"),
		static_cast<int32>(OriginalMobility));
	WriteStationState(Station, Output);
}

void FTruckStationSignConsoleCommands::RefreshWorld(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	if (Controller == nullptr || World == nullptr)
	{
		Output.Log(TEXT("error: controller or world unavailable"));
		return;
	}

	const double StartSeconds = FPlatformTime::Seconds();
	const int32 ActiveSignCount = Controller->RefreshWorld(World);
	const double ElapsedMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;
	Output.Logf(
		TEXT("refresh activeSigns=%d elapsedMs=%.3f"),
		ActiveSignCount,
		ElapsedMilliseconds);
}

void FTruckStationSignConsoleCommands::PurgeNearestGenerated(
	const TArray<FString>& Arguments,
	UWorld* World,
	FOutputDevice& Output) const
{
	AFGBuildableDockingStation* Station = FindNearestStation(World);
	if (Station == nullptr)
	{
		Output.Log(TEXT("error: no supported truck station found"));
		return;
	}

	TArray<AActor*> AttachedActors;
	Station->GetAttachedActors(AttachedActors, true, false);
	int32 DestroyedCount = 0;
	for (AActor* AttachedActor : AttachedActors)
	{
		AFGBuildableWidgetSign* Sign = Cast<AFGBuildableWidgetSign>(AttachedActor);
		if (FTruckStationSignPolicy::IsGeneratedSign(Sign) && Sign->HasAuthority())
		{
			Sign->Destroy();
			++DestroyedCount;
		}
	}
	Output.Logf(TEXT("purged generatedSigns=%d"), DestroyedCount);
}

AFGBuildableDockingStation* FTruckStationSignConsoleCommands::FindNearestStation(UWorld* World) const
{
	if (World == nullptr || Controller == nullptr)
	{
		return nullptr;
	}

	FVector ReferenceLocation = FVector::ZeroVector;
	bool HasReferenceLocation = false;
	APlayerController* PlayerController = World->GetFirstPlayerController();
	if (PlayerController != nullptr && PlayerController->GetPawn() != nullptr)
	{
		ReferenceLocation = PlayerController->GetPawn()->GetActorLocation();
		HasReferenceLocation = true;
	}

	AFGBuildableDockingStation* NearestStation = nullptr;
	double NearestDistanceSquared = TNumericLimits<double>::Max();
	for (TActorIterator<AFGBuildableDockingStation> StationIterator(World); StationIterator; ++StationIterator)
	{
		AFGBuildableDockingStation* Candidate = *StationIterator;
		if (!Controller->IsSupportedStation(Candidate))
		{
			continue;
		}
		if (!HasReferenceLocation)
		{
			return Candidate;
		}

		const double DistanceSquared = FVector::DistSquared(ReferenceLocation, Candidate->GetActorLocation());
		if (DistanceSquared < NearestDistanceSquared)
		{
			NearestDistanceSquared = DistanceSquared;
			NearestStation = Candidate;
		}
	}
	return NearestStation;
}

AFGBuildableWidgetSign* FTruckStationSignConsoleCommands::FindCurrentGeneratedSign(
	AFGBuildableDockingStation* Station) const
{
	if (!IsValid(Station))
	{
		return nullptr;
	}
	TArray<AActor*> AttachedActors;
	Station->GetAttachedActors(AttachedActors, true, false);
	for (AActor* AttachedActor : AttachedActors)
	{
		AFGBuildableWidgetSign* Sign = Cast<AFGBuildableWidgetSign>(AttachedActor);
		if (FTruckStationSignPolicy::IsCurrentGeneratedSign(Sign))
		{
			return Sign;
		}
	}
	return nullptr;
}

FString FTruckStationSignConsoleCommands::GetStationName(AFGBuildableDockingStation* Station) const
{
	AFGDockingStationIdentifier* Identifier = Station != nullptr ? Station->GetStationIdentifier() : nullptr;
	return Identifier != nullptr ? Identifier->GetStationName().ToString() : FString();
}

bool FTruckStationSignConsoleCommands::SignContainsExactText(
	AFGBuildableWidgetSign* Sign,
	const FString& Text) const
{
	if (!IsValid(Sign) || Text.IsEmpty())
	{
		return false;
	}

	FPrefabSignData SignData;
	Sign->GetSignPrefabData(SignData);
	for (const TPair<FString, FString>& TextElement : SignData.TextElementData)
	{
		if (TextElement.Value.Equals(Text, ESearchCase::CaseSensitive))
		{
			return true;
		}
	}

	return false;
}

void FTruckStationSignConsoleCommands::WritePlayerState(UWorld* World, FOutputDevice& Output) const
{
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* Pawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	if (Pawn == nullptr)
	{
		Output.Log(TEXT("player pawn=none"));
		return;
	}

	const FVector Location = Pawn->GetActorLocation();
	const FRotator Rotation = Pawn->GetActorRotation();
	const FVector Forward = Pawn->GetActorForwardVector();
	Output.Logf(
		TEXT("player pawn=%s location=(%.3f,%.3f,%.3f) rotation=(P=%.3f,Y=%.3f,R=%.3f) forward=(%.3f,%.3f,%.3f)"),
		*Pawn->GetName(),
		Location.X,
		Location.Y,
		Location.Z,
		Rotation.Pitch,
		Rotation.Yaw,
		Rotation.Roll,
		Forward.X,
		Forward.Y,
		Forward.Z);
}

void FTruckStationSignConsoleCommands::WriteStationState(
	AFGBuildableDockingStation* Station,
	FOutputDevice& Output) const
{
	const FVector Location = Station->GetActorLocation();
	const FRotator Rotation = Station->GetActorRotation();
	const FVector Forward = Station->GetActorForwardVector();
	const FVector Right = Station->GetActorRightVector();
	const FVector Up = Station->GetActorUpVector();
	FVector BoundsOrigin;
	FVector BoundsExtent;
	Station->GetActorBounds(false, BoundsOrigin, BoundsExtent, false);
	Output.Logf(
		TEXT("station name=%s stationName=%s class=%s location=(%.3f,%.3f,%.3f) rotation=(P=%.3f,Y=%.3f,R=%.3f) forward=(%.3f,%.3f,%.3f) right=(%.3f,%.3f,%.3f) up=(%.3f,%.3f,%.3f) boundsOrigin=(%.3f,%.3f,%.3f) boundsExtent=(%.3f,%.3f,%.3f)"),
		*Station->GetName(),
		*GetStationName(Station),
		*Station->GetClass()->GetName(),
		Location.X,
		Location.Y,
		Location.Z,
		Rotation.Pitch,
		Rotation.Yaw,
		Rotation.Roll,
		Forward.X,
		Forward.Y,
		Forward.Z,
		Right.X,
		Right.Y,
		Right.Z,
		Up.X,
		Up.Y,
		Up.Z,
		BoundsOrigin.X,
		BoundsOrigin.Y,
		BoundsOrigin.Z,
		BoundsExtent.X,
		BoundsExtent.Y,
		BoundsExtent.Z);

	TArray<AActor*> AttachedActors;
	Station->GetAttachedActors(AttachedActors, true, false);
	int32 SignIndex = 0;
	for (AActor* AttachedActor : AttachedActors)
	{
		AFGBuildableWidgetSign* Sign = Cast<AFGBuildableWidgetSign>(AttachedActor);
		if (IsValid(Sign))
		{
			WriteSignState(Sign, SignIndex, Output);
			++SignIndex;
		}
	}
	Output.Logf(TEXT("attachedWidgetSigns=%d"), SignIndex);
}

void FTruckStationSignConsoleCommands::WriteSignState(
	AFGBuildableWidgetSign* Sign,
	int32 SignIndex,
	FOutputDevice& Output,
	const AFGBuildableDockingStation* RelativeToStation) const
{
	TArray<FString> TagStrings;
	for (const FName Tag : Sign->Tags)
	{
		TagStrings.Add(Tag.ToString());
	}
	const FString Tags = FString::Join(TagStrings, TEXT(","));
	FPrefabSignData SignData;
	Sign->GetSignPrefabData(SignData);
	TArray<FString> TextElementStrings;
	for (const TPair<FString, FString>& TextElement : SignData.TextElementData)
	{
		TextElementStrings.Add(FString::Printf(TEXT("%s=%s"), *TextElement.Key, *TextElement.Value));
	}
	const FString TextElements = FString::Join(TextElementStrings, TEXT("|"));
	const USceneComponent* RootComponent = Sign->GetRootComponent();
	const FTransform RelativeTransform = RootComponent != nullptr
		? RootComponent->GetRelativeTransform()
		: FTransform::Identity;
	const FVector RelativeLocation = RelativeTransform.GetTranslation();
	const FRotator RelativeRotation = RelativeTransform.Rotator();
	const FVector WorldLocation = Sign->GetActorLocation();
	const FRotator WorldRotation = Sign->GetActorRotation();
	const FVector Forward = Sign->GetActorForwardVector();
	const FVector StationRelativeLocation = RelativeToStation != nullptr
		? RelativeToStation->GetActorTransform().InverseTransformPosition(WorldLocation)
		: FVector::ZeroVector;
	const FRotator StationRelativeRotation = RelativeToStation != nullptr
		? (Sign->GetActorTransform().GetRelativeTransform(RelativeToStation->GetActorTransform())).Rotator()
		: FRotator::ZeroRotator;
	FVector BoundsOrigin;
	FVector BoundsExtent;
	Sign->GetActorBounds(false, BoundsOrigin, BoundsExtent, true);
	const AActor* Owner = Sign->GetOwner();
	const AActor* AttachParent = Sign->GetAttachParentActor();
	const bool ShouldSave = IFGSaveInterface::Execute_ShouldSave(Sign);
	Output.Logf(
		TEXT("sign index=%d name=%s class=%s owner=%s attachParent=%s tags=[%s] text=[%s] collision=%s transient=%s shouldSave=%s relativeLocation=(%.3f,%.3f,%.3f) relativeRotation=(P=%.3f,Y=%.3f,R=%.3f) stationRelativeLocation=(%.3f,%.3f,%.3f) stationRelativeRotation=(P=%.3f,Y=%.3f,R=%.3f) worldLocation=(%.3f,%.3f,%.3f) worldRotation=(P=%.3f,Y=%.3f,R=%.3f) forward=(%.3f,%.3f,%.3f) boundsOrigin=(%.3f,%.3f,%.3f) boundsExtent=(%.3f,%.3f,%.3f)"),
		SignIndex,
		*Sign->GetName(),
		*Sign->GetClass()->GetName(),
		Owner != nullptr ? *Owner->GetName() : TEXT("none"),
		AttachParent != nullptr ? *AttachParent->GetName() : TEXT("none"),
		*Tags,
		*TextElements,
		Sign->GetActorEnableCollision() ? TEXT("true") : TEXT("false"),
		Sign->HasAnyFlags(RF_Transient) ? TEXT("true") : TEXT("false"),
		ShouldSave ? TEXT("true") : TEXT("false"),
		RelativeLocation.X,
		RelativeLocation.Y,
		RelativeLocation.Z,
		RelativeRotation.Pitch,
		RelativeRotation.Yaw,
		RelativeRotation.Roll,
		StationRelativeLocation.X,
		StationRelativeLocation.Y,
		StationRelativeLocation.Z,
		StationRelativeRotation.Pitch,
		StationRelativeRotation.Yaw,
		StationRelativeRotation.Roll,
		WorldLocation.X,
		WorldLocation.Y,
		WorldLocation.Z,
		WorldRotation.Pitch,
		WorldRotation.Yaw,
		WorldRotation.Roll,
		Forward.X,
		Forward.Y,
		Forward.Z,
		BoundsOrigin.X,
		BoundsOrigin.Y,
		BoundsOrigin.Z,
		BoundsExtent.X,
		BoundsExtent.Y,
		BoundsExtent.Z);
}

void FTruckStationSignConsoleCommands::RegisterCommand(IConsoleObject* ConsoleObject)
{
	if (ConsoleObject != nullptr)
	{
		RegisteredCommands.Add(ConsoleObject);
	}
}
