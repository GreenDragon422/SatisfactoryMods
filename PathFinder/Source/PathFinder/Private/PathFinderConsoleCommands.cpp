#include "PathFinderConsoleCommands.h"

#include "Blueprint/UserWidget.h"
#include "Components/LineBatchComponent.h"
#include "Components/PanelWidget.h"
#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "FGCharacterPlayer.h"
#include "FGInventoryComponent.h"
#include "FGInventoryComponentEquipment.h"
#include "FGPlayerController.h"
#include "FGResourceSinkSubsystem.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "PathFinderControlTool.h"
#include "PathFinderControlToolDescriptor.h"
#include "PathFinderInputTrace.h"
#include "PathFinderRouteMapLayerWidget.h"
#include "PathFinderRouteMapSubsystem.h"
#include "PathFinderRouteMapToggleWidget.h"
#include "PathFinderRouteTypes.h"
#include "PathFinderTraceService.h"
#include "PathFinderVehicleContext.h"
#include "Resources/FGItemDescriptor.h"
#include "Resources/FGEquipmentDescriptor.h"
#include "WheeledVehicles/FGDockingStationIdentifier.h"
#include "WheeledVehicles/FGVehiclePathNode.h"
#include "WheeledVehicles/FGVehiclePathSegment.h"
#include "WheeledVehicles/FGVehicleSubsystem.h"
#include "WheeledVehicles/FGWheeledVehicle.h"
#include "WheeledVehicles/FGWheeledVehicleIdentifier.h"
#include "UObject/UObjectIterator.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathFinder, Log, All);

namespace
{
	FString FormatPathFinderVector(const FVector& Location)
	{
		return FString::Printf(TEXT("X=%.1f Y=%.1f Z=%.1f"), Location.X, Location.Y, Location.Z);
	}

	FString FormatPathFinderMapMeters(const FVector& Location)
	{
		return FString::Printf(TEXT("X=%.1fm Y=%.1fm Z=%.1fm"), Location.X / 100.0, Location.Y / 100.0, Location.Z / 100.0);
	}

	FString FormatPathFinderCompassDirection(const float BearingDegrees)
	{
		static const TCHAR* DirectionNames[] =
		{
			TEXT("north"),
			TEXT("north-northeast"),
			TEXT("northeast"),
			TEXT("east-northeast"),
			TEXT("east"),
			TEXT("east-southeast"),
			TEXT("southeast"),
			TEXT("south-southeast"),
			TEXT("south"),
			TEXT("south-southwest"),
			TEXT("southwest"),
			TEXT("west-southwest"),
			TEXT("west"),
			TEXT("west-northwest"),
			TEXT("northwest"),
			TEXT("north-northwest")
		};

		const int32 DirectionIndex = FMath::RoundToInt(FMath::Fmod(BearingDegrees + 360.0f + 11.25f, 360.0f) / 22.5f) % UE_ARRAY_COUNT(DirectionNames);
		return DirectionNames[DirectionIndex];
	}

	FString FormatPathFinderBearingFromTo(const FVector& FromLocation, const FVector& ToLocation)
	{
		const FVector DeltaCentimeters = ToLocation - FromLocation;
		const double EastMeters = DeltaCentimeters.X / 100.0;
		const double SouthMeters = DeltaCentimeters.Y / 100.0;
		const double NorthMeters = -SouthMeters;
		const double DistanceMeters = FMath::Sqrt((EastMeters * EastMeters) + (SouthMeters * SouthMeters));
		const double BearingDegrees = FMath::Fmod(FMath::RadiansToDegrees(FMath::Atan2(EastMeters, NorthMeters)) + 360.0, 360.0);

		return FString::Printf(TEXT("%.1fm %s (bearing %.1f deg, delta east=%.1fm north=%.1fm, vertical=%.1fm)"),
			DistanceMeters,
			*FormatPathFinderCompassDirection(static_cast<float>(BearingDegrees)),
			BearingDegrees,
			EastMeters,
			NorthMeters,
			DeltaCentimeters.Z / 100.0);
	}

	FString FormatPathFinderWorldDeltaMeters(const FVector& FromLocation, const FVector& ToLocation)
	{
		const FVector DeltaCentimeters = ToLocation - FromLocation;
		return FString::Printf(TEXT("dX=%.1fm dY=%.1fm dZ=%.1fm"),
			DeltaCentimeters.X / 100.0,
			DeltaCentimeters.Y / 100.0,
			DeltaCentimeters.Z / 100.0);
	}

	void DrawPathFinderRuntimeLine(UWorld* World, const FVector& StartLocation, const FVector& EndLocation, const FLinearColor& Color, const float Thickness, const float LifeTime)
	{
		if (World == nullptr)
		{
			return;
		}

		ULineBatchComponent* LineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::Foreground);
		if (LineBatcher == nullptr)
		{
			LineBatcher = World->GetLineBatcher(UWorld::ELineBatcherType::World);
		}

		if (LineBatcher == nullptr)
		{
			return;
		}

