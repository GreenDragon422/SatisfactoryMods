#include "PathFinderRouteMapSubsystem.h"

#include "Components/SplineComponent.h"
#include "Engine/Canvas.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "Engine/Texture2D.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "UObject/StrongObjectPtr.h"
#include "WheeledVehicles/FGVehiclePathNode.h"
#include "WheeledVehicles/FGVehiclePathSegment.h"
#include "WheeledVehicles/FGVehicleSubsystem.h"

namespace
{
	constexpr int32 RouteMapTextureSize = 8192;
	constexpr float RouteLineThickness = 6.0f;
	constexpr float RouteDirectionMarkerSize = 11.0f;
	constexpr float RouteMapSplineSampleIntervalCentimeters = 1000.0f;
	constexpr double WestBoundCentimeters = -324698.832031;
	constexpr double EastBoundCentimeters = 425301.832031;
	constexpr double NorthBoundCentimeters = -375000.0;
	constexpr double SouthBoundCentimeters = 375000.0;
	const FLinearColor PathFinderRouteLineFicsitOrange = FLinearColor::FromSRGBColor(FColor(242, 101, 17, 235));
	const FLinearColor PathFinderRouteLineAssignedBlue = FLinearColor::FromSRGBColor(FColor(36, 146, 255, 245));
	const FLinearColor PathFinderRouteLineUnusedGrey = FLinearColor::FromSRGBColor(FColor(105, 105, 105, 220));
	const TCHAR* RouteDirectionMarkerTexturePath = TEXT("/PathFinder/RouteMap/T_PathFinderRouteDirection.T_PathFinderRouteDirection");

	UTexture2D* GetRouteDirectionMarkerTexture()
	{
		static TStrongObjectPtr<UTexture2D> DirectionMarkerTexture;
		if (!DirectionMarkerTexture.IsValid())
		{
			DirectionMarkerTexture.Reset(LoadObject<UTexture2D>(nullptr, RouteDirectionMarkerTexturePath));
		}
		return DirectionMarkerTexture.Get();
	}

	void DrawRouteDirectionMarker(UCanvas* Canvas, const FVector2D& MarkerCenter, const FVector2D& CardinalDirection)
	{
		UTexture2D* DirectionMarkerTexture = GetRouteDirectionMarkerTexture();
		if (Canvas == nullptr || DirectionMarkerTexture == nullptr)
		{
			return;
		}

		const float RotationDegrees = FMath::RadiansToDegrees(FMath::Atan2(CardinalDirection.Y, CardinalDirection.X));
		const FVector2D MarkerDimensions(RouteDirectionMarkerSize, RouteDirectionMarkerSize);

		Canvas->K2_DrawTexture(
			DirectionMarkerTexture,
			MarkerCenter - MarkerDimensions * 0.5f,
			MarkerDimensions,
			FVector2D::ZeroVector,
			FVector2D::UnitVector,
			FLinearColor::White,
			BLEND_Translucent,
			RotationDegrees,
			FVector2D(0.5f, 0.5f));
	}

	FVector2D ProjectRouteWorldPoint(const FVector& WorldPoint)
	{
		const double HorizontalAlpha = (WorldPoint.X - WestBoundCentimeters) / (EastBoundCentimeters - WestBoundCentimeters);
		const double VerticalAlpha = (WorldPoint.Y - NorthBoundCentimeters) / (SouthBoundCentimeters - NorthBoundCentimeters);
		return FVector2D(HorizontalAlpha * RouteMapTextureSize, VerticalAlpha * RouteMapTextureSize);
	}

	FString MakePathSegmentKey(const FGuid& FirstNodeGuid, const FGuid& SecondNodeGuid)
	{
		if (!FirstNodeGuid.IsValid() || !SecondNodeGuid.IsValid())
		{
			return FString();
		}

		const FString FirstNodeText = FirstNodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		const FString SecondNodeText = SecondNodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
		return FirstNodeText < SecondNodeText
			? FirstNodeText + TEXT("<->") + SecondNodeText
			: SecondNodeText + TEXT("<->") + FirstNodeText;
	}

	FString MakePathSegmentKey(const AFGVehiclePathSegment* PathSegment)
	{
		if (PathSegment == nullptr)
		{
			return FString();
		}

		const AFGVehiclePathNode* EndNode = PathSegment->GetEndNode();
		return MakePathSegmentKey(
			PathSegment->GetStartPathNodeGuid(),
			EndNode != nullptr ? EndNode->GetPathNodeGUID() : FGuid());
	}

