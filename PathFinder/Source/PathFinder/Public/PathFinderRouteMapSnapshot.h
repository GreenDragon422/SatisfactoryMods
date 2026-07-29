#pragma once

#include "CoreMinimal.h"

class FPathFinderRouteMapSnapshot
{
public:
	bool ReplaceAllSegments(const TMap<FString, TArray<FVector>>& CandidateWorldPointsBySegmentKey);
	bool UpdateSegment(const FString& SegmentKey, const TArray<FVector>& WorldPoints);
	bool RemoveSegment(const FString& SegmentKey);
	void Reset();
	const TMap<FString, TArray<FVector>>& GetWorldPointsBySegmentKey() const;

private:
	bool bInitialized{false};
	TMap<FString, TArray<FVector>> WorldPointsBySegmentKey;
};