		LineBatcher->DrawLine(StartLocation, EndLocation, Color, SDPG_Foreground, Thickness, LifeTime);
	}

	FString FormatPathFinderIndexList(const TArray<int32>& Indices)
	{
		FString Result = TEXT("[");
		for (int32 Index = 0; Index < Indices.Num(); ++Index)
		{
			if (Index > 0)
			{
				Result += TEXT(",");
			}

			Result += FString::FromInt(Indices[Index]);
		}

		Result += TEXT("]");
		return Result;
	}

	FString FormatPathFinderDisplayName(const FText& Text, const UObject* FallbackObject)
	{
		const FString DisplayName = Text.ToString();
		if (!DisplayName.IsEmpty())
		{
			return DisplayName;
		}

		return GetNameSafe(FallbackObject);
	}

	bool DoesPathFinderStationMatchQuery(AFGDockingStationIdentifier* StationIdentifier, const FString& Query, const bool bQueryIsGuid, const FGuid& QueryGuid)
	{
		if (StationIdentifier == nullptr)
		{
			return false;
		}

		if (bQueryIsGuid)
		{
			return StationIdentifier->GetPathNodeGUID() == QueryGuid;
		}

		return StationIdentifier->GetStationName().ToString().Contains(Query, ESearchCase::IgnoreCase);
	}

	void WritePathFinderConsoleLine(FOutputDevice* Output, const FString& Line)
	{
		if (Output != nullptr)
		{
			Output->Logf(TEXT("%s"), *Line);
		}
	}

	FString FormatPathFinderBool(const bool bValue)
	{
		return bValue ? TEXT("yes") : TEXT("no");
	}

	bool TryParsePathFinderToggle(const FString& Value, bool& bEnabled)
	{
		if (Value.Equals(TEXT("1"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("on"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("true"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("enable"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("enabled"), ESearchCase::IgnoreCase))
		{
			bEnabled = true;
			return true;
		}

		if (Value.Equals(TEXT("0"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("off"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("false"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("disable"), ESearchCase::IgnoreCase) ||
			Value.Equals(TEXT("disabled"), ESearchCase::IgnoreCase))
		{
			bEnabled = false;
			return true;
		}

		return false;
	}

	const FLinearColor PathFinderNetworkAssignedColor = FLinearColor::FromSRGBColor(FColor(242, 101, 17, 235));
	const FLinearColor PathFinderNetworkUnusedColor = FLinearColor::FromSRGBColor(FColor(105, 105, 105, 220));

	FLinearColor GetPathFinderTrafficColor(const int32 Bucket)
	{
		switch (Bucket)
		{
		case 0:
			return PathFinderNetworkUnusedColor;
		case 1:
			return FLinearColor(0.75f, 1.0f, 0.0f, 1.0f);
		case 2:
			return FLinearColor(1.0f, 0.55f, 0.0f, 1.0f);
		default:
			return FLinearColor(1.0f, 0.0f, 0.0f, 1.0f);
		}
	}

	FString FormatPathFinderAutopilotErrorStatus(const EVehicleAutopilotErrorStatus Status)
	{
		switch (Status)
		{
		case EVehicleAutopilotErrorStatus::None:
			return TEXT("none");
		case EVehicleAutopilotErrorStatus::StationUnreachable:
			return TEXT("station unreachable");
		case EVehicleAutopilotErrorStatus::NotOnPath:
			return TEXT("not on path");
		case EVehicleAutopilotErrorStatus::TooFewStations:
			return TEXT("too few stations");
		case EVehicleAutopilotErrorStatus::NoFuel:
			return TEXT("no fuel");
		case EVehicleAutopilotErrorStatus::Deadlocked:
			return TEXT("deadlocked");
		default:
			return TEXT("unknown");
		}
	}

	FString FormatPathFinderDockingStationStatus(const EDockingStationStatus Status)
	{
		switch (Status)
		{
		case EDockingStationStatus::DSS_Operational:
			return TEXT("operational");
		case EDockingStationStatus::DSS_FuelTypeWarning:
			return TEXT("fuel type warning");
		case EDockingStationStatus::DSS_NoPowerWarning:
			return TEXT("no power warning");
		default:
			return TEXT("unknown");
		}
	}

	bool TryResolvePathFinderViewerLocation(UWorld* World, FVector& ViewerLocation)
	{
		APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
		if (PlayerController == nullptr)
		{
			return false;
		}

		APawn* Pawn = PlayerController->GetPawn();
		if (Pawn != nullptr)
		{
			ViewerLocation = Pawn->GetActorLocation();
			return true;
		}

		FRotator ViewerRotation;
		PlayerController->GetPlayerViewPoint(ViewerLocation, ViewerRotation);
		return true;
	}

	bool TryResolvePathFinderNodeLocation(AFGVehicleSubsystem* VehicleSubsystem, const FGuid& PathNodeGuid, FVector& NodeLocation)
	{
		if (VehicleSubsystem == nullptr)
		{
			return false;
		}

		UFGVehiclePathNetwork* PathNetwork = VehicleSubsystem->FindNetworkByPathNodeGuid(PathNodeGuid);
		if (PathNetwork == nullptr)
		{
			return false;
		}

		const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
		const int32 NodeIndex = PathNetwork->FindPathNodeIndexByGuid(PathNodeGuid);
		if (!Nodes.IsValidIndex(NodeIndex))
		{
			return false;
		}

		NodeLocation = Nodes[NodeIndex].PathNodeLocation;
		return true;
	}

	void LogPathFinderEndpoint(AFGVehicleSubsystem* VehicleSubsystem, const FGuid& PathNodeGuid, const int32 RouteIndex)
	{
		if (VehicleSubsystem == nullptr)
		{
			UE_LOG(LogPathFinder, Warning, TEXT("  route[%d] guid=%s vehicle subsystem unavailable"), RouteIndex, *PathNodeGuid.ToString());
			return;
		}

		UFGVehiclePathNetwork* PathNetwork = VehicleSubsystem->FindNetworkByPathNodeGuid(PathNodeGuid);
		if (PathNetwork == nullptr)
		{
			UE_LOG(LogPathFinder, Warning, TEXT("  route[%d] guid=%s network=missing"), RouteIndex, *PathNodeGuid.ToString());
			return;
		}

		const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
		const int32 NodeIndex = PathNetwork->FindPathNodeIndexByGuid(PathNodeGuid);
		const FString LocationText = Nodes.IsValidIndex(NodeIndex) ? FormatPathFinderVector(Nodes[NodeIndex].PathNodeLocation) : TEXT("unknown");
		const FString MapLocationText = Nodes.IsValidIndex(NodeIndex) ? FormatPathFinderMapMeters(Nodes[NodeIndex].PathNodeLocation) : TEXT("unknown");
		AFGDockingStationIdentifier* StationIdentifier = VehicleSubsystem->FindDockingStationIdentifierForPathNodeGuid(PathNodeGuid);
		const FString StationName = StationIdentifier != nullptr ? StationIdentifier->GetStationName().ToString() : TEXT("<none>");
		const FString StationStatus = StationIdentifier != nullptr ? FormatPathFinderDockingStationStatus(StationIdentifier->GetStationStatus()) : TEXT("n/a");

		UE_LOG(LogPathFinder, Log, TEXT("  route[%d] guid=%s network=%d nodeIndex=%d locationCm=(%s) mapMeters=(%s) station=\"%s\" stationStatus=%s"),
			RouteIndex,
			*PathNodeGuid.ToString(),
			PathNetwork->GetNetworkID(),
			NodeIndex,
			*LocationText,
			*MapLocationText,
			*StationName,
			*StationStatus);
	}

	void LogPathFinderNetworkStations(UFGVehiclePathNetwork* PathNetwork)
	{
		if (PathNetwork == nullptr)
		{
			return;
		}

		TArray<AFGDockingStationIdentifier*> StationIdentifiers;
		PathNetwork->PopulateNetworkStations(StationIdentifiers);
		UE_LOG(LogPathFinder, Log, TEXT("Network %d stations: count=%d"), PathNetwork->GetNetworkID(), StationIdentifiers.Num());

		for (int32 StationIndex = 0; StationIndex < StationIdentifiers.Num(); ++StationIndex)
		{
			AFGDockingStationIdentifier* StationIdentifier = StationIdentifiers[StationIndex];
			if (StationIdentifier == nullptr)
			{
				UE_LOG(LogPathFinder, Warning, TEXT("  station[%d] <null>"), StationIndex);
				continue;
			}

			const FGuid PathNodeGuid = StationIdentifier->GetPathNodeGUID();
			const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
			const int32 NodeIndex = PathNetwork->FindPathNodeIndexByGuid(PathNodeGuid);
			const FString NodeLocationText = Nodes.IsValidIndex(NodeIndex) ? FormatPathFinderVector(Nodes[NodeIndex].PathNodeLocation) : TEXT("unknown");
			const FString NodeMapLocationText = Nodes.IsValidIndex(NodeIndex) ? FormatPathFinderMapMeters(Nodes[NodeIndex].PathNodeLocation) : TEXT("unknown");
			const FString ActorLocationText = FormatPathFinderVector(StationIdentifier->GetRealActorLocation());
			const FString ActorMapLocationText = FormatPathFinderMapMeters(StationIdentifier->GetRealActorLocation());

			UE_LOG(LogPathFinder, Log, TEXT("  station[%d] name=\"%s\" status=%s guid=%s nodeIndex=%d nodeLocationCm=(%s) nodeMapMeters=(%s) actorLocationCm=(%s) actorMapMeters=(%s)"),
				StationIndex,
				*StationIdentifier->GetStationName().ToString(),
				*FormatPathFinderDockingStationStatus(StationIdentifier->GetStationStatus()),
				*PathNodeGuid.ToString(),
				NodeIndex,
				*NodeLocationText,
				*NodeMapLocationText,
				*ActorLocationText,
				*ActorMapLocationText);
		}
	}

	void LogPathFinderSampleDetails(const FPathFinderLegResult& Result, const int32 MaxSamplesToLog)
	{
		const int32 SampleCountToLog = FMath::Min(Result.PaintSamples.Num(), MaxSamplesToLog);
		for (int32 SampleIndex = 0; SampleIndex < SampleCountToLog; ++SampleIndex)
		{
			const FPathFinderPaintSample& PaintSample = Result.PaintSamples[SampleIndex];
			const FString FirstPointText = PaintSample.WorldPoints.Num() > 0 ? FormatPathFinderVector(PaintSample.WorldPoints[0]) : TEXT("unknown");
			const FString LastPointText = PaintSample.WorldPoints.Num() > 0 ? FormatPathFinderVector(PaintSample.WorldPoints.Last()) : TEXT("unknown");

			UE_LOG(LogPathFinder, Log, TEXT("    sample[%d] segment=%d from=%s to=%s distance=%.1f alpha=%.3f points=%d first=(%s) last=(%s)"),
				SampleIndex,
				PaintSample.SegmentIndex,
				*PaintSample.FromNodeGuid.ToString(),
				*PaintSample.ToNodeGuid.ToString(),
				PaintSample.TravelDistance,
				PaintSample.GradientAlpha,
				PaintSample.WorldPoints.Num(),
				*FirstPointText,
				*LastPointText);
		}

		if (Result.PaintSamples.Num() > SampleCountToLog)
		{
			UE_LOG(LogPathFinder, Log, TEXT("    ... %d more samples omitted from log; raise MaxSamplesToLog in code if needed."), Result.PaintSamples.Num() - SampleCountToLog);
		}
	}

	void LogPathFinderGuidPath(UFGVehiclePathNetwork* PathNetwork, const FString& Label, const TArray<FGuid>& PathNodeGuids)
	{
		UE_LOG(LogPathFinder, Log, TEXT("    %s pathNodeCount=%d"), *Label, PathNodeGuids.Num());

		if (PathNetwork == nullptr)
		{
			for (int32 PathNodeIndex = 0; PathNodeIndex < PathNodeGuids.Num(); ++PathNodeIndex)
			{
				UE_LOG(LogPathFinder, Log, TEXT("      [%d] guid=%s network=missing"), PathNodeIndex, *PathNodeGuids[PathNodeIndex].ToString());
			}
			return;
		}

		const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
		for (int32 PathNodeIndex = 0; PathNodeIndex < PathNodeGuids.Num(); ++PathNodeIndex)
		{
			const FGuid& PathNodeGuid = PathNodeGuids[PathNodeIndex];
			const int32 NetworkNodeIndex = PathNetwork->FindPathNodeIndexByGuid(PathNodeGuid);
			const FString LocationText = Nodes.IsValidIndex(NetworkNodeIndex) ? FormatPathFinderVector(Nodes[NetworkNodeIndex].PathNodeLocation) : TEXT("unknown");
			UE_LOG(LogPathFinder, Log, TEXT("      [%d] guid=%s nodeIndex=%d location=(%s)"),
				PathNodeIndex,
				*PathNodeGuid.ToString(),
				NetworkNodeIndex,
				*LocationText);
		}
	}

	void LogPathFinderReferenceLocation(const FString& Label, const FVector& Location)
	{
		UE_LOG(LogPathFinder, Log, TEXT("%s location: cm=(%s) mapMeters=(%s)"),
			*Label,
			*FormatPathFinderVector(Location),
			*FormatPathFinderMapMeters(Location));
	}

	void LogPathFinderBearingToNode(AFGVehicleSubsystem* VehicleSubsystem, const FVector& ReferenceLocation, const FString& ReferenceLabel, const FString& TargetLabel, const FGuid& PathNodeGuid)
	{
		FVector NodeLocation;
		if (!TryResolvePathFinderNodeLocation(VehicleSubsystem, PathNodeGuid, NodeLocation))
		{
			UE_LOG(LogPathFinder, Warning, TEXT("  %s guid=%s reference=%s bearing=unavailable"),
				*TargetLabel,
				*PathNodeGuid.ToString(),
				*ReferenceLabel);
			return;
		}

		UE_LOG(LogPathFinder, Log, TEXT("  %s guid=%s reference=%s bearing=%s targetMapMeters=(%s)"),
			*TargetLabel,
			*PathNodeGuid.ToString(),
			*ReferenceLabel,
			*FormatPathFinderBearingFromTo(ReferenceLocation, NodeLocation),
			*FormatPathFinderMapMeters(NodeLocation));
	}

	int32 FindPathFinderRouteIndexByGuid(const TArray<FGuid>& RouteNodeGuids, const FGuid& PathNodeGuid)
	{
		for (int32 RouteIndex = 0; RouteIndex < RouteNodeGuids.Num(); ++RouteIndex)
		{
			if (RouteNodeGuids[RouteIndex] == PathNodeGuid)
			{
				return RouteIndex;
			}
		}

		return INDEX_NONE;
	}

	void LogPathFinderNearestRouteEndpoint(AFGVehicleSubsystem* VehicleSubsystem, const TArray<FGuid>& RouteNodeGuids, const FVector& ReferenceLocation, const FString& ReferenceLabel)
	{
		int32 ClosestRouteIndex = INDEX_NONE;
		FGuid ClosestPathNodeGuid;
		FVector ClosestLocation = FVector::ZeroVector;
		float ClosestDistance = TNumericLimits<float>::Max();

		for (int32 RouteIndex = 0; RouteIndex < RouteNodeGuids.Num(); ++RouteIndex)
		{
			FVector NodeLocation;
			if (!TryResolvePathFinderNodeLocation(VehicleSubsystem, RouteNodeGuids[RouteIndex], NodeLocation))
			{
				continue;
			}

			const float Distance = FVector::Dist2D(ReferenceLocation, NodeLocation);
			if (Distance < ClosestDistance)
			{
				ClosestRouteIndex = RouteIndex;
				ClosestPathNodeGuid = RouteNodeGuids[RouteIndex];
				ClosestLocation = NodeLocation;
				ClosestDistance = Distance;
			}
		}

		if (ClosestRouteIndex == INDEX_NONE)
		{
			UE_LOG(LogPathFinder, Warning, TEXT("Nearest route endpoint from %s: unavailable"), *ReferenceLabel);
			return;
		}

		UE_LOG(LogPathFinder, Log, TEXT("Nearest route endpoint from %s: route[%d] guid=%s mapMeters=(%s) bearing=%s"),
			*ReferenceLabel,
			ClosestRouteIndex,
			*ClosestPathNodeGuid.ToString(),
			*FormatPathFinderMapMeters(ClosestLocation),
			*FormatPathFinderBearingFromTo(ReferenceLocation, ClosestLocation));
	}

	void LogPathFinderNearestNetworkNode(UFGVehiclePathNetwork* PathNetwork, const FVector& ReferenceLocation, const FString& ReferenceLabel)
	{
		if (PathNetwork == nullptr)
		{
			UE_LOG(LogPathFinder, Warning, TEXT("Nearest network node from %s: network unavailable"), *ReferenceLabel);
			return;
		}

		const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
		int32 ClosestNodeIndex = INDEX_NONE;
		float ClosestDistance = TNumericLimits<float>::Max();
		for (int32 NodeIndex = 0; NodeIndex < Nodes.Num(); ++NodeIndex)
		{
			const float Distance = FVector::Dist2D(ReferenceLocation, Nodes[NodeIndex].PathNodeLocation);
			if (Distance < ClosestDistance)
			{
				ClosestNodeIndex = NodeIndex;
				ClosestDistance = Distance;
			}
		}

		if (!Nodes.IsValidIndex(ClosestNodeIndex))
		{
			UE_LOG(LogPathFinder, Warning, TEXT("Nearest network node from %s: unavailable"), *ReferenceLabel);
			return;
		}

		UE_LOG(LogPathFinder, Log, TEXT("Nearest network node from %s: network=%d nodeIndex=%d guid=%s mapMeters=(%s) bearing=%s"),
			*ReferenceLabel,
			PathNetwork->GetNetworkID(),
			ClosestNodeIndex,
			*Nodes[ClosestNodeIndex].PathNodeGuid.ToString(),
			*FormatPathFinderMapMeters(Nodes[ClosestNodeIndex].PathNodeLocation),
			*FormatPathFinderBearingFromTo(ReferenceLocation, Nodes[ClosestNodeIndex].PathNodeLocation));
	}

	void LogPathFinderCurrentRouteState(AFGVehicleSubsystem* VehicleSubsystem, const FPathFinderVehicleContext& Context, const TArray<FGuid>& RouteNodeGuids)
	{
		const FGuid CurrentFromGuid = Context.VehicleIdentifier->GetCurrentFromPathNodeGUID();
		const FGuid CurrentToGuid = Context.VehicleIdentifier->GetCurrentToPathNodeGUID();
		const int32 CurrentFromRouteIndex = FindPathFinderRouteIndexByGuid(RouteNodeGuids, CurrentFromGuid);
		const int32 CurrentToRouteIndex = FindPathFinderRouteIndexByGuid(RouteNodeGuids, CurrentToGuid);
		const int32 TargetWaypointIndex = Context.VehicleIdentifier->GetCurrentTargetWaypointIndex();
		const bool bHasTargetWaypoint = RouteNodeGuids.IsValidIndex(TargetWaypointIndex);

		UE_LOG(LogPathFinder, Log, TEXT("Current route mapping: currentFromRouteIndex=%d currentToRouteIndex=%d targetWaypointIndex=%d targetWaypointGuid=%s"),
			CurrentFromRouteIndex,
			CurrentToRouteIndex,
			TargetWaypointIndex,
			bHasTargetWaypoint ? *RouteNodeGuids[TargetWaypointIndex].ToString() : TEXT("<invalid>"));

		UFGVehiclePathNetwork* CurrentNetwork = VehicleSubsystem != nullptr ? VehicleSubsystem->FindNetworkByPathNodeGuid(CurrentFromGuid) : nullptr;
		if (CurrentNetwork == nullptr)
		{
			CurrentNetwork = RouteNodeGuids.Num() > 0 && VehicleSubsystem != nullptr ? VehicleSubsystem->FindNetworkByPathNodeGuid(RouteNodeGuids[0]) : nullptr;
		}

		if (CurrentNetwork == nullptr)
		{
			UE_LOG(LogPathFinder, Warning, TEXT("Current route mapping: current network unavailable"));
			return;
		}

		const int32 FromNodeIndex = CurrentNetwork->FindPathNodeIndexByGuid(CurrentFromGuid);
		const int32 ToNodeIndex = CurrentNetwork->FindPathNodeIndexByGuid(CurrentToGuid);
		const int32 SegmentIndex = CurrentNetwork->FindPathIndexBetweenPathNodes(FromNodeIndex, ToNodeIndex);
		const TArray<FVehiclePathNetworkSegmentData>& Segments = CurrentNetwork->GetPathSegments();
		const bool bHasCurrentSegment = Segments.IsValidIndex(SegmentIndex);
		const bool bCurrentSegmentTraversable = bHasCurrentSegment && CurrentNetwork->CanVehicleTraverseSegment(Segments[SegmentIndex], Context.VehiclePathPreset);

		UE_LOG(LogPathFinder, Log, TEXT("Current network mapping: network=%d traversabilityChangelist=%d fromNodeIndex=%d toNodeIndex=%d segmentIndex=%d segmentLength=%.1fm traversable=%s"),
			CurrentNetwork->GetNetworkID(),
			CurrentNetwork->GetNetworkPathTraversabilityChangelist(),
			FromNodeIndex,
			ToNodeIndex,
			SegmentIndex,
			bHasCurrentSegment ? Segments[SegmentIndex].SegmentLength / 100.0f : 0.0f,
			*FormatPathFinderBool(bCurrentSegmentTraversable));
	}

	void LogPathFinderNodeNeighborhood(UFGVehiclePathNetwork* PathNetwork, const UFGVehiclePathPreset* VehiclePathPreset, const int32 NodeIndex, const FString& Label, const FVector& VehicleLocation, const FVector& PlayerLocation, const bool bHasPlayerLocation, const int32 MaxSegmentsToLog)
	{
		if (PathNetwork == nullptr)
		{
			return;
		}

		const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
		const TArray<FVehiclePathNetworkSegmentData>& Segments = PathNetwork->GetPathSegments();
		if (!Nodes.IsValidIndex(NodeIndex))
		{
			UE_LOG(LogPathFinder, Warning, TEXT("    %s neighborhood unavailable: nodeIndex=%d invalid"), *Label, NodeIndex);
			return;
		}

		int32 OutgoingCount = 0;
		int32 IncomingCount = 0;
		for (const FVehiclePathNetworkSegmentData& SegmentData : Segments)
		{
			if (SegmentData.FromNodeIndex == NodeIndex)
			{
				++OutgoingCount;
			}
			if (SegmentData.ToNodeIndex == NodeIndex)
			{
				++IncomingCount;
			}
		}

		UE_LOG(LogPathFinder, Log, TEXT("    %s nodeIndex=%d guid=%s mapMeters=(%s) fromVehicle=%s outgoing=%d incoming=%d loggingLimit=%d"),
			*Label,
			NodeIndex,
			*Nodes[NodeIndex].PathNodeGuid.ToString(),
			*FormatPathFinderMapMeters(Nodes[NodeIndex].PathNodeLocation),
			*FormatPathFinderBearingFromTo(VehicleLocation, Nodes[NodeIndex].PathNodeLocation),
			OutgoingCount,
			IncomingCount,
			MaxSegmentsToLog);

		if (bHasPlayerLocation)
		{
			UE_LOG(LogPathFinder, Log, TEXT("    %s fromPlayer=%s"), *Label, *FormatPathFinderBearingFromTo(PlayerLocation, Nodes[NodeIndex].PathNodeLocation));
		}

		int32 LoggedSegmentCount = 0;
		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num() && LoggedSegmentCount < MaxSegmentsToLog; ++SegmentIndex)
		{
			const FVehiclePathNetworkSegmentData& SegmentData = Segments[SegmentIndex];
			const bool bOutgoing = SegmentData.FromNodeIndex == NodeIndex;
			const bool bIncoming = SegmentData.ToNodeIndex == NodeIndex;
			if (!bOutgoing && !bIncoming)
			{
				continue;
			}

			const int32 NeighborNodeIndex = bOutgoing ? SegmentData.ToNodeIndex : SegmentData.FromNodeIndex;
			const bool bHasNeighbor = Nodes.IsValidIndex(NeighborNodeIndex);
			const FString NeighborGuidText = bHasNeighbor ? Nodes[NeighborNodeIndex].PathNodeGuid.ToString() : TEXT("<invalid>");
			const FString NeighborMapText = bHasNeighbor ? FormatPathFinderMapMeters(Nodes[NeighborNodeIndex].PathNodeLocation) : TEXT("unknown");
			const FString NeighborBearingText = bHasNeighbor ? FormatPathFinderBearingFromTo(VehicleLocation, Nodes[NeighborNodeIndex].PathNodeLocation) : TEXT("unknown");
			const FString PlayerBearingText = bHasPlayerLocation && bHasNeighbor ? FormatPathFinderBearingFromTo(PlayerLocation, Nodes[NeighborNodeIndex].PathNodeLocation) : TEXT("n/a");
			const bool bTraversable = VehiclePathPreset != nullptr && PathNetwork->CanVehicleTraverseSegment(SegmentData, VehiclePathPreset);

			UE_LOG(LogPathFinder, Log, TEXT("      segment[%d] %s neighborNodeIndex=%d neighborGuid=%s neighborMapMeters=(%s) length=%.1fm traversable=%s mask=%lld virtualLengthCount=%d neighborFromVehicle=%s neighborFromPlayer=%s"),
				SegmentIndex,
				bOutgoing ? TEXT("outgoing") : TEXT("incoming"),
				NeighborNodeIndex,
				*NeighborGuidText,
				*NeighborMapText,
				SegmentData.SegmentLength / 100.0f,
				*FormatPathFinderBool(bTraversable),
				SegmentData.VehicleTraversabilityMask,
				SegmentData.VehicleSegmentVirtualLength.Num(),
				*NeighborBearingText,
				*PlayerBearingText);

			++LoggedSegmentCount;
		}

		if (OutgoingCount + IncomingCount > LoggedSegmentCount)
		{
			UE_LOG(LogPathFinder, Log, TEXT("      ... %d more neighbor segments omitted"), OutgoingCount + IncomingCount - LoggedSegmentCount);
		}
	}
}

void FPathFinderConsoleCommands::Register()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	InputTrace = MakeUnique<FPathFinderInputTrace>();

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.Help"),
		TEXT("List PathFinder debug commands."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::HelpWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.GiveControlTool"),
		TEXT("Add the PathFinder Route Dial prototype to the local player's inventory."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::GiveControlToolWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.TriggerControlToolSecondary"),
		TEXT("Trigger the equipped Route Dial through Satisfactory's default SecondaryFire equipment action API."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::TriggerControlToolSecondaryWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.ShowVehicleContext"),
		TEXT("Show the current PathFinder vehicle context."),
		FConsoleCommandDelegate::CreateRaw(this, &FPathFinderConsoleCommands::ShowVehicleContext),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.ShowRoute"),
		TEXT("Show directed loop legs for the current vehicle route."),
		FConsoleCommandDelegate::CreateRaw(this, &FPathFinderConsoleCommands::ShowRoute),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.ScanRoute"),
		TEXT("Trace every directed loop leg for the current vehicle route."),
		FConsoleCommandDelegate::CreateRaw(this, &FPathFinderConsoleCommands::ScanRoute),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.FindVehiclesForStation"),
		TEXT("Find vehicles whose route visits a station. Usage: PathFinder.FindVehiclesForStation <station name|guid>"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::FindVehiclesForStationWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.TrackStationVehicles"),
		TEXT("Live-track vehicles whose route visits a station. Usage: PathFinder.TrackStationVehicles <station name|guid|off>"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::TrackStationVehiclesWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.VehicleTracing"),
		TEXT("Turn vehicle route tracing overlay on or off. Usage: PathFinder.VehicleTracing <on|off>"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::SetVehicleTracingWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.ShowNearestTrafficLabel"),
		TEXT("Report the nearest route-load label plane and its alignment with the traffic strip."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::ShowNearestTrafficLabelWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.MoveToNearestTrafficLabel"),
		TEXT("Move the player above the nearest route-load label for visual inspection."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::MoveToNearestTrafficLabelWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.TraceLeg"),
		TEXT("Trace one directed route leg by index. Usage: PathFinder.TraceLeg 0"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPathFinderConsoleCommands::TraceLeg),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.InputTrace"),
		TEXT("Toggle PathFinder input/widget trace. Usage: PathFinder.InputTrace 1"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FPathFinderConsoleCommands::SetInputTrace),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.ToggleMap"),
		TEXT("Toggle the local player's map for route-map diagnostics."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::ToggleMapWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.RouteMapStatus"),
		TEXT("Report route-map geometry, render target, and live widget state."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::ShowRouteMapStatusWithOutput),
		ECVF_Default));

	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("PathFinder.Clear"),
		TEXT("Clear PathFinder debug state."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FPathFinderConsoleCommands::ClearWithOutput),
		ECVF_Default));

	UE_LOG(LogPathFinder, Log, TEXT("PathFinder console commands registered."));
}

void FPathFinderConsoleCommands::Unregister()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();

	StopStationVehicleTracker();
	StopVehicleTracing();

	for (IConsoleObject* ConsoleObject : RegisteredCommands)
	{
		if (ConsoleObject != nullptr)
		{
			ConsoleManager.UnregisterConsoleObject(ConsoleObject);
		}
	}

	RegisteredCommands.Empty();
	InputTrace.Reset();
	UE_LOG(LogPathFinder, Log, TEXT("PathFinder console commands unregistered."));
}

UWorld* FPathFinderConsoleCommands::ResolveWorld() const
{
	if (GEngine == nullptr)
	{
		return nullptr;
	}

	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (World != nullptr && (World->WorldType == EWorldType::Game || World->WorldType == EWorldType::PIE))
		{
			return World;
		}
	}

	return nullptr;
}

void FPathFinderConsoleCommands::RegisterCommand(IConsoleObject* ConsoleObject)
{
	if (ConsoleObject != nullptr)
	{
		RegisteredCommands.Add(ConsoleObject);
	}
}

void FPathFinderConsoleCommands::Help() const
{
	HelpInternal(nullptr);
}

void FPathFinderConsoleCommands::HelpWithOutput(FOutputDevice& Output) const
{
	HelpInternal(&Output);
}

void FPathFinderConsoleCommands::HelpInternal(FOutputDevice* Output) const
{
	const TCHAR* HelpLines[] =
	{
		TEXT("PathFinder commands:"),
		TEXT("  PathFinder.Help"),
		TEXT("  PathFinder.GiveControlTool"),
		TEXT("  PathFinder.TriggerControlToolSecondary"),
		TEXT("  PathFinder.ShowVehicleContext"),
		TEXT("  PathFinder.ShowRoute"),
		TEXT("  PathFinder.ScanRoute"),
		TEXT("  PathFinder.FindVehiclesForStation <station name|guid>"),
		TEXT("  PathFinder.TrackStationVehicles <station name|guid|off>"),
		TEXT("  PathFinder.VehicleTracing <on|off>"),
		TEXT("  PathFinder.TraceLeg <index>"),
		TEXT("  PathFinder.InputTrace <0|1>"),
		TEXT("  PathFinder.ToggleMap"),
		TEXT("  PathFinder.RouteMapStatus"),
		TEXT("  PathFinder.Clear")
	};

	for (const TCHAR* HelpLine : HelpLines)
	{
		UE_LOG(LogPathFinder, Log, TEXT("%s"), HelpLine);
		WritePathFinderConsoleLine(Output, HelpLine);
	}
}

void FPathFinderConsoleCommands::GiveControlToolWithOutput(FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	AFGPlayerController* PlayerController = World != nullptr
		? Cast<AFGPlayerController>(World->GetFirstPlayerController())
		: nullptr;
	AFGCharacterPlayer* PlayerCharacter = PlayerController != nullptr
		? Cast<AFGCharacterPlayer>(PlayerController->GetPawn())
		: nullptr;
	UFGInventoryComponent* Inventory = PlayerCharacter != nullptr ? PlayerCharacter->GetInventory() : nullptr;
	if (Inventory == nullptr)
	{
		WritePathFinderConsoleLine(&Output, TEXT("Cannot give Route Dial: local player inventory unavailable."));
		return;
	}

	const FInventoryStack ControlToolStack(1, UPathFinderControlToolDescriptor::StaticClass());
	UFGInventoryComponentEquipment* ArmsInventory = PlayerCharacter->GetEquipmentSlot(EEquipmentSlot::ES_ARMS);
	const int32 AddedToArmsCount = ArmsInventory != nullptr
		? ArmsInventory->AddStack(ControlToolStack, false)
		: 0;
	if (AddedToArmsCount == 1)
	{
		const int32 ControlToolIndex = ArmsInventory->FindFirstIndexWithItemType(UPathFinderControlToolDescriptor::StaticClass());
		if (ControlToolIndex != INDEX_NONE)
		{
			ArmsInventory->SetActiveEquipmentIndex(ControlToolIndex);
		}

		WritePathFinderConsoleLine(&Output, TEXT("PathFinder Route Filter added to the arms equipment inventory and selected."));
		return;
	}

	const int32 AddedItemCount = Inventory->AddStack(ControlToolStack, false);
	if (AddedItemCount == 1)
	{
		WritePathFinderConsoleLine(&Output, TEXT("PathFinder Route Filter added to the player inventory; move it to an arms equipment slot to use it."));
		return;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.Owner = PlayerCharacter;
	SpawnParameters.Instigator = PlayerCharacter;
	APathFinderControlTool* ControlTool = World->SpawnActor<APathFinderControlTool>(
		APathFinderControlTool::StaticClass(),
		PlayerCharacter->GetActorTransform(),
		SpawnParameters);
	if (ControlTool != nullptr)
	{
		AFGEquipment* ExistingEquipment = PlayerCharacter->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS);
		if (ExistingEquipment != nullptr)
		{
			PlayerCharacter->ClearOverrideEquipment(ExistingEquipment);
		}
		PlayerCharacter->SetOverrideEquipment(ControlTool);
		WritePathFinderConsoleLine(&Output, TEXT("PathFinder Route Filter selected as a temporary equipment override because both inventories are full."));
		return;
	}

	WritePathFinderConsoleLine(&Output, TEXT("Cannot give PathFinder Route Filter: arms equipment and player inventories are full."));
}

void FPathFinderConsoleCommands::TriggerControlToolSecondaryWithOutput(FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	AFGPlayerController* PlayerController = World != nullptr
		? Cast<AFGPlayerController>(World->GetFirstPlayerController())
		: nullptr;
	AFGCharacterPlayer* PlayerCharacter = PlayerController != nullptr
		? Cast<AFGCharacterPlayer>(PlayerController->GetPawn())
		: nullptr;
	APathFinderControlTool* ControlTool = PlayerCharacter != nullptr
		? Cast<APathFinderControlTool>(PlayerCharacter->GetEquipmentInSlot(EEquipmentSlot::ES_ARMS))
		: nullptr;
	if (ControlTool == nullptr)
	{
		WritePathFinderConsoleLine(&Output, TEXT("Cannot trigger Route Dial secondary action: the Route Dial is not equipped."));
		return;
	}

	ControlTool->TriggerDefaultEquipmentActionEvent(
		EDefaultEquipmentAction::SecondaryFire,
		EDefaultEquipmentActionEvent::Pressed);
	ControlTool->TriggerDefaultEquipmentActionEvent(
		EDefaultEquipmentAction::SecondaryFire,
		EDefaultEquipmentActionEvent::Released);
	WritePathFinderConsoleLine(&Output, TEXT("Triggered Route Dial SecondaryFire through the equipment action API."));
}

void FPathFinderConsoleCommands::ShowVehicleContext() const
{
	const FPathFinderVehicleContext Context = FPathFinderVehicleContextResolver::Resolve(ResolveWorld());
	UE_LOG(LogPathFinder, Log, TEXT("Vehicle context: %s"), *Context.ToDebugString());
}

void FPathFinderConsoleCommands::ShowRoute() const
{
	const FPathFinderVehicleContext Context = FPathFinderVehicleContextResolver::Resolve(ResolveWorld());
	if (!Context.IsValid())
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Cannot show route: %s"), *Context.ToDebugString());
		return;
	}

	const TArray<FGuid>& RouteNodeGuids = Context.VehicleIdentifier->GetVehicleRoute();
	const TArray<FPathFinderRouteLeg> Legs = FPathFinderRouteBuilder::BuildLoopLegs(RouteNodeGuids);
	AFGVehicleSubsystem* VehicleSubsystem = AFGVehicleSubsystem::Get(Context.World);
	const FVector VehicleLocation = Context.WheeledVehicle->GetVehicleLocation();
	const bool bHasPlayerLocation = Context.Pawn != nullptr;
	const FVector PlayerLocation = bHasPlayerLocation ? Context.Pawn->GetActorLocation() : FVector::ZeroVector;

	UE_LOG(LogPathFinder, Log, TEXT("Vehicle route has %d endpoints and %d directed loop legs."), RouteNodeGuids.Num(), Legs.Num());
	LogPathFinderReferenceLocation(TEXT("Vehicle"), VehicleLocation);
	if (bHasPlayerLocation)
	{
		LogPathFinderReferenceLocation(TEXT("Player/pawn"), PlayerLocation);
	}

	UE_LOG(LogPathFinder, Log, TEXT("Vehicle route endpoints:"));
	for (int32 RouteIndex = 0; RouteIndex < RouteNodeGuids.Num(); ++RouteIndex)
	{
		LogPathFinderEndpoint(VehicleSubsystem, RouteNodeGuids[RouteIndex], RouteIndex);
		LogPathFinderBearingToNode(VehicleSubsystem, VehicleLocation, TEXT("vehicle"), FString::Printf(TEXT("route[%d]"), RouteIndex), RouteNodeGuids[RouteIndex]);
		if (bHasPlayerLocation)
		{
			LogPathFinderBearingToNode(VehicleSubsystem, PlayerLocation, TEXT("player/pawn"), FString::Printf(TEXT("route[%d]"), RouteIndex), RouteNodeGuids[RouteIndex]);
		}
	}

	LogPathFinderCurrentRouteState(VehicleSubsystem, Context, RouteNodeGuids);
	LogPathFinderNearestRouteEndpoint(VehicleSubsystem, RouteNodeGuids, VehicleLocation, TEXT("vehicle"));
	if (bHasPlayerLocation)
	{
		LogPathFinderNearestRouteEndpoint(VehicleSubsystem, RouteNodeGuids, PlayerLocation, TEXT("player/pawn"));
	}

	UFGVehiclePathNetwork* ReferenceNetwork = VehicleSubsystem != nullptr && RouteNodeGuids.Num() > 0 ? VehicleSubsystem->FindNetworkByPathNodeGuid(RouteNodeGuids[0]) : nullptr;
	if (ReferenceNetwork != nullptr)
	{
		LogPathFinderNearestNetworkNode(ReferenceNetwork, VehicleLocation, TEXT("vehicle"));
		if (bHasPlayerLocation)
		{
			LogPathFinderNearestNetworkNode(ReferenceNetwork, PlayerLocation, TEXT("player/pawn"));
		}
	}

	if (RouteNodeGuids.Num() < 2)
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Vehicle route has fewer than 2 endpoints."));
		return;
	}

	for (const FPathFinderRouteLeg& Leg : Legs)
	{
		UE_LOG(LogPathFinder, Log, TEXT("  %s"), *Leg.ToDebugString());
	}
}

void FPathFinderConsoleCommands::ScanRoute() const
{
	const FPathFinderVehicleContext Context = FPathFinderVehicleContextResolver::Resolve(ResolveWorld());
	if (!Context.IsValid())
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Cannot scan route: %s"), *Context.ToDebugString());
		return;
	}

	AFGVehicleSubsystem* VehicleSubsystem = AFGVehicleSubsystem::Get(Context.World);
	const TArray<FGuid>& RouteNodeGuids = Context.VehicleIdentifier->GetVehicleRoute();
	const FVector VehicleLocation = Context.WheeledVehicle->GetVehicleLocation();
	const bool bHasPlayerLocation = Context.Pawn != nullptr;
	const FVector PlayerLocation = bHasPlayerLocation ? Context.Pawn->GetActorLocation() : FVector::ZeroVector;
	UE_LOG(LogPathFinder, Log, TEXT("Vehicle state: name=\"%s\" autopilot=%s autopilotError=%s currentlyDocking=%s currentTargetWaypointIndex=%d routeChangelist=%d currentSegment=%s -> %s"),
		*Context.VehicleIdentifier->GetVehicleName().ToString(),
		*FormatPathFinderBool(Context.VehicleIdentifier->IsAutopilotEnabled()),
		*FormatPathFinderAutopilotErrorStatus(Context.VehicleIdentifier->GetAutopilotErrorStatus()),
		*FormatPathFinderBool(Context.VehicleIdentifier->IsCurrentlyDocking()),
		Context.VehicleIdentifier->GetCurrentTargetWaypointIndex(),
		Context.VehicleIdentifier->GetVehicleRouteChangelist(),
		*Context.VehicleIdentifier->GetCurrentFromPathNodeGUID().ToString(),
		*Context.VehicleIdentifier->GetCurrentToPathNodeGUID().ToString());
	LogPathFinderReferenceLocation(TEXT("Vehicle"), VehicleLocation);
	if (bHasPlayerLocation)
	{
		LogPathFinderReferenceLocation(TEXT("Player/pawn"), PlayerLocation);
	}

	UE_LOG(LogPathFinder, Log, TEXT("Vehicle route endpoints before scan:"));
	for (int32 RouteIndex = 0; RouteIndex < RouteNodeGuids.Num(); ++RouteIndex)
	{
		LogPathFinderEndpoint(VehicleSubsystem, RouteNodeGuids[RouteIndex], RouteIndex);
		LogPathFinderBearingToNode(VehicleSubsystem, VehicleLocation, TEXT("vehicle"), FString::Printf(TEXT("route[%d]"), RouteIndex), RouteNodeGuids[RouteIndex]);
		if (bHasPlayerLocation)
		{
			LogPathFinderBearingToNode(VehicleSubsystem, PlayerLocation, TEXT("player/pawn"), FString::Printf(TEXT("route[%d]"), RouteIndex), RouteNodeGuids[RouteIndex]);
		}
	}

	LogPathFinderCurrentRouteState(VehicleSubsystem, Context, RouteNodeGuids);
	LogPathFinderNearestRouteEndpoint(VehicleSubsystem, RouteNodeGuids, VehicleLocation, TEXT("vehicle"));
	if (bHasPlayerLocation)
	{
		LogPathFinderNearestRouteEndpoint(VehicleSubsystem, RouteNodeGuids, PlayerLocation, TEXT("player/pawn"));
	}

	UFGVehiclePathNetwork* ReferenceNetwork = VehicleSubsystem != nullptr && RouteNodeGuids.Num() > 0 ? VehicleSubsystem->FindNetworkByPathNodeGuid(RouteNodeGuids[0]) : nullptr;
	if (ReferenceNetwork != nullptr)
	{
		LogPathFinderNearestNetworkNode(ReferenceNetwork, VehicleLocation, TEXT("vehicle"));
		if (bHasPlayerLocation)
		{
			LogPathFinderNearestNetworkNode(ReferenceNetwork, PlayerLocation, TEXT("player/pawn"));
		}
	}

	const FPathFinderRouteScanResult ScanResult = FPathFinderTraceService::ScanRoute(Context);
	UE_LOG(LogPathFinder, Log, TEXT("Route scan: legs=%d, detail=%s"), ScanResult.LegResults.Num(), *ScanResult.Detail);

	TSet<int32> LoggedNetworkIds;
	for (const FPathFinderLegResult& Result : ScanResult.LegResults)
	{
		UFGVehiclePathNetwork* PathNetwork = VehicleSubsystem != nullptr ? VehicleSubsystem->FindNetworkByPathNodeGuid(Result.Leg.FromNodeGuid) : nullptr;
		if (PathNetwork != nullptr && !LoggedNetworkIds.Contains(PathNetwork->GetNetworkID()))
		{
			LoggedNetworkIds.Add(PathNetwork->GetNetworkID());
			LogPathFinderNetworkStations(PathNetwork);
		}

		UE_LOG(LogPathFinder, Log, TEXT("  %s status=%s, pathNodes=%d, paintSamples=%d, detail=%s"),
			*Result.Leg.ToDebugString(),
			*FPathFinderRouteBuilder::LegStatusToString(Result.Status),
			Result.PathNodeGuids.Num(),
			Result.PaintSamples.Num(),
			*Result.Detail);
		UE_LOG(LogPathFinder, Log, TEXT("    network=%d nodes=%d segments=%d traversableSegments=%d fromIndex=%d toIndex=%d"),
			Result.NetworkId,
			Result.NetworkNodeCount,
			Result.NetworkSegmentCount,
			Result.TraversableSegmentCount,
			Result.FromNodeIndex,
			Result.ToNodeIndex);
		UE_LOG(LogPathFinder, Log, TEXT("    endpointDegrees from: outgoing=%d incoming=%d; to: outgoing=%d incoming=%d"),
			Result.FromOutgoingSegmentCount,
			Result.FromIncomingSegmentCount,
			Result.ToOutgoingSegmentCount,
			Result.ToIncomingSegmentCount);
		UE_LOG(LogPathFinder, Log, TEXT("    fromLocationKnown=%s fromCm=(%s) fromMapMeters=(%s) toLocationKnown=%s toCm=(%s) toMapMeters=(%s)"),
			*FormatPathFinderBool(Result.bHasFromNodeLocation),
			*FormatPathFinderVector(Result.FromNodeLocation),
			*FormatPathFinderMapMeters(Result.FromNodeLocation),
			*FormatPathFinderBool(Result.bHasToNodeLocation),
			*FormatPathFinderVector(Result.ToNodeLocation),
			*FormatPathFinderMapMeters(Result.ToNodeLocation));
		if (Result.bHasFromNodeLocation)
		{
			UE_LOG(LogPathFinder, Log, TEXT("    fromEndpointFromVehicle=%s"), *FormatPathFinderBearingFromTo(VehicleLocation, Result.FromNodeLocation));
			if (bHasPlayerLocation)
			{
				UE_LOG(LogPathFinder, Log, TEXT("    fromEndpointFromPlayer=%s"), *FormatPathFinderBearingFromTo(PlayerLocation, Result.FromNodeLocation));
			}
		}
		if (Result.bHasToNodeLocation)
		{
			UE_LOG(LogPathFinder, Log, TEXT("    toEndpointFromVehicle=%s"), *FormatPathFinderBearingFromTo(VehicleLocation, Result.ToNodeLocation));
			if (bHasPlayerLocation)
			{
				UE_LOG(LogPathFinder, Log, TEXT("    toEndpointFromPlayer=%s"), *FormatPathFinderBearingFromTo(PlayerLocation, Result.ToNodeLocation));
			}
		}
		UE_LOG(LogPathFinder, Log, TEXT("    independentSearch found=%s visitedNodes=%d relaxedSegments=%d travelDistance=%.1f independentPathNodes=%d"),
			*FormatPathFinderBool(Result.bIndependentSearchFoundPath),
			Result.SearchVisitedNodeCount,
			Result.SearchRelaxedSegmentCount,
			Result.SearchTravelDistance,
			Result.IndependentPathNodeGuids.Num());

		if (Result.bHasClosestNodeLocation)
		{
			UE_LOG(LogPathFinder, Log, TEXT("    closestReachedNode guid=%s locationCm=(%s) mapMeters=(%s) distanceToTarget=%.1f travelDistance=%.1f fromVehicle=%s"),
				*Result.ClosestNodeGuid.ToString(),
				*FormatPathFinderVector(Result.ClosestNodeLocation),
				*FormatPathFinderMapMeters(Result.ClosestNodeLocation),
				Result.ClosestNodeDistanceToTarget,
				Result.ClosestNodeTravelDistance,
				*FormatPathFinderBearingFromTo(VehicleLocation, Result.ClosestNodeLocation));
			if (bHasPlayerLocation)
			{
				UE_LOG(LogPathFinder, Log, TEXT("    closestReachedNodeFromPlayer=%s"), *FormatPathFinderBearingFromTo(PlayerLocation, Result.ClosestNodeLocation));
			}
		}

		if (Result.Status != EPathFinderLegStatus::Reachable || !Result.bIndependentSearchFoundPath)
		{
			UE_LOG(LogPathFinder, Log, TEXT("    suspectNeighborhoods: logging from/to/closest nodes because status=%s independentFound=%s"),
				*FPathFinderRouteBuilder::LegStatusToString(Result.Status),
				*FormatPathFinderBool(Result.bIndependentSearchFoundPath));
			LogPathFinderNodeNeighborhood(PathNetwork, Context.VehiclePathPreset, Result.FromNodeIndex, TEXT("fromNode"), VehicleLocation, PlayerLocation, bHasPlayerLocation, 12);
			LogPathFinderNodeNeighborhood(PathNetwork, Context.VehiclePathPreset, Result.ToNodeIndex, TEXT("toNode"), VehicleLocation, PlayerLocation, bHasPlayerLocation, 12);
			if (Result.bHasClosestNodeLocation && PathNetwork != nullptr)
			{
				LogPathFinderNodeNeighborhood(PathNetwork, Context.VehiclePathPreset, PathNetwork->FindPathNodeIndexByGuid(Result.ClosestNodeGuid), TEXT("closestReachedNode"), VehicleLocation, PlayerLocation, bHasPlayerLocation, 12);
			}
		}

		LogPathFinderGuidPath(PathNetwork, TEXT("engine"), Result.PathNodeGuids);
		LogPathFinderGuidPath(PathNetwork, TEXT("independent"), Result.IndependentPathNodeGuids);
		LogPathFinderSampleDetails(Result, TNumericLimits<int32>::Max());
	}

}

void FPathFinderConsoleCommands::FindVehiclesForStation(const TArray<FString>& Args) const
{
	FindVehiclesForStationInternal(Args, nullptr);
}

void FPathFinderConsoleCommands::FindVehiclesForStationWithOutput(const TArray<FString>& Args, FOutputDevice& Output) const
{
	FindVehiclesForStationInternal(Args, &Output);
}

void FPathFinderConsoleCommands::FindVehiclesForStationInternal(const TArray<FString>& Args, FOutputDevice* Output) const
{
	const FString Query = FString::Join(Args, TEXT(" ")).TrimStartAndEnd();
	if (Query.IsEmpty())
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Usage: PathFinder.FindVehiclesForStation <station name|guid>"));
		WritePathFinderConsoleLine(Output, TEXT("Usage: PathFinder.FindVehiclesForStation <station name|guid>"));
		return;
	}

	UWorld* World = ResolveWorld();
	AFGVehicleSubsystem* VehicleSubsystem = AFGVehicleSubsystem::Get(World);
	if (VehicleSubsystem == nullptr)
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Cannot find vehicles for station: vehicle subsystem unavailable."));
		WritePathFinderConsoleLine(Output, TEXT("Cannot find vehicles for station: vehicle subsystem unavailable."));
		return;
	}

	FGuid QueryGuid;
	const bool bQueryIsGuid = FGuid::Parse(Query, QueryGuid);
	TMap<FGuid, AFGDockingStationIdentifier*> MatchingStationsByGuid;

	const TArray<AFGDockingStationIdentifier*>& DockingStations = VehicleSubsystem->GetDockingStations();
	for (AFGDockingStationIdentifier* StationIdentifier : DockingStations)
	{
		if (!DoesPathFinderStationMatchQuery(StationIdentifier, Query, bQueryIsGuid, QueryGuid))
		{
			continue;
		}

		MatchingStationsByGuid.Add(StationIdentifier->GetPathNodeGUID(), StationIdentifier);
	}

	const TArray<AFGWheeledVehicleIdentifier*>& VehicleIdentifiers = VehicleSubsystem->GetAllVehicles();
	FVector ViewerLocation = FVector::ZeroVector;
	const bool bHasViewerLocation = TryResolvePathFinderViewerLocation(World, ViewerLocation);
	UE_LOG(LogPathFinder, Log, TEXT("FindVehiclesForStation query=\"%s\" queryIsGuid=%s stationMatches=%d vehiclesScanned=%d"),
		*Query,
		*FormatPathFinderBool(bQueryIsGuid),
		MatchingStationsByGuid.Num(),
		VehicleIdentifiers.Num());
	WritePathFinderConsoleLine(Output, FString::Printf(TEXT("FindVehiclesForStation query=\"%s\" queryIsGuid=%s stationMatches=%d vehiclesScanned=%d"),
		*Query,
		*FormatPathFinderBool(bQueryIsGuid),
		MatchingStationsByGuid.Num(),
		VehicleIdentifiers.Num()));
	if (bHasViewerLocation)
	{
		WritePathFinderConsoleLine(Output, FString::Printf(TEXT("  fromYouReference mapMeters=(%s)"), *FormatPathFinderMapMeters(ViewerLocation)));
	}

	for (const TPair<FGuid, AFGDockingStationIdentifier*>& StationMatch : MatchingStationsByGuid)
	{
		AFGDockingStationIdentifier* StationIdentifier = StationMatch.Value;
		UFGVehiclePathNetwork* PathNetwork = VehicleSubsystem->FindNetworkByPathNodeGuid(StationMatch.Key);
		const int32 NodeIndex = PathNetwork != nullptr ? PathNetwork->FindPathNodeIndexByGuid(StationMatch.Key) : INDEX_NONE;
		FVector StationNodeLocation = FVector::ZeroVector;
		const bool bHasStationNodeLocation = TryResolvePathFinderNodeLocation(VehicleSubsystem, StationMatch.Key, StationNodeLocation);
		const FString StationNodeLocationText = bHasStationNodeLocation ? FormatPathFinderMapMeters(StationNodeLocation) : TEXT("unknown");
		const FString StationActorLocationText = StationIdentifier != nullptr ? FormatPathFinderMapMeters(StationIdentifier->GetRealActorLocation()) : TEXT("unknown");

		UE_LOG(LogPathFinder, Log, TEXT("  matchedStation name=\"%s\" guid=%s status=%s network=%d nodeIndex=%d nodeMapMeters=(%s) actorMapMeters=(%s)"),
			StationIdentifier != nullptr ? *StationIdentifier->GetStationName().ToString() : TEXT("<missing>"),
			*StationMatch.Key.ToString(),
			StationIdentifier != nullptr ? *FormatPathFinderDockingStationStatus(StationIdentifier->GetStationStatus()) : TEXT("n/a"),
			PathNetwork != nullptr ? PathNetwork->GetNetworkID() : INDEX_NONE,
			NodeIndex,
			*StationNodeLocationText,
			*StationActorLocationText);
		WritePathFinderConsoleLine(Output, FString::Printf(TEXT("  matchedStation name=\"%s\" guid=%s network=%d nodeIndex=%d nodeMapMeters=(%s)"),
			StationIdentifier != nullptr ? *StationIdentifier->GetStationName().ToString() : TEXT("<missing>"),
			*StationMatch.Key.ToString(),
			PathNetwork != nullptr ? PathNetwork->GetNetworkID() : INDEX_NONE,
			NodeIndex,
			*StationNodeLocationText));
	}

	if (MatchingStationsByGuid.Num() == 0 && !bQueryIsGuid)
	{
		UE_LOG(LogPathFinder, Warning, TEXT("No docking stations matched query=\"%s\". Try a fuller station name or a route endpoint GUID from PathFinder.ShowRoute."), *Query);
		WritePathFinderConsoleLine(Output, FString::Printf(TEXT("No docking stations matched query=\"%s\". Try a fuller station name or a route endpoint GUID from PathFinder.ShowRoute."), *Query));
		return;
	}

	int32 MatchingVehicleCount = 0;
	int32 MatchingRouteEndpointCount = 0;
	for (AFGWheeledVehicleIdentifier* VehicleIdentifier : VehicleIdentifiers)
	{
		if (VehicleIdentifier == nullptr)
		{
			continue;
		}

		const TArray<FGuid>& VehicleRoute = VehicleIdentifier->GetVehicleRoute();
		TMap<FGuid, TArray<int32>> RouteIndicesByMatchedStationGuid;
		TMap<FGuid, AFGDockingStationIdentifier*> RouteStationByGuid;

		for (int32 RouteIndex = 0; RouteIndex < VehicleRoute.Num(); ++RouteIndex)
		{
			const FGuid& RoutePathNodeGuid = VehicleRoute[RouteIndex];
			AFGDockingStationIdentifier* StationIdentifier = VehicleSubsystem->FindDockingStationIdentifierForPathNodeGuid(RoutePathNodeGuid);
			const bool bMatchesKnownStation = StationIdentifier != nullptr && MatchingStationsByGuid.Contains(StationIdentifier->GetPathNodeGUID());
			const bool bMatchesRawGuid = bQueryIsGuid && RoutePathNodeGuid == QueryGuid;
			if (!bMatchesKnownStation && !bMatchesRawGuid)
			{
				continue;
			}

			const FGuid MatchedStationGuid = StationIdentifier != nullptr ? StationIdentifier->GetPathNodeGUID() : RoutePathNodeGuid;
			RouteIndicesByMatchedStationGuid.FindOrAdd(MatchedStationGuid).Add(RouteIndex);
			RouteStationByGuid.Add(MatchedStationGuid, StationIdentifier);
			++MatchingRouteEndpointCount;
		}

		if (RouteIndicesByMatchedStationGuid.Num() == 0)
		{
			continue;
		}

		++MatchingVehicleCount;
		for (const TPair<FGuid, TArray<int32>>& RouteMatch : RouteIndicesByMatchedStationGuid)
		{
			AFGDockingStationIdentifier* StationIdentifier = nullptr;
			if (AFGDockingStationIdentifier** StationIdentifierPtr = RouteStationByGuid.Find(RouteMatch.Key))
			{
				StationIdentifier = *StationIdentifierPtr;
			}

			FVector StationLocation = FVector::ZeroVector;
			const bool bHasStationLocation = TryResolvePathFinderNodeLocation(VehicleSubsystem, RouteMatch.Key, StationLocation);
			AFGWheeledVehicle* OwnerVehicle = VehicleIdentifier->GetOwnerVehicle();
			const bool bHasOwnerVehicle = OwnerVehicle != nullptr;
			const FVector VehicleLocation = bHasOwnerVehicle ? OwnerVehicle->GetActorLocation() : VehicleIdentifier->GetRealActorLocation();
			const FString BearingText = bHasStationLocation ? FormatPathFinderBearingFromTo(StationLocation, VehicleLocation) : TEXT("unavailable");
			const FString PlayerBearingText = bHasViewerLocation ? FormatPathFinderBearingFromTo(ViewerLocation, VehicleLocation) : TEXT("unavailable");
			const FString DeltaText = bHasStationLocation ? FormatPathFinderWorldDeltaMeters(StationLocation, VehicleLocation) : TEXT("unavailable");
			UFGVehiclePathNetwork* PathNetwork = VehicleSubsystem->FindNetworkByPathNodeGuid(RouteMatch.Key);
			const int32 NodeIndex = PathNetwork != nullptr ? PathNetwork->FindPathNodeIndexByGuid(RouteMatch.Key) : INDEX_NONE;

			UE_LOG(LogPathFinder, Log, TEXT("  vehicle=\"%s\" class=%s station=\"%s\" stationGuid=%s routeIndices=%s routeEndpoints=%d autopilot=%s autopilotError=%s currentlyDocking=%s currentTargetWaypointIndex=%d currentSegment=%s -> %s ownerReplicated=%s vehicleMapMeters=(%s) stationMapMeters=(%s) stationNetwork=%d stationNodeIndex=%d fromYou=%s fromStation=%s worldDelta=(%s)"),
				*FormatPathFinderDisplayName(VehicleIdentifier->GetVehicleName(), VehicleIdentifier),
				*GetNameSafe(VehicleIdentifier->GetVehicleClass()),
				StationIdentifier != nullptr ? *StationIdentifier->GetStationName().ToString() : TEXT("<missing station>"),
				*RouteMatch.Key.ToString(),
				*FormatPathFinderIndexList(RouteMatch.Value),
				VehicleRoute.Num(),
				*FormatPathFinderBool(VehicleIdentifier->IsAutopilotEnabled()),
				*FormatPathFinderAutopilotErrorStatus(VehicleIdentifier->GetAutopilotErrorStatus()),
				*FormatPathFinderBool(VehicleIdentifier->IsCurrentlyDocking()),
				VehicleIdentifier->GetCurrentTargetWaypointIndex(),
				*VehicleIdentifier->GetCurrentFromPathNodeGUID().ToString(),
				*VehicleIdentifier->GetCurrentToPathNodeGUID().ToString(),
				*FormatPathFinderBool(bHasOwnerVehicle),
				*FormatPathFinderMapMeters(VehicleLocation),
				bHasStationLocation ? *FormatPathFinderMapMeters(StationLocation) : TEXT("unknown"),
				PathNetwork != nullptr ? PathNetwork->GetNetworkID() : INDEX_NONE,
				NodeIndex,
				*PlayerBearingText,
				*BearingText,
				*DeltaText);
			WritePathFinderConsoleLine(Output, FString::Printf(TEXT("  vehicle=\"%s\" station=\"%s\" routeIndices=%s vehicleMapMeters=(%s) fromYou=%s fromStation=%s"),
				*FormatPathFinderDisplayName(VehicleIdentifier->GetVehicleName(), VehicleIdentifier),
				StationIdentifier != nullptr ? *StationIdentifier->GetStationName().ToString() : TEXT("<missing station>"),
				*FormatPathFinderIndexList(RouteMatch.Value),
				*FormatPathFinderMapMeters(VehicleLocation),
				*PlayerBearingText,
				*BearingText));
		}
	}

	UE_LOG(LogPathFinder, Log, TEXT("FindVehiclesForStation summary: query=\"%s\" matchingVehicles=%d matchingRouteEndpoints=%d"),
		*Query,
		MatchingVehicleCount,
		MatchingRouteEndpointCount);
	WritePathFinderConsoleLine(Output, FString::Printf(TEXT("FindVehiclesForStation summary: query=\"%s\" matchingVehicles=%d matchingRouteEndpoints=%d"),
		*Query,
		MatchingVehicleCount,
		MatchingRouteEndpointCount));

	if (MatchingVehicleCount == 0)
	{
		UE_LOG(LogPathFinder, Warning, TEXT("No vehicles have a route endpoint matching query=\"%s\"."), *Query);
		WritePathFinderConsoleLine(Output, FString::Printf(TEXT("No vehicles have a route endpoint matching query=\"%s\"."), *Query));
	}
}

void FPathFinderConsoleCommands::TrackStationVehicles(const TArray<FString>& Args)
{
	TrackStationVehiclesInternal(Args, nullptr);
}

void FPathFinderConsoleCommands::TrackStationVehiclesWithOutput(const TArray<FString>& Args, FOutputDevice& Output)
{
	TrackStationVehiclesInternal(Args, &Output);
}

void FPathFinderConsoleCommands::TrackStationVehiclesInternal(const TArray<FString>& Args, FOutputDevice* Output)
{
	const FString Query = FString::Join(Args, TEXT(" ")).TrimStartAndEnd();
	if (Query.IsEmpty())
	{
		if (bStationVehicleTrackerEnabled)
		{
			UE_LOG(LogPathFinder, Log, TEXT("PathFinder station vehicle tracker is active: query=\"%s\""), *StationVehicleTrackerQuery);
			WritePathFinderConsoleLine(Output, FString::Printf(TEXT("PathFinder station vehicle tracker is active: query=\"%s\""), *StationVehicleTrackerQuery));
			return;
		}

		UE_LOG(LogPathFinder, Warning, TEXT("Usage: PathFinder.TrackStationVehicles <station name|guid|off>"));
		WritePathFinderConsoleLine(Output, TEXT("Usage: PathFinder.TrackStationVehicles <station name|guid|off>"));
		return;
	}

	if (Query.Equals(TEXT("off"), ESearchCase::IgnoreCase) ||
		Query.Equals(TEXT("stop"), ESearchCase::IgnoreCase) ||
		Query.Equals(TEXT("0"), ESearchCase::IgnoreCase) ||
		Query.Equals(TEXT("false"), ESearchCase::IgnoreCase))
	{
		StopStationVehicleTracker();
		UE_LOG(LogPathFinder, Log, TEXT("PathFinder station vehicle tracker stopped."));
		UE_LOG(LogPathFinder, Log, TEXT("ok"));
		WritePathFinderConsoleLine(Output, TEXT("PathFinder station vehicle tracker stopped."));
		WritePathFinderConsoleLine(Output, TEXT("ok"));
		return;
	}

	StationVehicleTrackerQuery = Query;
	StationVehicleTrackerLastMatchCount = INDEX_NONE;
	bStationVehicleTrackerEnabled = true;

	if (!StationVehicleTrackerTickerHandle.IsValid())
	{
		StationVehicleTrackerTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FPathFinderConsoleCommands::TickStationVehicleTracker),
			0.25f);
	}

	UE_LOG(LogPathFinder, Log, TEXT("PathFinder station vehicle tracker started: query=\"%s\""), *StationVehicleTrackerQuery);
	UE_LOG(LogPathFinder, Log, TEXT("ok"));
	WritePathFinderConsoleLine(Output, FString::Printf(TEXT("PathFinder station vehicle tracker started: query=\"%s\""), *StationVehicleTrackerQuery));
	WritePathFinderConsoleLine(Output, TEXT("Run PathFinder.FindVehiclesForStation with the same query for an immediate console snapshot."));
	WritePathFinderConsoleLine(Output, TEXT("ok"));
	TickStationVehicleTracker(0.0f);
}