	void SampleSpline(const USplineComponent* SplineComponent, TArray<FVector>& WorldPoints)
	{
		WorldPoints.Empty();
		if (SplineComponent == nullptr || SplineComponent->GetNumberOfSplinePoints() < 2)
		{
			return;
		}

		const float SplineLength = SplineComponent->GetSplineLength();
		const int32 SegmentCount = FMath::Max(1, FMath::CeilToInt(SplineLength / RouteMapSplineSampleIntervalCentimeters));
		WorldPoints.Reserve(SegmentCount + 1);
		for (int32 SampleIndex = 0; SampleIndex <= SegmentCount; ++SampleIndex)
		{
			const float DistanceAlongSpline = SplineLength * static_cast<float>(SampleIndex) / static_cast<float>(SegmentCount);
			WorldPoints.Add(SplineComponent->GetLocationAtDistanceAlongSpline(DistanceAlongSpline, ESplineCoordinateSpace::World));
		}
	}

	void BuildNetworkSegmentWorldPoints(
		UFGVehiclePathNetwork* PathNetwork,
		const FVehiclePathNetworkSegmentData& SegmentData,
		USplineComponent* SamplingSpline,
		TArray<FVector>& WorldPoints)
	{
		WorldPoints.Empty();
		if (PathNetwork == nullptr)
		{
			return;
		}

		if (SamplingSpline != nullptr && SegmentData.SplinePoints.Num() > 1)
		{
			SamplingSpline->SetSplineData(SegmentData.SplinePoints, ESplineCoordinateSpace::World);
			SampleSpline(SamplingSpline, WorldPoints);
			if (WorldPoints.Num() > 1)
			{
				return;
			}
		}

		const TArray<FVehiclePathNetworkNodeData>& PathNodes = PathNetwork->GetPathNodes();
		if (PathNodes.IsValidIndex(SegmentData.FromNodeIndex))
		{
			WorldPoints.Add(PathNodes[SegmentData.FromNodeIndex].PathNodeLocation);
		}
		if (PathNodes.IsValidIndex(SegmentData.ToNodeIndex))
		{
			WorldPoints.Add(PathNodes[SegmentData.ToNodeIndex].PathNodeLocation);
		}
	}

	TMap<FString, TArray<FVector>> BuildAllPathSegmentGeometry(UObject* Owner, AFGVehicleSubsystem* VehicleSubsystem)
	{
		TMap<FString, TArray<FVector>> WorldPointsBySegmentKey;
		if (Owner == nullptr || VehicleSubsystem == nullptr)
		{
			return WorldPointsBySegmentKey;
		}

		USplineComponent* SamplingSpline = NewObject<USplineComponent>(Owner, NAME_None, RF_Transient);
		for (UFGVehiclePathNetwork* PathNetwork : VehicleSubsystem->GetAllPathNetworks())
		{
			if (PathNetwork == nullptr)
			{
				continue;
			}

			const TArray<FVehiclePathNetworkNodeData>& PathNodes = PathNetwork->GetPathNodes();
			for (const FVehiclePathNetworkSegmentData& SegmentData : PathNetwork->GetPathSegments())
			{
				if (!PathNodes.IsValidIndex(SegmentData.FromNodeIndex) || !PathNodes.IsValidIndex(SegmentData.ToNodeIndex))
				{
					continue;
				}

				const FString SegmentKey = MakePathSegmentKey(
					PathNodes[SegmentData.FromNodeIndex].PathNodeGuid,
					PathNodes[SegmentData.ToNodeIndex].PathNodeGuid);
				if (SegmentKey.IsEmpty())
				{
					continue;
				}

				TArray<FVector> WorldPoints;
				BuildNetworkSegmentWorldPoints(PathNetwork, SegmentData, SamplingSpline, WorldPoints);
				if (WorldPoints.Num() > 1)
				{
					WorldPointsBySegmentKey.Add(SegmentKey, MoveTemp(WorldPoints));
				}
			}
		}
		return WorldPointsBySegmentKey;
	}

