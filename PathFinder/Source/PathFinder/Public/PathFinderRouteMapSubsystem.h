#pragma once

#include "CoreMinimal.h"
#include "PathFinderRouteMapSnapshot.h"
#include "PathFinderVehicleRouteCache.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Tickable.h"
#include "PathFinderRouteMapSubsystem.generated.h"

class UCanvasRenderTarget2D;
class AFGVehiclePathSegment;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FPathFinderRouteMapVisibilityChanged, bool, bVisible);

UCLASS()
class PATHFINDER_API UPathFinderRouteMapSubsystem : public UGameInstanceSubsystem, public FTickableGameObject
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool IsTickable() const override;
	virtual UWorld* GetTickableGameObjectWorld() const override;

	UCanvasRenderTarget2D* AcquireRenderTarget();
	void RequestPathSegmentRefresh(AFGVehiclePathSegment* PathSegment);
	void RequestPathSegmentRemoval(AFGVehiclePathSegment* PathSegment);
	void RequestRouteUsageRefresh();
	bool IsRouteLayerVisible() const;
	int32 GetRouteSegmentCount() const;
	int32 GetRoutePointCount() const;
	int32 GetPendingRefreshCount() const;
	int32 GetPendingRemovalCount() const;
	bool IsInitialPopulationRequested() const;
	bool IsRedrawPending() const;
	FIntPoint GetRenderTargetSize() const;
	void ToggleRouteLayerVisibility();
	void SetRouteLayerVisible(bool bVisible);
	void SetNetworkUsageFilterEnabled(bool bEnabled);
	bool IsNetworkUsageFilterEnabled() const;

	UPROPERTY(BlueprintAssignable)
	FPathFinderRouteMapVisibilityChanged OnVisibilityChanged;

private:
	void ResetForWorld(UWorld* World);
	void ProcessPendingPathChanges(UWorld* World);
	void RedrawRouteLayer();
	static FVector2D ProjectWorldPoint(const FVector& WorldPoint);

	UPROPERTY(Transient)
	TObjectPtr<UCanvasRenderTarget2D> RenderTarget;

	TWeakObjectPtr<UWorld> CachedWorld;
	FPathFinderRouteMapSnapshot RouteSnapshot;
	FPathFinderVehicleRouteCache VehicleRouteCache;
	TSet<TWeakObjectPtr<AFGVehiclePathSegment>> PendingPathSegmentRefreshes;
	TSet<FString> PendingPathSegmentRemovals;
	bool bInitialPopulationRequested{true};
	bool bRouteUsageRefreshRequested{true};
	bool bNeedsRedraw{true};
	bool bRouteLayerVisible{true};
	bool bNetworkUsageFilterEnabled{false};
};
