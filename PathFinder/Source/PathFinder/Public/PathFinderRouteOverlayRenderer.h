#pragma once

#include "CoreMinimal.h"
#include "HAL/CriticalSection.h"

class AFGVehiclePathSegment;
class UPathFinderRouteOverlayComponent;
class UWorld;
struct FPathFinderPaintSample;

class FPathFinderRouteOverlayRenderer
{
public:
	struct FLabelGeometry
	{
		FString SegmentKey;
		FVector Location = FVector::ZeroVector;
		FRotator Rotation = FRotator::ZeroRotator;
		FVector TextPlaneNormal = FVector::ZeroVector;
		FVector TextDirection = FVector::ZeroVector;
		FVector StripDirection = FVector::ZeroVector;
		double DistanceCentimeters = 0.0;
	};

	struct FQueuedLabelUpdate
	{
		FString SegmentKey;
		FString LabelText;
	};

	bool DrawPaintSample(UWorld* World, const FString& SegmentKey, const FPathFinderPaintSample& PaintSample, const FLinearColor& Color, bool bColorChanged, const FString& InitialLabelText);
	bool DrawPathSegment(AFGVehiclePathSegment* SegmentActor, const FString& SegmentKey, const FLinearColor& Color, bool bColorChanged, const FString& InitialLabelText);
	void QueueLabelUpdate(const FString& SegmentKey, const FString& LabelText);
	void FlushQueuedLabelUpdates();
	bool UpdateSegmentColor(const FString& SegmentKey, const FLinearColor& Color);
	void RemoveStaleSegments(const TSet<FString>& CurrentSegmentKeys);
	void Clear();
	FString DescribeMaterialProbe() const;
	int32 GetRenderStateCount() const;
	bool GetNearestLabelGeometry(const FVector& ReferenceLocation, FLabelGeometry& LabelGeometry) const;

	static FString DescribeRouteOverlayMaterialProbe();
	static AFGVehiclePathSegment* ResolveVehiclePathSegmentActor(UWorld* World, const FPathFinderPaintSample& PaintSample);

private:
	FCriticalSection QueuedLabelUpdatesCriticalSection;
	TArray<FQueuedLabelUpdate> QueuedLabelUpdates;
	TMap<FString, TWeakObjectPtr<UPathFinderRouteOverlayComponent>> OverlayComponentsBySegmentKey;
};