	void DrawRoutePolyline(UCanvas* Canvas, const TArray<FVector>& WorldPoints, const FLinearColor& Color, const bool bDrawArrow)
	{
		if (Canvas == nullptr || WorldPoints.Num() < 2)
		{
			return;
		}

		float TotalLength = 0.0f;
		for (int32 PointIndex = 1; PointIndex < WorldPoints.Num(); ++PointIndex)
		{
			const FVector2D StartPoint = ProjectRouteWorldPoint(WorldPoints[PointIndex - 1]);
			const FVector2D EndPoint = ProjectRouteWorldPoint(WorldPoints[PointIndex]);
			Canvas->K2_DrawLine(StartPoint, EndPoint, RouteLineThickness, Color);
			TotalLength += FVector2D::Distance(StartPoint, EndPoint);
		}

		if (!bDrawArrow || TotalLength <= RouteDirectionMarkerSize)
		{
			return;
		}

		const float ArrowDistance = TotalLength * 0.5f;
		float TraversedLength = 0.0f;
		for (int32 PointIndex = 1; PointIndex < WorldPoints.Num(); ++PointIndex)
		{
			const FVector2D StartPoint = ProjectRouteWorldPoint(WorldPoints[PointIndex - 1]);
			const FVector2D EndPoint = ProjectRouteWorldPoint(WorldPoints[PointIndex]);
			const float SegmentLength = FVector2D::Distance(StartPoint, EndPoint);
			if (TraversedLength + SegmentLength < ArrowDistance || SegmentLength <= UE_SMALL_NUMBER)
			{
				TraversedLength += SegmentLength;
				continue;
			}

			const FVector2D Direction = (EndPoint - StartPoint) / SegmentLength;
			const float SegmentAlpha = (ArrowDistance - TraversedLength) / SegmentLength;
			const FVector2D MarkerCenter = FMath::Lerp(StartPoint, EndPoint, SegmentAlpha);
			const FVector2D CardinalDirection = FMath::Abs(Direction.X) >= FMath::Abs(Direction.Y)
				? FVector2D(FMath::Sign(Direction.X), 0.0f)
				: FVector2D(0.0f, FMath::Sign(Direction.Y));
			DrawRouteDirectionMarker(Canvas, MarkerCenter, CardinalDirection);
			return;
		}
	}
}

void UPathFinderRouteMapSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	bInitialPopulationRequested = true;
}

void UPathFinderRouteMapSubsystem::Deinitialize()
{
	RouteSnapshot.Reset();
	VehicleRouteCache.Clear();
	PendingPathSegmentRefreshes.Empty();
	PendingPathSegmentRemovals.Empty();
	RenderTarget = nullptr;
	CachedWorld.Reset();
	Super::Deinitialize();
}

void UPathFinderRouteMapSubsystem::Tick(float DeltaTime)
{
	(void)DeltaTime;
	UWorld* World = GetWorld();
	if (World == nullptr || World->GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	if (CachedWorld.Get() != World)
	{
		ResetForWorld(World);
	}

	if (!bInitialPopulationRequested && !bRouteUsageRefreshRequested && PendingPathSegmentRefreshes.IsEmpty() && PendingPathSegmentRemovals.IsEmpty())
	{
		return;
	}
	ProcessPendingPathChanges(World);
}

TStatId UPathFinderRouteMapSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UPathFinderRouteMapSubsystem, STATGROUP_Tickables);
}

bool UPathFinderRouteMapSubsystem::IsTickable() const
{
	return !IsTemplate();
}

UWorld* UPathFinderRouteMapSubsystem::GetTickableGameObjectWorld() const
{
	return GetWorld();
}

UCanvasRenderTarget2D* UPathFinderRouteMapSubsystem::AcquireRenderTarget()
{
	if (RenderTarget == nullptr)
	{
		RenderTarget = UCanvasRenderTarget2D::CreateCanvasRenderTarget2D(
			this,
			UCanvasRenderTarget2D::StaticClass(),
			RouteMapTextureSize,
			RouteMapTextureSize);
		if (RenderTarget != nullptr)
		{
			RenderTarget->ClearColor = FLinearColor::Transparent;
			RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8;
			RenderTarget->Filter = TF_Bilinear;
			RenderTarget->UpdateResourceImmediate(true);
			bNeedsRedraw = true;
		}
	}

	if (bNeedsRedraw && bRouteLayerVisible)
	{
		RedrawRouteLayer();
	}
	return RenderTarget;
}

void UPathFinderRouteMapSubsystem::RequestPathSegmentRefresh(AFGVehiclePathSegment* PathSegment)
{
	if (PathSegment != nullptr)
	{
		PendingPathSegmentRefreshes.Add(PathSegment);
	}
}