bool FPathFinderConsoleCommands::TickStationVehicleTracker(float DeltaSeconds)
{
	if (!bStationVehicleTrackerEnabled)
	{
		return false;
	}

	UWorld* World = ResolveWorld();
	AFGVehicleSubsystem* VehicleSubsystem = AFGVehicleSubsystem::Get(World);
	if (World == nullptr || VehicleSubsystem == nullptr)
	{
		return true;
	}

	FGuid QueryGuid;
	const bool bQueryIsGuid = FGuid::Parse(StationVehicleTrackerQuery, QueryGuid);
	TMap<FGuid, AFGDockingStationIdentifier*> MatchingStationsByGuid;

	const TArray<AFGDockingStationIdentifier*>& DockingStations = VehicleSubsystem->GetDockingStations();
	for (AFGDockingStationIdentifier* StationIdentifier : DockingStations)
	{
		if (!DoesPathFinderStationMatchQuery(StationIdentifier, StationVehicleTrackerQuery, bQueryIsGuid, QueryGuid))
		{
			continue;
		}

		MatchingStationsByGuid.Add(StationIdentifier->GetPathNodeGUID(), StationIdentifier);
	}

	int32 MatchingVehicleCount = 0;
	int32 MatchingRouteEndpointCount = 0;
	FVector ViewerLocation = FVector::ZeroVector;
	const bool bHasViewerLocation = TryResolvePathFinderViewerLocation(World, ViewerLocation);
	if (MatchingStationsByGuid.Num() == 0 && !bQueryIsGuid)
	{
		return true;
	}

	const TArray<AFGWheeledVehicleIdentifier*>& VehicleIdentifiers = VehicleSubsystem->GetAllVehicles();
	for (AFGWheeledVehicleIdentifier* VehicleIdentifier : VehicleIdentifiers)
	{
		if (VehicleIdentifier == nullptr)
		{
			continue;
		}

		const TArray<FGuid>& VehicleRoute = VehicleIdentifier->GetVehicleRoute();
		for (int32 RouteIndex = 0; RouteIndex < VehicleRoute.Num(); ++RouteIndex)
		{
			const FGuid& RoutePathNodeGuid = VehicleRoute[RouteIndex];
			AFGDockingStationIdentifier* StationIdentifier = VehicleSubsystem->FindDockingStationIdentifierForPathNodeGuid(RoutePathNodeGuid);
			const bool bMatchesKnownStation = StationIdentifier != nullptr && MatchingStationsByGuid.Contains(StationIdentifier->GetPathNodeGUID());
			const bool bMatchesQueryStation = StationIdentifier != nullptr && DoesPathFinderStationMatchQuery(StationIdentifier, StationVehicleTrackerQuery, bQueryIsGuid, QueryGuid);
			const bool bMatchesRawGuid = bQueryIsGuid && RoutePathNodeGuid == QueryGuid;
			if (!bMatchesKnownStation && !bMatchesQueryStation && !bMatchesRawGuid)
			{
				continue;
			}

			FVector StationLocation = FVector::ZeroVector;
			if (!TryResolvePathFinderNodeLocation(VehicleSubsystem, RoutePathNodeGuid, StationLocation))
			{
				continue;
			}

			AFGWheeledVehicle* OwnerVehicle = VehicleIdentifier->GetOwnerVehicle();
			const bool bHasOwnerVehicle = OwnerVehicle != nullptr;
			const FVector VehicleLocation = bHasOwnerVehicle ? OwnerVehicle->GetActorLocation() : VehicleIdentifier->GetRealActorLocation();

			if (bHasViewerLocation)
			{
				DrawPathFinderRuntimeLine(World, ViewerLocation + FVector(0.0, 0.0, 120.0), VehicleLocation + FVector(0.0, 0.0, 180.0), FLinearColor(0.0f, 1.0f, 1.0f, 1.0f), 14.0f, 0.45f);
			}
			DrawPathFinderRuntimeLine(World, StationLocation + FVector(0.0, 0.0, 180.0), VehicleLocation + FVector(0.0, 0.0, 160.0), FLinearColor(0.0f, 1.0f, 0.1f, 1.0f), 6.0f, 0.45f);

			++MatchingVehicleCount;
			++MatchingRouteEndpointCount;
			break;
		}
	}

	if (StationVehicleTrackerLastMatchCount != MatchingVehicleCount)
	{
		UE_LOG(LogPathFinder, Log, TEXT("PathFinder station vehicle tracker query=\"%s\" stationMatches=%d matchingVehicles=%d matchingRouteEndpoints=%d"),
			*StationVehicleTrackerQuery,
			MatchingStationsByGuid.Num(),
			MatchingVehicleCount,
			MatchingRouteEndpointCount);
		StationVehicleTrackerLastMatchCount = MatchingVehicleCount;
	}

	return true;
}

