#include "PathFinderTraceService.h"

#include "PathFinderVehicleContext.h"
#include "WheeledVehicles/FGVehicleSubsystem.h"
#include "WheeledVehicles/FGWheeledVehicle.h"
#include "WheeledVehicles/FGWheeledVehicleIdentifier.h"

FPathFinderRouteScanResult FPathFinderTraceService::ScanRoute(const FPathFinderVehicleContext& Context)
{
	FPathFinderRouteScanResult ScanResult;

	if (!Context.IsRouteTraceValid())
	{
		ScanResult.Detail = FString::Printf(TEXT("invalid vehicle context: %s"), *Context.ToDebugString());
		return ScanResult;
	}

	const TArray<FPathFinderRouteLeg> Legs = FPathFinderRouteBuilder::BuildLoopLegs(Context.VehicleIdentifier->GetVehicleRoute());
	if (Legs.Num() == 0)
	{
		ScanResult.Detail = TEXT("vehicle route has fewer than 2 endpoints");
		return ScanResult;
	}

	ScanResult.LegResults.Reserve(Legs.Num());
	for (const FPathFinderRouteLeg& Leg : Legs)
	{
		ScanResult.LegResults.Add(TraceLeg(Context, Leg));
	}

	ScanResult.Detail = FString::Printf(TEXT("scanned %d directed loop legs"), ScanResult.LegResults.Num());
	return ScanResult;
}

FPathFinderLegResult FPathFinderTraceService::TraceLeg(const FPathFinderVehicleContext& Context, const FPathFinderRouteLeg& Leg)
{
	FPathFinderLegResult Result;
	Result.Leg = Leg;

	if (!Context.IsRouteTraceValid())
	{
		Result.Status = EPathFinderLegStatus::Invalid;
		Result.Detail = FString::Printf(TEXT("invalid vehicle context: %s"), *Context.ToDebugString());
		return Result;
	}

	if (!Leg.IsValid())
	{
		Result.Status = EPathFinderLegStatus::Invalid;
		Result.Detail = TEXT("invalid route leg");
		return Result;
	}

	if (Context.VehiclePathPreset == nullptr)
	{
		Result.Status = EPathFinderLegStatus::TraceUnavailable;
		Result.Detail = TEXT("vehicle path preset is unavailable");
		return Result;
	}

	AFGVehicleSubsystem* VehicleSubsystem = AFGVehicleSubsystem::Get(Context.World);
	if (VehicleSubsystem == nullptr)
	{
		Result.Status = EPathFinderLegStatus::TraceUnavailable;
		Result.Detail = TEXT("vehicle subsystem is unavailable");
		return Result;
	}

	UFGVehiclePathNetwork* PathNetwork = VehicleSubsystem->FindNetworkByPathNodeGuid(Leg.FromNodeGuid);
	if (PathNetwork == nullptr)
	{
		Result.Status = EPathFinderLegStatus::TraceUnavailable;
		Result.Detail = TEXT("could not find a path network containing the leg start node");
		return Result;
	}

	PopulateNetworkSummary(PathNetwork, Context.VehiclePathPreset, Result);
	Result.bHasFromNodeLocation = TryResolveNodeLocation(PathNetwork, Leg.FromNodeGuid, Result.FromNodeLocation);
	Result.bHasToNodeLocation = TryResolveNodeLocation(PathNetwork, Leg.ToNodeGuid, Result.ToNodeLocation);
	RunIndependentSearch(PathNetwork, Context.VehiclePathPreset, Result);

	const bool bFoundPath = PathNetwork->FindVehiclePath(Leg.FromNodeGuid, Leg.ToNodeGuid, Context.VehiclePathPreset, Result.PathNodeGuids);
	if (bFoundPath)
	{
		Result.Status = EPathFinderLegStatus::Reachable;
		Result.Detail = FString::Printf(TEXT("path contains %d nodes"), Result.PathNodeGuids.Num());
		BuildSuccessfulPathSamples(PathNetwork, Result.PathNodeGuids, Result.PaintSamples);
		return Result;
	}

	Result.Status = EPathFinderLegStatus::Failed;
	Result.Detail = TEXT("no valid path found; painting reachable directed segments from leg start");
	BuildReachableSamples(PathNetwork, Leg.FromNodeGuid, Context.VehiclePathPreset, Result.PaintSamples);
	return Result;
}

