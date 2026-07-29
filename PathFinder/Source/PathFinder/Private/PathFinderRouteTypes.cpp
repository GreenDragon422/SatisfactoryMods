#include "PathFinderRouteTypes.h"

bool FPathFinderRouteLeg::IsValid() const
{
	return LegIndex != INDEX_NONE && FromNodeGuid.IsValid() && ToNodeGuid.IsValid();
}

FString FPathFinderRouteLeg::ToDebugString() const
{
	return FString::Printf(TEXT("[%d] %s -> %s"), LegIndex, *FromNodeGuid.ToString(), *ToNodeGuid.ToString());
}

TArray<FPathFinderRouteLeg> FPathFinderRouteBuilder::BuildLoopLegs(const TArray<FGuid>& RouteNodeGuids)
{
	TArray<FPathFinderRouteLeg> Legs;

	const int32 NodeCount = RouteNodeGuids.Num();
	if (NodeCount < 2)
	{
		return Legs;
	}

	Legs.Reserve(NodeCount);

	for (int32 NodeIndex = 0; NodeIndex < NodeCount; ++NodeIndex)
	{
		const int32 NextNodeIndex = (NodeIndex + 1) % NodeCount;

		FPathFinderRouteLeg Leg;
		Leg.LegIndex = NodeIndex;
		Leg.FromNodeGuid = RouteNodeGuids[NodeIndex];
		Leg.ToNodeGuid = RouteNodeGuids[NextNodeIndex];
		Legs.Add(Leg);
	}

	return Legs;
}

FString FPathFinderRouteBuilder::LegStatusToString(EPathFinderLegStatus Status)
{
	switch (Status)
	{
	case EPathFinderLegStatus::NotChecked:
		return TEXT("not checked");
	case EPathFinderLegStatus::Checking:
		return TEXT("checking");
	case EPathFinderLegStatus::Reachable:
		return TEXT("reachable");
	case EPathFinderLegStatus::Failed:
		return TEXT("failed");
	case EPathFinderLegStatus::Invalid:
		return TEXT("invalid");
	case EPathFinderLegStatus::TraceUnavailable:
		return TEXT("trace unavailable");
	default:
		return TEXT("unknown");
	}
}

float FPathFinderRouteBuilder::NormalizeTravelDistance(float TravelDistance, float MaxTravelDistance)
{
	if (MaxTravelDistance <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Clamp(TravelDistance / MaxTravelDistance, 0.0f, 1.0f);
}