void FPathFinderConsoleCommands::StopStationVehicleTracker()
{
	bStationVehicleTrackerEnabled = false;
	StationVehicleTrackerQuery.Empty();
	StationVehicleTrackerLastMatchCount = INDEX_NONE;

	if (StationVehicleTrackerTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(StationVehicleTrackerTickerHandle);
		StationVehicleTrackerTickerHandle.Reset();
	}
}

void FPathFinderConsoleCommands::SetVehicleTracingWithOutput(const TArray<FString>& Args, FOutputDevice& Output)
{
	SetVehicleTracingInternal(Args, &Output);
}

void FPathFinderConsoleCommands::SetVehicleTracingInternal(const TArray<FString>& Args, FOutputDevice* Output)
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogPathFinder, Log, TEXT("Vehicle tracing overlay is %s."), bVehicleTracingEnabled ? TEXT("enabled") : TEXT("disabled"));
		WritePathFinderConsoleLine(Output, FString::Printf(TEXT("Vehicle tracing overlay is %s."), bVehicleTracingEnabled ? TEXT("enabled") : TEXT("disabled")));
		WriteVehicleTracingStatus(Output);
		return;
	}

	const bool bNetworkUsageRequested = Args[0].Equals(TEXT("network"), ESearchCase::IgnoreCase);
	bool bEnabled = bNetworkUsageRequested;
	if (!bNetworkUsageRequested && !TryParsePathFinderToggle(Args[0], bEnabled))
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Usage: PathFinder.VehicleTracing <on|off|network>"));
		WritePathFinderConsoleLine(Output, TEXT("Usage: PathFinder.VehicleTracing <on|off|network>"));
		return;
	}

	if (!bEnabled)
	{
		StopVehicleTracing();
		UE_LOG(LogPathFinder, Log, TEXT("PathFinder vehicle tracing overlay disabled."));
		UE_LOG(LogPathFinder, Log, TEXT("ok"));
		WritePathFinderConsoleLine(Output, TEXT("PathFinder vehicle tracing overlay disabled."));
		WritePathFinderConsoleLine(Output, TEXT("ok"));
		return;
	}

	if (bVehicleTracingEnabled && bNetworkUsageTracingEnabled != bNetworkUsageRequested)
	{
		VehicleTracingLoadWindowsBySegmentKey.Empty();
		VehicleTracingRouteCache.Clear();
		RouteOverlayRenderer.Clear();
	}

	bool bStartedTicker = false;
	if (!VehicleTracingTickerHandle.IsValid())
	{
		VehicleTracingTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateRaw(this, &FPathFinderConsoleCommands::TickVehicleTracing),
			0.5f);
		bStartedTicker = true;
	}

	bVehicleTracingEnabled = true;
	bNetworkUsageTracingEnabled = bNetworkUsageRequested;
	UWorld* World = ResolveWorld();
	AFGVehicleSubsystem* VehicleSubsystem = World != nullptr ? AFGVehicleSubsystem::Get(World) : nullptr;
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	const int32 VehicleCount = VehicleSubsystem != nullptr ? VehicleSubsystem->GetAllVehicles().Num() : 0;
	const FString MaterialProbeLine = RouteOverlayRenderer.DescribeMaterialProbe();
	const FString PrerequisiteLine = FString::Printf(
		TEXT("PathFinder vehicle tracing prerequisites: world=%s player=%s vehicleSubsystem=%s vehicles=%d"),
		World != nullptr ? TEXT("ok") : TEXT("missing"),
		PlayerController != nullptr ? TEXT("ok") : TEXT("missing"),
		VehicleSubsystem != nullptr ? TEXT("ok") : TEXT("missing"),
		VehicleCount);
	UE_LOG(LogPathFinder, Log, TEXT("PathFinder vehicle tracing init: ticker=%s intervalMs=500"), bStartedTicker ? TEXT("started") : TEXT("already-active"));
	UE_LOG(LogPathFinder, Log, TEXT("%s"), *MaterialProbeLine);
	UE_LOG(LogPathFinder, Log, TEXT("%s"), *PrerequisiteLine);
	UE_LOG(LogPathFinder, Log, TEXT("PathFinder vehicle tracing overlay enabled. Sampling every 500 ms over a 60 second rolling window."));
	UE_LOG(LogPathFinder, Log, TEXT("ok"));
	WritePathFinderConsoleLine(Output, FString::Printf(TEXT("PathFinder vehicle tracing init: ticker=%s intervalMs=500"), bStartedTicker ? TEXT("started") : TEXT("already-active")));
	WritePathFinderConsoleLine(Output, MaterialProbeLine);
	WritePathFinderConsoleLine(Output, PrerequisiteLine);
	WriteVehicleTracingStatus(Output);
	WritePathFinderConsoleLine(Output, TEXT("PathFinder vehicle tracing overlay enabled. Sampling every 500 ms over a 60 second rolling window."));
	WritePathFinderConsoleLine(Output, TEXT("ok"));
}