void FPathFinderTraceService::PopulateNetworkSummary(UFGVehiclePathNetwork* PathNetwork, const UFGVehiclePathPreset* VehiclePathPreset, FPathFinderLegResult& Result)
{
	if (PathNetwork == nullptr)
	{
		return;
	}

	const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
	const TArray<FVehiclePathNetworkSegmentData>& Segments = PathNetwork->GetPathSegments();

	Result.NetworkId = PathNetwork->GetNetworkID();
	Result.NetworkNodeCount = Nodes.Num();
	Result.NetworkSegmentCount = Segments.Num();
	Result.FromNodeIndex = PathNetwork->FindPathNodeIndexByGuid(Result.Leg.FromNodeGuid);
	Result.ToNodeIndex = PathNetwork->FindPathNodeIndexByGuid(Result.Leg.ToNodeGuid);

	for (const FVehiclePathNetworkSegmentData& SegmentData : Segments)
	{
		if (VehiclePathPreset != nullptr && PathNetwork->CanVehicleTraverseSegment(SegmentData, VehiclePathPreset))
		{
			++Result.TraversableSegmentCount;
		}

		if (SegmentData.FromNodeIndex == Result.FromNodeIndex)
		{
			++Result.FromOutgoingSegmentCount;
		}

		if (SegmentData.ToNodeIndex == Result.FromNodeIndex)
		{
			++Result.FromIncomingSegmentCount;
		}

		if (SegmentData.FromNodeIndex == Result.ToNodeIndex)
		{
			++Result.ToOutgoingSegmentCount;
		}

		if (SegmentData.ToNodeIndex == Result.ToNodeIndex)
		{
			++Result.ToIncomingSegmentCount;
		}
	}
}