void UPathFinderRouteMapSubsystem::RequestPathSegmentRemoval(AFGVehiclePathSegment* PathSegment)
{
	const FString SegmentKey = MakePathSegmentKey(PathSegment);
	if (!SegmentKey.IsEmpty())
	{
		PendingPathSegmentRemovals.Add(SegmentKey);
		PendingPathSegmentRefreshes.Remove(PathSegment);
	}
}

void UPathFinderRouteMapSubsystem::RequestRouteUsageRefresh()
{
	bRouteUsageRefreshRequested = true;
}

bool UPathFinderRouteMapSubsystem::IsRouteLayerVisible() const
{
	return bRouteLayerVisible;
}

int32 UPathFinderRouteMapSubsystem::GetRouteSegmentCount() const
{
	return RouteSnapshot.GetWorldPointsBySegmentKey().Num();
}

int32 UPathFinderRouteMapSubsystem::GetRoutePointCount() const
{
	int32 PointCount = 0;
	for (const TPair<FString, TArray<FVector>>& RouteSegmentEntry : RouteSnapshot.GetWorldPointsBySegmentKey())
	{
		PointCount += RouteSegmentEntry.Value.Num();
	}
	return PointCount;
}

int32 UPathFinderRouteMapSubsystem::GetPendingRefreshCount() const
{
	return PendingPathSegmentRefreshes.Num();
}

int32 UPathFinderRouteMapSubsystem::GetPendingRemovalCount() const
{
	return PendingPathSegmentRemovals.Num();
}

bool UPathFinderRouteMapSubsystem::IsInitialPopulationRequested() const
{
	return bInitialPopulationRequested;
}

bool UPathFinderRouteMapSubsystem::IsRedrawPending() const
{
	return bNeedsRedraw;
}

FIntPoint UPathFinderRouteMapSubsystem::GetRenderTargetSize() const
{
	return RenderTarget != nullptr
		? FIntPoint(RenderTarget->SizeX, RenderTarget->SizeY)
		: FIntPoint::ZeroValue;
}

void UPathFinderRouteMapSubsystem::ToggleRouteLayerVisibility()
{
	SetRouteLayerVisible(!bRouteLayerVisible);
}

void UPathFinderRouteMapSubsystem::SetRouteLayerVisible(bool bVisible)
{
	if (bRouteLayerVisible == bVisible)
	{
		return;
	}

	bRouteLayerVisible = bVisible;
	if (bRouteLayerVisible && bNeedsRedraw)
	{
		RedrawRouteLayer();
	}
	OnVisibilityChanged.Broadcast(bRouteLayerVisible);
}

void UPathFinderRouteMapSubsystem::SetNetworkUsageFilterEnabled(const bool bEnabled)
{
	if (bNetworkUsageFilterEnabled == bEnabled)
	{
		return;
	}

	bNetworkUsageFilterEnabled = bEnabled;
	bRouteUsageRefreshRequested = true;
	bNeedsRedraw = true;
}

bool UPathFinderRouteMapSubsystem::IsNetworkUsageFilterEnabled() const
{
	return bNetworkUsageFilterEnabled;
}

void UPathFinderRouteMapSubsystem::ResetForWorld(UWorld* World)
{
	CachedWorld = World;
	RouteSnapshot.Reset();
	VehicleRouteCache.Clear();
	PendingPathSegmentRefreshes.Empty();
	PendingPathSegmentRemovals.Empty();
	bInitialPopulationRequested = true;
	bRouteUsageRefreshRequested = true;
	bNeedsRedraw = true;
	if (RenderTarget != nullptr)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, FLinearColor::Transparent);
	}
}