void FPathFinderConsoleCommands::WriteVehicleTracingStatus(FOutputDevice* Output) const
{
	if (!bVehicleTracingHasTicked)
	{
		WritePathFinderConsoleLine(Output, TEXT("PathFinder vehicle tracing last tick: not-yet-run"));
		return;
	}

	WritePathFinderConsoleLine(Output, FString::Printf(
		TEXT("PathFinder vehicle tracing last tick: world=%s player=%s vehicleSubsystem=%s vehiclesScanned=%d validRoutes=%d routeLegs=%d paintSamples=%d candidateSegments=%d drawAttempts=%d drawnSegments=%d failedDraws=%d activeRenderStates=%d"),
		bLastVehicleTracingHadWorld ? TEXT("ok") : TEXT("missing"),
		bLastVehicleTracingHadPlayer ? TEXT("ok") : TEXT("missing"),
		bLastVehicleTracingHadVehicleSubsystem ? TEXT("ok") : TEXT("missing"),
		LastVehicleTracingVehiclesScanned,
		LastVehicleTracingValidRoutes,
		LastVehicleTracingRouteLegs,
		LastVehicleTracingPaintSamples,
		LastVehicleTracingCandidateSegments,
		LastVehicleTracingDrawAttempts,
		LastVehicleTracingDrawnSegments,
		LastVehicleTracingFailedDraws,
		LastVehicleTracingActiveRenderStates));
}

