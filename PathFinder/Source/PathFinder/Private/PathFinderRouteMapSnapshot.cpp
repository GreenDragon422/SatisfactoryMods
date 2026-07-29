#include "PathFinderRouteMapSnapshot.h"

bool FPathFinderRouteMapSnapshot::ReplaceAllSegments(const TMap<FString, TArray<FVector>>& CandidateWorldPointsBySegmentKey)
{
	bool bGeometryChanged = !bInitialized || CandidateWorldPointsBySegmentKey.Num() != WorldPointsBySegmentKey.Num();
	if (!bGeometryChanged)
	{
		for (const TPair<FString, TArray<FVector>>& CandidateEntry : CandidateWorldPointsBySegmentKey)
		{
			const TArray<FVector>* ExistingWorldPoints = WorldPointsBySegmentKey.Find(CandidateEntry.Key);
			if (ExistingWorldPoints == nullptr || *ExistingWorldPoints != CandidateEntry.Value)
			{
				bGeometryChanged = true;
				break;
			}
		}
	}

	if (bGeometryChanged)
	{
		WorldPointsBySegmentKey = CandidateWorldPointsBySegmentKey;
	}
	bInitialized = true;
	return bGeometryChanged;
}

bool FPathFinderRouteMapSnapshot::UpdateSegment(const FString& SegmentKey, const TArray<FVector>& WorldPoints)
{
	const TArray<FVector>* ExistingWorldPoints = WorldPointsBySegmentKey.Find(SegmentKey);
	if (ExistingWorldPoints != nullptr && *ExistingWorldPoints == WorldPoints)
	{
		return false;
	}

	WorldPointsBySegmentKey.Add(SegmentKey, WorldPoints);
	bInitialized = true;
	return true;
}

bool FPathFinderRouteMapSnapshot::RemoveSegment(const FString& SegmentKey)
{
	bInitialized = true;
	return WorldPointsBySegmentKey.Remove(SegmentKey) > 0;
}

void FPathFinderRouteMapSnapshot::Reset()
{
	bInitialized = false;
	WorldPointsBySegmentKey.Empty();
}

const TMap<FString, TArray<FVector>>& FPathFinderRouteMapSnapshot::GetWorldPointsBySegmentKey() const
{
	return WorldPointsBySegmentKey;
}
