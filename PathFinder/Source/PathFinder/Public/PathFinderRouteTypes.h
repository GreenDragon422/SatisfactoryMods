#pragma once

#include "CoreMinimal.h"

enum class EPathFinderLegStatus : uint8
{
	NotChecked,
	Checking,
	Reachable,
	Failed,
	Invalid,
	TraceUnavailable
};

struct FPathFinderRouteLeg
{
	int32 LegIndex{INDEX_NONE};
	FGuid FromNodeGuid;
	FGuid ToNodeGuid;

	bool IsValid() const;
	FString ToDebugString() const;
};

struct FPathFinderPaintSample
{
	int32 SegmentIndex{INDEX_NONE};
	FGuid FromNodeGuid;
	FGuid ToNodeGuid;
	TArray<FVector> WorldPoints;
	float TravelDistance{0.0f};
	float GradientAlpha{0.0f};
};

struct FPathFinderLegResult
{
	FPathFinderRouteLeg Leg;
	EPathFinderLegStatus Status{EPathFinderLegStatus::NotChecked};
	int32 NetworkId{INDEX_NONE};
	int32 NetworkNodeCount{0};
	int32 NetworkSegmentCount{0};
	int32 TraversableSegmentCount{0};
	int32 FromNodeIndex{INDEX_NONE};
	int32 ToNodeIndex{INDEX_NONE};
	int32 FromOutgoingSegmentCount{0};
	int32 FromIncomingSegmentCount{0};
	int32 ToOutgoingSegmentCount{0};
	int32 ToIncomingSegmentCount{0};
	int32 SearchVisitedNodeCount{0};
	int32 SearchRelaxedSegmentCount{0};
	float SearchTravelDistance{0.0f};
	bool bIndependentSearchFoundPath{false};
	bool bHasFromNodeLocation{false};
	bool bHasToNodeLocation{false};
	bool bHasClosestNodeLocation{false};
	FVector FromNodeLocation{ForceInit};
	FVector ToNodeLocation{ForceInit};
	FGuid ClosestNodeGuid;
	FVector ClosestNodeLocation{ForceInit};
	float ClosestNodeDistanceToTarget{0.0f};
	float ClosestNodeTravelDistance{0.0f};
	TArray<FGuid> PathNodeGuids;
	TArray<FGuid> IndependentPathNodeGuids;
	TArray<FPathFinderPaintSample> PaintSamples;
	FString Detail;
};

struct FPathFinderRouteScanResult
{
	TArray<FPathFinderLegResult> LegResults;
	FString Detail;
};

class FPathFinderRouteBuilder
{
public:
	static TArray<FPathFinderRouteLeg> BuildLoopLegs(const TArray<FGuid>& RouteNodeGuids);
	static FString LegStatusToString(EPathFinderLegStatus Status);
	static float NormalizeTravelDistance(float TravelDistance, float MaxTravelDistance);
};