void FPathFinderTraceService::RunIndependentSearch(UFGVehiclePathNetwork* PathNetwork, const UFGVehiclePathPreset* VehiclePathPreset, FPathFinderLegResult& Result)
{
	if (PathNetwork == nullptr || VehiclePathPreset == nullptr)
	{
		return;
	}

	const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
	const TArray<FVehiclePathNetworkSegmentData>& Segments = PathNetwork->GetPathSegments();
	const int32 StartNodeIndex = Result.FromNodeIndex;
	const int32 TargetNodeIndex = Result.ToNodeIndex;
	if (!Nodes.IsValidIndex(StartNodeIndex) || !Nodes.IsValidIndex(TargetNodeIndex))
	{
		return;
	}

	TArray<float> TravelDistances;
	TravelDistances.Init(TNumericLimits<float>::Max(), Nodes.Num());
	TravelDistances[StartNodeIndex] = 0.0f;

	TArray<float> EstimatedTotalCosts;
	EstimatedTotalCosts.Init(TNumericLimits<float>::Max(), Nodes.Num());
	EstimatedTotalCosts[StartNodeIndex] = FVector::Distance(Nodes[StartNodeIndex].PathNodeLocation, Nodes[TargetNodeIndex].PathNodeLocation);

	TArray<int32> PreviousNodeIndexes;
	PreviousNodeIndexes.Init(INDEX_NONE, Nodes.Num());

	TSet<int32> VisitedNodeIndexes;

	while (VisitedNodeIndexes.Num() < Nodes.Num())
	{
		int32 CurrentNodeIndex = INDEX_NONE;
		float CurrentEstimatedTotalCost = TNumericLimits<float>::Max();

		for (int32 NodeIndex = 0; NodeIndex < EstimatedTotalCosts.Num(); ++NodeIndex)
		{
			if (!VisitedNodeIndexes.Contains(NodeIndex) && EstimatedTotalCosts[NodeIndex] < CurrentEstimatedTotalCost)
			{
				CurrentNodeIndex = NodeIndex;
				CurrentEstimatedTotalCost = EstimatedTotalCosts[NodeIndex];
			}
		}

		if (CurrentNodeIndex == INDEX_NONE)
		{
			break;
		}

		VisitedNodeIndexes.Add(CurrentNodeIndex);
		++Result.SearchVisitedNodeCount;

		if (CurrentNodeIndex == TargetNodeIndex)
		{
			Result.bIndependentSearchFoundPath = true;
			Result.SearchTravelDistance = TravelDistances[CurrentNodeIndex];
			break;
		}

		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			const FVehiclePathNetworkSegmentData& SegmentData = Segments[SegmentIndex];
			if (SegmentData.FromNodeIndex != CurrentNodeIndex || !Nodes.IsValidIndex(SegmentData.ToNodeIndex))
			{
				continue;
			}

			if (!PathNetwork->CanVehicleTraverseSegment(SegmentData, VehiclePathPreset))
			{
				continue;
			}

			const float SegmentLength = FMath::Max(SegmentData.SegmentLength, 0.0f);
			const float CandidateTravelDistance = TravelDistances[CurrentNodeIndex] + SegmentLength;
			if (CandidateTravelDistance >= TravelDistances[SegmentData.ToNodeIndex])
			{
				continue;
			}

			PreviousNodeIndexes[SegmentData.ToNodeIndex] = CurrentNodeIndex;
			TravelDistances[SegmentData.ToNodeIndex] = CandidateTravelDistance;
			EstimatedTotalCosts[SegmentData.ToNodeIndex] = CandidateTravelDistance + FVector::Distance(Nodes[SegmentData.ToNodeIndex].PathNodeLocation, Nodes[TargetNodeIndex].PathNodeLocation);
			++Result.SearchRelaxedSegmentCount;
		}
	}

	if (Result.bIndependentSearchFoundPath)
	{
		TArray<FGuid> ReversedPathNodeGuids;
		int32 PathNodeIndex = TargetNodeIndex;
		while (Nodes.IsValidIndex(PathNodeIndex))
		{
			ReversedPathNodeGuids.Add(Nodes[PathNodeIndex].PathNodeGuid);
			if (PathNodeIndex == StartNodeIndex)
			{
				break;
			}

			PathNodeIndex = PreviousNodeIndexes[PathNodeIndex];
		}

		Result.IndependentPathNodeGuids.Reserve(ReversedPathNodeGuids.Num());
		for (int32 ReversedIndex = ReversedPathNodeGuids.Num() - 1; ReversedIndex >= 0; --ReversedIndex)
		{
			Result.IndependentPathNodeGuids.Add(ReversedPathNodeGuids[ReversedIndex]);
		}
		return;
	}

	int32 ClosestNodeIndex = INDEX_NONE;
	float ClosestNodeDistanceToTarget = TNumericLimits<float>::Max();
	for (const int32 VisitedNodeIndex : VisitedNodeIndexes)
	{
		if (!Nodes.IsValidIndex(VisitedNodeIndex))
		{
			continue;
		}

		const float DistanceToTarget = FVector::Distance(Nodes[VisitedNodeIndex].PathNodeLocation, Nodes[TargetNodeIndex].PathNodeLocation);
		if (DistanceToTarget < ClosestNodeDistanceToTarget)
		{
			ClosestNodeIndex = VisitedNodeIndex;
			ClosestNodeDistanceToTarget = DistanceToTarget;
		}
	}

	if (Nodes.IsValidIndex(ClosestNodeIndex))
	{
		Result.bHasClosestNodeLocation = true;
		Result.ClosestNodeGuid = Nodes[ClosestNodeIndex].PathNodeGuid;
		Result.ClosestNodeLocation = Nodes[ClosestNodeIndex].PathNodeLocation;
		Result.ClosestNodeDistanceToTarget = ClosestNodeDistanceToTarget;
		Result.ClosestNodeTravelDistance = TravelDistances[ClosestNodeIndex];
	}
}