void FPathFinderConsoleCommands::ShowNearestTrafficLabelWithOutput(FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	if (PlayerPawn == nullptr)
	{
		WritePathFinderConsoleLine(&Output, TEXT("trafficLabel unavailable: player pawn missing"));
		return;
	}

	FPathFinderRouteOverlayRenderer::FLabelGeometry LabelGeometry;
	if (!RouteOverlayRenderer.GetNearestLabelGeometry(PlayerPawn->GetActorLocation(), LabelGeometry))
	{
		WritePathFinderConsoleLine(&Output, TEXT("trafficLabel unavailable: no rendered route labels"));
		return;
	}

	const double FlatDot = FMath::Abs(FVector::DotProduct(
		LabelGeometry.TextPlaneNormal.GetSafeNormal(),
		FVector::UpVector));
	const double ParallelDot = FMath::Abs(FVector::DotProduct(
		LabelGeometry.TextDirection.GetSafeNormal(),
		LabelGeometry.StripDirection.GetSafeNormal()));
	WritePathFinderConsoleLine(&Output, FString::Printf(
		TEXT("trafficLabel segment=%s distanceCm=%.3f location=%s rotation=%s planeNormal=%s textDirection=%s stripDirection=%s flatDot=%.6f parallelDot=%.6f flat=%s parallel=%s"),
		*LabelGeometry.SegmentKey,
		LabelGeometry.DistanceCentimeters,
		*LabelGeometry.Location.ToCompactString(),
		*LabelGeometry.Rotation.ToCompactString(),
		*LabelGeometry.TextPlaneNormal.ToCompactString(),
		*LabelGeometry.TextDirection.ToCompactString(),
		*LabelGeometry.StripDirection.ToCompactString(),
		FlatDot,
		ParallelDot,
		FlatDot >= 0.999 ? TEXT("true") : TEXT("false"),
		ParallelDot >= 0.999 ? TEXT("true") : TEXT("false")));
}

void FPathFinderConsoleCommands::MoveToNearestTrafficLabelWithOutput(FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	if (PlayerPawn == nullptr)
	{
		WritePathFinderConsoleLine(&Output, TEXT("trafficLabel move failed: player pawn missing"));
		return;
	}

	FPathFinderRouteOverlayRenderer::FLabelGeometry LabelGeometry;
	if (!RouteOverlayRenderer.GetNearestLabelGeometry(PlayerPawn->GetActorLocation(), LabelGeometry))
	{
		WritePathFinderConsoleLine(&Output, TEXT("trafficLabel move failed: no rendered route labels"));
		return;
	}

	const FVector ViewLocation = LabelGeometry.Location
		- (LabelGeometry.StripDirection.GetSafeNormal() * 500.0)
		+ (FVector::UpVector * 350.0);
	const FRotator ViewRotation = (LabelGeometry.Location - ViewLocation).Rotation();
	PlayerPawn->SetActorLocationAndRotation(
		ViewLocation,
		ViewRotation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics);
	if (PlayerController != nullptr)
	{
		PlayerController->SetControlRotation(ViewRotation);
	}

	WritePathFinderConsoleLine(&Output, FString::Printf(
		TEXT("trafficLabel player moved segment=%s location=%s rotation=%s"),
		*LabelGeometry.SegmentKey,
		*ViewLocation.ToCompactString(),
		*ViewRotation.ToCompactString()));
}