void UPathFinderRouteMapSubsystem::ProcessPendingPathChanges(UWorld* World)
{
	AFGVehicleSubsystem* VehicleSubsystem = AFGVehicleSubsystem::Get(World);
	if (VehicleSubsystem == nullptr)
	{
		return;
	}

	bool bGeometryChanged = false;
	bool bRouteUsageChanged = false;
	if (bRouteUsageRefreshRequested)
	{
		bRouteUsageRefreshRequested = false;
		const FPathFinderVehicleRouteCacheUpdateResult RouteCacheResult = VehicleRouteCache.Update(
			World,
			World->GetFirstPlayerController(),
			VehicleSubsystem->GetAllVehicles());
		bRouteUsageChanged = RouteCacheResult.bRouteMembershipChanged;
	}
	if (bInitialPopulationRequested)
	{
		bInitialPopulationRequested = false;
		bGeometryChanged |= RouteSnapshot.ReplaceAllSegments(BuildAllPathSegmentGeometry(this, VehicleSubsystem));
	}

	for (const FString& SegmentKey : PendingPathSegmentRemovals)
	{
		bGeometryChanged |= RouteSnapshot.RemoveSegment(SegmentKey);
	}
	PendingPathSegmentRemovals.Empty();

	for (const TWeakObjectPtr<AFGVehiclePathSegment>& PathSegmentReference : PendingPathSegmentRefreshes)
	{
		AFGVehiclePathSegment* PathSegment = PathSegmentReference.Get();
		if (PathSegment == nullptr)
		{
			continue;
		}

		const FString SegmentKey = MakePathSegmentKey(PathSegment);
		if (SegmentKey.IsEmpty())
		{
			continue;
		}

		TArray<FVector> WorldPoints;
		SampleSpline(PathSegment->GetSplineComponent(), WorldPoints);
		bGeometryChanged |= WorldPoints.Num() > 1
			? RouteSnapshot.UpdateSegment(SegmentKey, WorldPoints)
			: RouteSnapshot.RemoveSegment(SegmentKey);
	}
	PendingPathSegmentRefreshes.Empty();

	if (bGeometryChanged || bRouteUsageChanged || bNeedsRedraw)
	{
		bNeedsRedraw = true;
		if (RenderTarget != nullptr && bRouteLayerVisible)
		{
			RedrawRouteLayer();
		}
	}
}

void UPathFinderRouteMapSubsystem::RedrawRouteLayer()
{
	if (RenderTarget == nullptr || !bRouteLayerVisible)
	{
		return;
	}

	UKismetRenderingLibrary::ClearRenderTarget2D(this, RenderTarget, FLinearColor::Transparent);
	UCanvas* Canvas = nullptr;
	FVector2D RenderTargetSize;
	FDrawToRenderTargetContext DrawContext;
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, RenderTarget, Canvas, RenderTargetSize, DrawContext);
	if (Canvas != nullptr)
	{
		TSet<FString> AssignedSegmentKeys;
		TMultiMap<FString, const FPathFinderPaintSample*> AssignedSamplesBySegmentKey;
		for (const TPair<FString, FPathFinderPaintSample>& PaintSampleEntry : VehicleRouteCache.GetPaintSamplesBySegmentKey())
		{
			const FPathFinderPaintSample& PaintSample = PaintSampleEntry.Value;
			const FString SegmentKey = MakePathSegmentKey(PaintSample.FromNodeGuid, PaintSample.ToNodeGuid);
			AssignedSegmentKeys.Add(SegmentKey);
			AssignedSamplesBySegmentKey.Add(SegmentKey, &PaintSample);
		}

		for (const TPair<FString, TArray<FVector>>& RouteSegmentEntry : RouteSnapshot.GetWorldPointsBySegmentKey())
		{
			const TArray<FVector>& WorldPoints = RouteSegmentEntry.Value;
			const bool bAssigned = AssignedSegmentKeys.Contains(RouteSegmentEntry.Key);
			const FLinearColor SegmentColor = bNetworkUsageFilterEnabled
				? (bAssigned ? PathFinderRouteLineFicsitOrange : PathFinderRouteLineUnusedGrey)
				: (bAssigned ? PathFinderRouteLineAssignedBlue : PathFinderRouteLineFicsitOrange);
			DrawRoutePolyline(
				Canvas,
				WorldPoints,
				SegmentColor,
				!bAssigned);

			if (bAssigned)
			{
				TArray<const FPathFinderPaintSample*> AssignedSamples;
				AssignedSamplesBySegmentKey.MultiFind(RouteSegmentEntry.Key, AssignedSamples);
				for (const FPathFinderPaintSample* AssignedSample : AssignedSamples)
				{
					if (AssignedSample != nullptr)
					{
						DrawRoutePolyline(Canvas, AssignedSample->WorldPoints, SegmentColor, true);
					}
				}
			}
		}
	}
	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, DrawContext);
	bNeedsRedraw = false;
}

FVector2D UPathFinderRouteMapSubsystem::ProjectWorldPoint(const FVector& WorldPoint)
{
	return ProjectRouteWorldPoint(WorldPoint);
}