bool FPathFinderTraceService::TryResolveNodeLocation(UFGVehiclePathNetwork* PathNetwork, const FGuid& PathNodeGuid, FVector& NodeLocation)
{
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

void FPathFinderTraceService::PopulateSampleWorldPoints(UFGVehiclePathNetwork* PathNetwork, const FVehiclePathNetworkSegmentData& SegmentData, FPathFinderPaintSample& PaintSample)
{
	if (PathNetwork == nullptr)
	{
		return;
	}

	if (SegmentData.SplinePoints.Num() > 0)
	{
		PaintSample.WorldPoints.Reserve(SegmentData.SplinePoints.Num());
		for (const FSplinePointData& SplinePoint : SegmentData.SplinePoints)
		{
			PaintSample.WorldPoints.Add(SplinePoint.Location);
		}
		return;
	}

	const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
	if (Nodes.IsValidIndex(SegmentData.FromNodeIndex))
	{
		PaintSample.WorldPoints.Add(Nodes[SegmentData.FromNodeIndex].PathNodeLocation);
	}

	if (Nodes.IsValidIndex(SegmentData.ToNodeIndex))
	{
		PaintSample.WorldPoints.Add(Nodes[SegmentData.ToNodeIndex].PathNodeLocation);
	}
}

void FPathFinderTraceService::BuildSuccessfulPathSamples(UFGVehiclePathNetwork* PathNetwork, const TArray<FGuid>& PathNodeGuids, TArray<FPathFinderPaintSample>& PaintSamples)
{
	if (PathNetwork == nullptr || PathNodeGuids.Num() < 2)
	{
		return;
	}

	float TravelDistance = 0.0f;
	for (int32 NodeIndex = 0; NodeIndex < PathNodeGuids.Num() - 1; ++NodeIndex)
	{
		const int32 FromNodeIndex = PathNetwork->FindPathNodeIndexByGuid(PathNodeGuids[NodeIndex]);
		const int32 ToNodeIndex = PathNetwork->FindPathNodeIndexByGuid(PathNodeGuids[NodeIndex + 1]);
		const int32 SegmentIndex = PathNetwork->FindPathIndexBetweenPathNodes(FromNodeIndex, ToNodeIndex);
		if (SegmentIndex == INDEX_NONE)
		{
			continue;
		}

		const FVehiclePathNetworkSegmentData SegmentData = PathNetwork->GetPathSegmentAtIndex(SegmentIndex);
		TravelDistance += FMath::Max(SegmentData.SegmentLength, 0.0f);

		FPathFinderPaintSample PaintSample;
		PaintSample.SegmentIndex = SegmentIndex;
		PaintSample.FromNodeGuid = PathNodeGuids[NodeIndex];
		PaintSample.ToNodeGuid = PathNodeGuids[NodeIndex + 1];
		PaintSample.TravelDistance = TravelDistance;
		PopulateSampleWorldPoints(PathNetwork, SegmentData, PaintSample);
		PaintSamples.Add(PaintSample);
	}

	NormalizeSamples(PaintSamples);
}

void FPathFinderTraceService::BuildReachableSamples(UFGVehiclePathNetwork* PathNetwork, const FGuid& StartNodeGuid, const UFGVehiclePathPreset* VehiclePathPreset, TArray<FPathFinderPaintSample>& PaintSamples)
{
	if (PathNetwork == nullptr || VehiclePathPreset == nullptr)
	{
		return;
	}

	const TArray<FVehiclePathNetworkNodeData>& Nodes = PathNetwork->GetPathNodes();
	const TArray<FVehiclePathNetworkSegmentData>& Segments = PathNetwork->GetPathSegments();
	const int32 StartNodeIndex = PathNetwork->FindPathNodeIndexByGuid(StartNodeGuid);
	if (!Nodes.IsValidIndex(StartNodeIndex))
	{
		return;
	}

	TArray<float> Distances;
	Distances.Init(TNumericLimits<float>::Max(), Nodes.Num());
	Distances[StartNodeIndex] = 0.0f;

	TSet<int32> VisitedNodeIndexes;
	TMap<int32, int32> PaintSampleIndexesBySegmentIndex;

	while (VisitedNodeIndexes.Num() < Nodes.Num())
	{
		int32 CurrentNodeIndex = INDEX_NONE;
		float CurrentDistance = TNumericLimits<float>::Max();

		for (int32 NodeIndex = 0; NodeIndex < Distances.Num(); ++NodeIndex)
		{
			if (!VisitedNodeIndexes.Contains(NodeIndex) && Distances[NodeIndex] < CurrentDistance)
			{
				CurrentNodeIndex = NodeIndex;
				CurrentDistance = Distances[NodeIndex];
			}
		}

		if (CurrentNodeIndex == INDEX_NONE)
		{
			break;
		}

		VisitedNodeIndexes.Add(CurrentNodeIndex);

		for (int32 SegmentIndex = 0; SegmentIndex < Segments.Num(); ++SegmentIndex)
		{
			const FVehiclePathNetworkSegmentData& SegmentData = Segments[SegmentIndex];
			if (SegmentData.FromNodeIndex != CurrentNodeIndex || !Nodes.IsValidIndex(SegmentData.ToNodeIndex))
			{
				continue;
			}

			if (!PathNetwork->CanVehicleTraverseSegment(SegmentData, VehiclePathPreset))
			{
				continue;
			}

			const float CandidateDistance = CurrentDistance + FMath::Max(SegmentData.SegmentLength, 0.0f);
			if (CandidateDistance >= Distances[SegmentData.ToNodeIndex])
			{
				continue;
			}

			Distances[SegmentData.ToNodeIndex] = CandidateDistance;

			FPathFinderPaintSample PaintSample;
			PaintSample.SegmentIndex = SegmentIndex;
			PaintSample.FromNodeGuid = Nodes[SegmentData.FromNodeIndex].PathNodeGuid;
			PaintSample.ToNodeGuid = Nodes[SegmentData.ToNodeIndex].PathNodeGuid;
			PaintSample.TravelDistance = CandidateDistance;
			PopulateSampleWorldPoints(PathNetwork, SegmentData, PaintSample);

			int32* ExistingPaintSampleIndex = PaintSampleIndexesBySegmentIndex.Find(SegmentIndex);
			if (ExistingPaintSampleIndex != nullptr && PaintSamples.IsValidIndex(*ExistingPaintSampleIndex))
			{
				PaintSamples[*ExistingPaintSampleIndex] = PaintSample;
			}
			else
			{
				PaintSampleIndexesBySegmentIndex.Add(SegmentIndex, PaintSamples.Add(PaintSample));
			}
		}
	}

	NormalizeSamples(PaintSamples);
}

void FPathFinderTraceService::NormalizeSamples(TArray<FPathFinderPaintSample>& PaintSamples)
{
	float MaxTravelDistance = 0.0f;
	for (const FPathFinderPaintSample& PaintSample : PaintSamples)
	{
		MaxTravelDistance = FMath::Max(MaxTravelDistance, PaintSample.TravelDistance);
	}

	for (FPathFinderPaintSample& PaintSample : PaintSamples)
	{
		PaintSample.GradientAlpha = FPathFinderRouteBuilder::NormalizeTravelDistance(PaintSample.TravelDistance, MaxTravelDistance);
	}
}