bool FPathFinderConsoleCommands::TickVehicleTracing(float DeltaSeconds)
{
	if (!bVehicleTracingEnabled)
	{
		return false;
	}

	UWorld* World = ResolveWorld();
	if (World == nullptr)
	{
		bVehicleTracingHasTicked = true;
		bLastVehicleTracingHadWorld = false;
		bLastVehicleTracingHadPlayer = false;
		bLastVehicleTracingHadVehicleSubsystem = false;
		LastVehicleTracingVehiclesScanned = 0;
		LastVehicleTracingValidRoutes = 0;
		LastVehicleTracingRouteLegs = 0;
		LastVehicleTracingPaintSamples = 0;
		LastVehicleTracingCandidateSegments = 0;
		LastVehicleTracingDrawAttempts = 0;
		LastVehicleTracingDrawnSegments = 0;
		LastVehicleTracingFailedDraws = 0;
		VehicleTracingLoadWindowsBySegmentKey.Empty();
		VehicleTracingRouteCache.Clear();
		RouteOverlayRenderer.Clear();
		LastVehicleTracingActiveRenderStates = RouteOverlayRenderer.GetRenderStateCount();
		return true;
	}

	AFGVehicleSubsystem* VehicleSubsystem = AFGVehicleSubsystem::Get(World);
	if (VehicleSubsystem == nullptr)
	{
		bVehicleTracingHasTicked = true;
		bLastVehicleTracingHadWorld = true;
		bLastVehicleTracingHadPlayer = World->GetFirstPlayerController() != nullptr;
		bLastVehicleTracingHadVehicleSubsystem = false;
		LastVehicleTracingVehiclesScanned = 0;
		LastVehicleTracingValidRoutes = 0;
		LastVehicleTracingRouteLegs = 0;
		LastVehicleTracingPaintSamples = 0;
		LastVehicleTracingCandidateSegments = 0;
		LastVehicleTracingDrawAttempts = 0;
		LastVehicleTracingDrawnSegments = 0;
		LastVehicleTracingFailedDraws = 0;
		VehicleTracingLoadWindowsBySegmentKey.Empty();
		VehicleTracingRouteCache.Clear();
		RouteOverlayRenderer.Clear();
		LastVehicleTracingActiveRenderStates = RouteOverlayRenderer.GetRenderStateCount();
		return true;
	}

	APlayerController* PlayerController = World->GetFirstPlayerController();
	const TArray<AFGWheeledVehicleIdentifier*>& VehicleIdentifiers = VehicleSubsystem->GetAllVehicles();
	const FPathFinderVehicleRouteCacheUpdateResult RouteCacheResult = VehicleTracingRouteCache.Update(World, PlayerController, VehicleIdentifiers);
	const TMap<FString, int32>& RouteSegmentReferenceCounts = VehicleTracingRouteCache.GetRouteSegmentReferenceCounts();
	const TMap<FString, FPathFinderPaintSample>& PaintSamplesBySegmentKey = VehicleTracingRouteCache.GetPaintSamplesBySegmentKey();

	if (bNetworkUsageTracingEnabled)
	{
		TSet<FString> CurrentRouteSegmentKeys;
		int32 DrawAttempts = 0;
		int32 DrawnSegments = 0;
		for (TActorIterator<AFGVehiclePathSegment> SegmentIterator(World); SegmentIterator; ++SegmentIterator)
		{
			AFGVehiclePathSegment* SegmentActor = *SegmentIterator;
			AFGVehiclePathNode* EndNode = SegmentActor != nullptr ? SegmentActor->GetEndNode() : nullptr;
			if (SegmentActor == nullptr || SegmentActor->IsActorBeingDestroyed() || EndNode == nullptr)
			{
				continue;
			}

			const FString SegmentKey = FPathFinderVehicleRouteCache::MakeSegmentKey(
				SegmentActor->GetStartPathNodeGuid(),
				EndNode->GetPathNodeGUID());
			const bool bAssigned = RouteSegmentReferenceCounts.Contains(SegmentKey);
			CurrentRouteSegmentKeys.Add(SegmentKey);
			++DrawAttempts;
			if (RouteOverlayRenderer.DrawPathSegment(
				SegmentActor,
				SegmentKey,
				bAssigned ? PathFinderNetworkAssignedColor : PathFinderNetworkUnusedColor,
				true,
				FString()))
			{
				++DrawnSegments;
			}
		}

		VehicleTracingLoadWindowsBySegmentKey.Empty();
		RouteOverlayRenderer.RemoveStaleSegments(CurrentRouteSegmentKeys);
		bVehicleTracingHasTicked = true;
		bLastVehicleTracingHadWorld = true;
		bLastVehicleTracingHadPlayer = PlayerController != nullptr;
		bLastVehicleTracingHadVehicleSubsystem = true;
		LastVehicleTracingVehiclesScanned = RouteCacheResult.VehiclesScanned;
		LastVehicleTracingValidRoutes = RouteCacheResult.ValidRoutes;
		LastVehicleTracingRouteLegs = RouteCacheResult.RouteLegs;
		LastVehicleTracingPaintSamples = RouteCacheResult.PaintSampleCount;
		LastVehicleTracingCandidateSegments = CurrentRouteSegmentKeys.Num();
		LastVehicleTracingDrawAttempts = DrawAttempts;
		LastVehicleTracingDrawnSegments = DrawnSegments;
		LastVehicleTracingFailedDraws = DrawAttempts - DrawnSegments;
		LastVehicleTracingActiveRenderStates = RouteOverlayRenderer.GetRenderStateCount();
		return true;
	}

	if (RouteSegmentReferenceCounts.Num() == 0)
	{
		VehicleTracingLoadWindowsBySegmentKey.Empty();
		RouteOverlayRenderer.Clear();
		bVehicleTracingHasTicked = true;
		bLastVehicleTracingHadWorld = true;
		bLastVehicleTracingHadPlayer = PlayerController != nullptr;
		bLastVehicleTracingHadVehicleSubsystem = true;
		LastVehicleTracingVehiclesScanned = RouteCacheResult.VehiclesScanned;
		LastVehicleTracingValidRoutes = RouteCacheResult.ValidRoutes;
		LastVehicleTracingRouteLegs = RouteCacheResult.RouteLegs;
		LastVehicleTracingPaintSamples = RouteCacheResult.PaintSampleCount;
		LastVehicleTracingCandidateSegments = 0;
		LastVehicleTracingDrawAttempts = 0;
		LastVehicleTracingDrawnSegments = 0;
		LastVehicleTracingFailedDraws = 0;
		LastVehicleTracingActiveRenderStates = RouteOverlayRenderer.GetRenderStateCount();
		return true;
	}

	TMap<FString, int32> CurrentVehicleCountBySegmentKey;
	TSet<FString> CurrentRouteSegmentKeys;
	for (const TPair<FString, int32>& RouteSegmentReferenceCountEntry : RouteSegmentReferenceCounts)
	{
		CurrentRouteSegmentKeys.Add(RouteSegmentReferenceCountEntry.Key);
		CurrentVehicleCountBySegmentKey.Add(RouteSegmentReferenceCountEntry.Key, 0);
	}

	for (AFGWheeledVehicleIdentifier* VehicleIdentifier : VehicleIdentifiers)
	{
		if (VehicleIdentifier == nullptr)
		{
			continue;
		}

		const FString SegmentKey = FPathFinderVehicleRouteCache::MakeSegmentKey(VehicleIdentifier->GetCurrentFromPathNodeGUID(), VehicleIdentifier->GetCurrentToPathNodeGUID());
		int32* VehicleCount = CurrentVehicleCountBySegmentKey.Find(SegmentKey);
		if (VehicleCount != nullptr)
		{
			++(*VehicleCount);
		}
	}

	TSet<FString> ChangedTrafficBucketSegmentKeys;
	for (const FString& SegmentKey : CurrentRouteSegmentKeys)
	{
		const int32 CurrentVehicleCount = CurrentVehicleCountBySegmentKey.FindRef(SegmentKey);
		FPathFinderTrafficLoadWindow& TrafficLoadWindow = VehicleTracingLoadWindowsBySegmentKey.FindOrAdd(SegmentKey);
		const FPathFinderTrafficLoadSampleResult SampleResult = TrafficLoadWindow.AddVehicleCountSample(CurrentVehicleCount);
		if (SampleResult.bLoadTextChanged)
		{
			RouteOverlayRenderer.QueueLabelUpdate(SegmentKey, TrafficLoadWindow.GetLoadText());
		}

		if (SampleResult.bTrafficBucketChanged)
		{
			ChangedTrafficBucketSegmentKeys.Add(SegmentKey);
		}
	}

	TArray<FString> StaleVehicleLoadKeys;
	for (const TPair<FString, FPathFinderTrafficLoadWindow>& TrafficLoadWindowEntry : VehicleTracingLoadWindowsBySegmentKey)
	{
		if (!CurrentRouteSegmentKeys.Contains(TrafficLoadWindowEntry.Key))
		{
			StaleVehicleLoadKeys.Add(TrafficLoadWindowEntry.Key);
		}
	}

	for (const FString& StaleVehicleLoadKey : StaleVehicleLoadKeys)
	{
		VehicleTracingLoadWindowsBySegmentKey.Remove(StaleVehicleLoadKey);
	}

	TSet<FString> DrawnRouteSegmentKeys;
	int32 DrawAttempts = 0;
	TSet<FString> RouteSegmentKeysNeedingDraw = VehicleTracingRouteCache.ConsumeDirtySegmentKeys();
	for (const FString& ChangedTrafficBucketSegmentKey : ChangedTrafficBucketSegmentKeys)
	{
		if (RouteSegmentKeysNeedingDraw.Contains(ChangedTrafficBucketSegmentKey))
		{
			continue;
		}

		const FPathFinderTrafficLoadWindow* TrafficLoadWindow = VehicleTracingLoadWindowsBySegmentKey.Find(ChangedTrafficBucketSegmentKey);
		const int32 Bucket = TrafficLoadWindow != nullptr ? TrafficLoadWindow->GetTrafficBucket() : 0;
		if (!RouteOverlayRenderer.UpdateSegmentColor(ChangedTrafficBucketSegmentKey, GetPathFinderTrafficColor(Bucket)))
		{
			RouteSegmentKeysNeedingDraw.Add(ChangedTrafficBucketSegmentKey);
		}
	}

	TSet<FString> FailedRouteSegmentKeys;
	for (const FString& SegmentKey : RouteSegmentKeysNeedingDraw)
	{
		const FPathFinderPaintSample* PaintSample = PaintSamplesBySegmentKey.Find(SegmentKey);
		if (PaintSample == nullptr)
		{
			continue;
		}

		const FPathFinderTrafficLoadWindow* TrafficLoadWindow = VehicleTracingLoadWindowsBySegmentKey.Find(SegmentKey);
		static const FString ZeroLoadText(TEXT("0.0/s"));
		const FString& LoadText = TrafficLoadWindow != nullptr ? TrafficLoadWindow->GetLoadText() : ZeroLoadText;
		const int32 Bucket = TrafficLoadWindow != nullptr ? TrafficLoadWindow->GetTrafficBucket() : 0;
		++DrawAttempts;
		if (RouteOverlayRenderer.DrawPaintSample(World, SegmentKey, *PaintSample, GetPathFinderTrafficColor(Bucket), true, LoadText))
		{
			DrawnRouteSegmentKeys.Add(SegmentKey);
		}
		else
		{
			FailedRouteSegmentKeys.Add(SegmentKey);
		}
	}

	for (const FString& FailedRouteSegmentKey : FailedRouteSegmentKeys)
	{
		VehicleTracingRouteCache.MarkSegmentDirty(FailedRouteSegmentKey);
	}

	RouteOverlayRenderer.FlushQueuedLabelUpdates();
	if (RouteCacheResult.bRouteMembershipChanged)
	{
		RouteOverlayRenderer.RemoveStaleSegments(CurrentRouteSegmentKeys);
	}

	bVehicleTracingHasTicked = true;
	bLastVehicleTracingHadWorld = true;
	bLastVehicleTracingHadPlayer = PlayerController != nullptr;
	bLastVehicleTracingHadVehicleSubsystem = true;
	LastVehicleTracingVehiclesScanned = RouteCacheResult.VehiclesScanned;
	LastVehicleTracingValidRoutes = RouteCacheResult.ValidRoutes;
	LastVehicleTracingRouteLegs = RouteCacheResult.RouteLegs;
	LastVehicleTracingPaintSamples = RouteCacheResult.PaintSampleCount;
	LastVehicleTracingCandidateSegments = PaintSamplesBySegmentKey.Num();
	LastVehicleTracingDrawAttempts = DrawAttempts;
	LastVehicleTracingDrawnSegments = DrawnRouteSegmentKeys.Num();
	LastVehicleTracingFailedDraws = DrawAttempts - DrawnRouteSegmentKeys.Num();
	LastVehicleTracingActiveRenderStates = RouteOverlayRenderer.GetRenderStateCount();

	return true;
}

void FPathFinderConsoleCommands::StopVehicleTracing()
{
	bVehicleTracingEnabled = false;
	bNetworkUsageTracingEnabled = false;
	bVehicleTracingHasTicked = false;
	bLastVehicleTracingHadWorld = false;
	bLastVehicleTracingHadPlayer = false;
	bLastVehicleTracingHadVehicleSubsystem = false;
	VehicleTracingLoadWindowsBySegmentKey.Empty();
	RouteOverlayRenderer.Clear();
	LastVehicleTracingVehiclesScanned = 0;
	LastVehicleTracingValidRoutes = 0;
	LastVehicleTracingRouteLegs = 0;
	LastVehicleTracingPaintSamples = 0;
	LastVehicleTracingCandidateSegments = 0;
	LastVehicleTracingDrawAttempts = 0;
	LastVehicleTracingDrawnSegments = 0;
	LastVehicleTracingFailedDraws = 0;
	LastVehicleTracingActiveRenderStates = 0;

	if (VehicleTracingTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(VehicleTracingTickerHandle);
		VehicleTracingTickerHandle.Reset();
	}
}

void FPathFinderConsoleCommands::TraceLeg(const TArray<FString>& Args) const
{
	if (Args.Num() < 1)
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Usage: PathFinder.TraceLeg <index>"));
		return;
	}

	int32 LegIndex = INDEX_NONE;
	if (!LexTryParseString(LegIndex, *Args[0]))
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Invalid leg index: %s"), *Args[0]);
		return;
	}

	const FPathFinderVehicleContext Context = FPathFinderVehicleContextResolver::Resolve(ResolveWorld());
	if (!Context.IsValid())
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Cannot trace leg: %s"), *Context.ToDebugString());
		return;
	}

	const TArray<FPathFinderRouteLeg> Legs = FPathFinderRouteBuilder::BuildLoopLegs(Context.VehicleIdentifier->GetVehicleRoute());
	if (!Legs.IsValidIndex(LegIndex))
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Leg index %d is outside the route leg range 0..%d."), LegIndex, Legs.Num() - 1);
		return;
	}

	const FPathFinderLegResult Result = FPathFinderTraceService::TraceLeg(Context, Legs[LegIndex]);
	const FVector VehicleLocation = Context.WheeledVehicle->GetVehicleLocation();
	const bool bHasPlayerLocation = Context.Pawn != nullptr;
	const FVector PlayerLocation = bHasPlayerLocation ? Context.Pawn->GetActorLocation() : FVector::ZeroVector;
	UE_LOG(LogPathFinder, Log, TEXT("Trace leg %d: %s, status=%s, pathNodes=%d, paintSamples=%d, detail=%s"),
		LegIndex,
		*Result.Leg.ToDebugString(),
		*FPathFinderRouteBuilder::LegStatusToString(Result.Status),
		Result.PathNodeGuids.Num(),
		Result.PaintSamples.Num(),
		*Result.Detail);
	UE_LOG(LogPathFinder, Log, TEXT("  from=(%s) to=(%s) independentSearchFound=%s visitedNodes=%d relaxedSegments=%d closestReached=(%s)"),
		*FormatPathFinderVector(Result.FromNodeLocation),
		*FormatPathFinderVector(Result.ToNodeLocation),
		*FormatPathFinderBool(Result.bIndependentSearchFoundPath),
		Result.SearchVisitedNodeCount,
		Result.SearchRelaxedSegmentCount,
		*FormatPathFinderVector(Result.ClosestNodeLocation));
	LogPathFinderReferenceLocation(TEXT("Vehicle"), VehicleLocation);
	if (bHasPlayerLocation)
	{
		LogPathFinderReferenceLocation(TEXT("Player/pawn"), PlayerLocation);
	}
	if (Result.bHasFromNodeLocation)
	{
		UE_LOG(LogPathFinder, Log, TEXT("  fromEndpointFromVehicle=%s"), *FormatPathFinderBearingFromTo(VehicleLocation, Result.FromNodeLocation));
		if (bHasPlayerLocation)
		{
			UE_LOG(LogPathFinder, Log, TEXT("  fromEndpointFromPlayer=%s"), *FormatPathFinderBearingFromTo(PlayerLocation, Result.FromNodeLocation));
		}
	}
	if (Result.bHasToNodeLocation)
	{
		UE_LOG(LogPathFinder, Log, TEXT("  toEndpointFromVehicle=%s"), *FormatPathFinderBearingFromTo(VehicleLocation, Result.ToNodeLocation));
		if (bHasPlayerLocation)
		{
			UE_LOG(LogPathFinder, Log, TEXT("  toEndpointFromPlayer=%s"), *FormatPathFinderBearingFromTo(PlayerLocation, Result.ToNodeLocation));
		}
	}
	if (Result.bHasClosestNodeLocation)
	{
		UE_LOG(LogPathFinder, Log, TEXT("  closestReachedNodeFromVehicle=%s"), *FormatPathFinderBearingFromTo(VehicleLocation, Result.ClosestNodeLocation));
		if (bHasPlayerLocation)
		{
			UE_LOG(LogPathFinder, Log, TEXT("  closestReachedNodeFromPlayer=%s"), *FormatPathFinderBearingFromTo(PlayerLocation, Result.ClosestNodeLocation));
		}
	}
	if (Result.Status != EPathFinderLegStatus::Reachable || !Result.bIndependentSearchFoundPath)
	{
		AFGVehicleSubsystem* VehicleSubsystem = AFGVehicleSubsystem::Get(Context.World);
		UFGVehiclePathNetwork* PathNetwork = VehicleSubsystem != nullptr ? VehicleSubsystem->FindNetworkByPathNodeGuid(Result.Leg.FromNodeGuid) : nullptr;
		UE_LOG(LogPathFinder, Log, TEXT("  suspectNeighborhoods: logging from/to/closest nodes because status=%s independentFound=%s"),
			*FPathFinderRouteBuilder::LegStatusToString(Result.Status),
			*FormatPathFinderBool(Result.bIndependentSearchFoundPath));
		LogPathFinderNodeNeighborhood(PathNetwork, Context.VehiclePathPreset, Result.FromNodeIndex, TEXT("fromNode"), VehicleLocation, PlayerLocation, bHasPlayerLocation, 12);
		LogPathFinderNodeNeighborhood(PathNetwork, Context.VehiclePathPreset, Result.ToNodeIndex, TEXT("toNode"), VehicleLocation, PlayerLocation, bHasPlayerLocation, 12);
		if (Result.bHasClosestNodeLocation && PathNetwork != nullptr)
		{
			LogPathFinderNodeNeighborhood(PathNetwork, Context.VehiclePathPreset, PathNetwork->FindPathNodeIndexByGuid(Result.ClosestNodeGuid), TEXT("closestReachedNode"), VehicleLocation, PlayerLocation, bHasPlayerLocation, 12);
		}
	}
}

void FPathFinderConsoleCommands::SetInputTrace(const TArray<FString>& Args)
{
	if (Args.Num() < 1)
	{
		const bool bInputTraceEnabled = InputTrace.IsValid() && InputTrace->IsEnabled();
		UE_LOG(LogPathFinder, Log, TEXT("Input trace is %s."), bInputTraceEnabled ? TEXT("enabled") : TEXT("disabled"));
		return;
	}

	int32 EnabledValue = 0;
	if (!LexTryParseString(EnabledValue, *Args[0]))
	{
		UE_LOG(LogPathFinder, Warning, TEXT("Usage: PathFinder.InputTrace <0|1>"));
		return;
	}

	if (!InputTrace.IsValid())
	{
		InputTrace = MakeUnique<FPathFinderInputTrace>();
	}

	InputTrace->SetEnabled(EnabledValue != 0);
	UE_LOG(LogPathFinder, Log, TEXT("ok"));
}

void FPathFinderConsoleCommands::ShowRouteMapStatusWithOutput(FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	if (World == nullptr)
	{
		WritePathFinderConsoleLine(&Output, TEXT("RouteMapStatus world=unavailable"));
		return;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	UPathFinderRouteMapSubsystem* RouteMapSubsystem = GameInstance != nullptr
		? GameInstance->GetSubsystem<UPathFinderRouteMapSubsystem>()
		: nullptr;
	if (RouteMapSubsystem == nullptr)
	{
		WritePathFinderConsoleLine(&Output, FString::Printf(TEXT("RouteMapStatus world=%s subsystem=unavailable"), *World->GetName()));
		return;
	}

	const FIntPoint RenderTargetSize = RouteMapSubsystem->GetRenderTargetSize();
	const FString RenderTargetText = RenderTargetSize == FIntPoint::ZeroValue
		? TEXT("absent")
		: FString::Printf(TEXT("%dx%d"), RenderTargetSize.X, RenderTargetSize.Y);
	WritePathFinderConsoleLine(&Output, FString::Printf(
		TEXT("RouteMapStatus world=%s visible=%s initialPopulationRequested=%s redrawPending=%s segments=%d points=%d pendingRefreshes=%d pendingRemovals=%d renderTarget=%s"),
		*World->GetName(),
		*FormatPathFinderBool(RouteMapSubsystem->IsRouteLayerVisible()),
		*FormatPathFinderBool(RouteMapSubsystem->IsInitialPopulationRequested()),
		*FormatPathFinderBool(RouteMapSubsystem->IsRedrawPending()),
		RouteMapSubsystem->GetRouteSegmentCount(),
		RouteMapSubsystem->GetRoutePointCount(),
		RouteMapSubsystem->GetPendingRefreshCount(),
		RouteMapSubsystem->GetPendingRemovalCount(),
		*RenderTargetText));

	int32 LayerWidgetCount = 0;
	for (TObjectIterator<UPathFinderRouteMapLayerWidget> LayerWidgetIterator; LayerWidgetIterator; ++LayerWidgetIterator)
	{
		UPathFinderRouteMapLayerWidget* LayerWidget = *LayerWidgetIterator;
		if (!IsValid(LayerWidget) || LayerWidget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || LayerWidget->GetWorld() != World)
		{
			continue;
		}

		++LayerWidgetCount;
		UPanelWidget* ParentWidget = LayerWidget->GetParent();
		WritePathFinderConsoleLine(&Output, FString::Printf(
			TEXT("  layer name=%s parent=%s visibility=%d visible=%s cached=%s inViewport=%s"),
			*LayerWidget->GetName(),
			ParentWidget != nullptr ? *ParentWidget->GetName() : TEXT("none"),
			static_cast<int32>(LayerWidget->GetVisibility()),
			*FormatPathFinderBool(LayerWidget->IsVisible()),
			*FormatPathFinderBool(LayerWidget->GetCachedWidget().IsValid()),
			*FormatPathFinderBool(LayerWidget->IsInViewport())));
	}

	int32 ToggleWidgetCount = 0;
	for (TObjectIterator<UPathFinderRouteMapToggleWidget> ToggleWidgetIterator; ToggleWidgetIterator; ++ToggleWidgetIterator)
	{
		UPathFinderRouteMapToggleWidget* ToggleWidget = *ToggleWidgetIterator;
		if (!IsValid(ToggleWidget) || ToggleWidget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || ToggleWidget->GetWorld() != World)
		{
			continue;
		}

		++ToggleWidgetCount;
		UPanelWidget* ParentWidget = ToggleWidget->GetParent();
		WritePathFinderConsoleLine(&Output, FString::Printf(
			TEXT("  toggle name=%s parent=%s visibility=%d visible=%s cached=%s inViewport=%s"),
			*ToggleWidget->GetName(),
			ParentWidget != nullptr ? *ParentWidget->GetName() : TEXT("none"),
			static_cast<int32>(ToggleWidget->GetVisibility()),
			*FormatPathFinderBool(ToggleWidget->IsVisible()),
			*FormatPathFinderBool(ToggleWidget->GetCachedWidget().IsValid()),
			*FormatPathFinderBool(ToggleWidget->IsInViewport())));
	}

	WritePathFinderConsoleLine(&Output, FString::Printf(TEXT("RouteMapStatus widgets layers=%d toggles=%d"), LayerWidgetCount, ToggleWidgetCount));

	int32 VanillaMapWidgetCount = 0;
	for (TObjectIterator<UUserWidget> MapWidgetIterator; MapWidgetIterator; ++MapWidgetIterator)
	{
		UUserWidget* MapWidget = *MapWidgetIterator;
		if (!IsValid(MapWidget) || MapWidget->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject) || MapWidget->GetWorld() != World || MapWidget->GetClass()->GetName() != TEXT("Widget_Map_C"))
		{
			continue;
		}

		++VanillaMapWidgetCount;
		UWidget* MapTargetWidget = MapWidget->GetWidgetFromName(TEXT("mMap"));
		UPanelWidget* MapTargetPanel = Cast<UPanelWidget>(MapTargetWidget);
		UPanelWidget* MapTargetParent = MapTargetWidget != nullptr ? MapTargetWidget->GetParent() : nullptr;
		WritePathFinderConsoleLine(&Output, FString::Printf(
			TEXT("  vanillaMap name=%s cached=%s visible=%s mMapClass=%s mMapIsPanel=%s mMapAllowsMultiple=%s mMapParent=%s mMapParentClass=%s"),
			*MapWidget->GetName(),
			*FormatPathFinderBool(MapWidget->GetCachedWidget().IsValid()),
			*FormatPathFinderBool(MapWidget->IsVisible()),
			MapTargetWidget != nullptr ? *MapTargetWidget->GetClass()->GetName() : TEXT("none"),
			*FormatPathFinderBool(MapTargetPanel != nullptr),
			*FormatPathFinderBool(MapTargetPanel != nullptr && MapTargetPanel->CanHaveMultipleChildren()),
			MapTargetParent != nullptr ? *MapTargetParent->GetName() : TEXT("none"),
			MapTargetParent != nullptr ? *MapTargetParent->GetClass()->GetName() : TEXT("none")));
	}
	WritePathFinderConsoleLine(&Output, FString::Printf(TEXT("RouteMapStatus vanillaMapWidgets=%d"), VanillaMapWidgetCount));
}

void FPathFinderConsoleCommands::ToggleMapWithOutput(FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	AFGPlayerController* PlayerController = World != nullptr
		? Cast<AFGPlayerController>(World->GetFirstPlayerController())
		: nullptr;
	if (PlayerController == nullptr)
	{
		WritePathFinderConsoleLine(&Output, TEXT("Cannot toggle map: local player controller unavailable."));
		return;
	}

	UFunction* ToggleMapFunction = PlayerController->FindFunction(TEXT("ToggleMap"));
	if (ToggleMapFunction == nullptr)
	{
		WritePathFinderConsoleLine(&Output, TEXT("Cannot toggle map: ToggleMap function unavailable."));
		return;
	}

	PlayerController->ProcessEvent(ToggleMapFunction, nullptr);
	WritePathFinderConsoleLine(&Output, TEXT("Map toggled."));
}

void FPathFinderConsoleCommands::Clear()
{
	StopStationVehicleTracker();
	StopVehicleTracing();
	UE_LOG(LogPathFinder, Log, TEXT("PathFinder debug state cleared."));
}

void FPathFinderConsoleCommands::ClearWithOutput(FOutputDevice& Output)
{
	Clear();
	WritePathFinderConsoleLine(&Output, TEXT("PathFinder debug state cleared."));
}
