#pragma once

#include "CoreMinimal.h"
#include "PathFinderRouteTypes.h"

class AFGWheeledVehicleIdentifier;
class APlayerController;
class UWorld;

struct FPathFinderVehicleRouteCacheUpdateResult
{
	int32 VehiclesScanned{0};
	int32 ValidRoutes{0};
	int32 RouteLegs{0};
	int32 PaintSampleCount{0};
	bool bCacheEntriesChanged{false};
	bool bRouteMembershipChanged{false};
};

class FPathFinderVehicleRouteCache
{
public:
	void Clear();
	FPathFinderVehicleRouteCacheUpdateResult Update(UWorld* World, APlayerController* PlayerController, const TArray<AFGWheeledVehicleIdentifier*>& VehicleIdentifiers);
	FPathFinderVehicleRouteCacheUpdateResult RefreshVehicle(UWorld* World, APlayerController* PlayerController, AFGWheeledVehicleIdentifier* VehicleIdentifier);
	FPathFinderVehicleRouteCacheUpdateResult RemoveVehicle(const TObjectKey<AFGWheeledVehicleIdentifier>& VehicleKey);
	const TMap<FString, int32>& GetRouteSegmentReferenceCounts() const;
	const TMap<FString, FPathFinderPaintSample>& GetPaintSamplesBySegmentKey() const;
	TSet<FString> ConsumeDirtySegmentKeys();
	void MarkSegmentDirty(const FString& SegmentKey);

	static FString MakeSegmentKey(const FGuid& FromNodeGuid, const FGuid& ToNodeGuid);

private:
	struct FVehicleRouteCacheEntry
	{
		TWeakObjectPtr<AFGWheeledVehicleIdentifier> VehicleIdentifier;
		int32 RouteChangelist{INDEX_NONE};
		TArray<FGuid> RouteNodeGuids;
		bool bRouteTraceValid{false};
		int32 RouteLegs{0};
		int32 PaintSampleCount{0};
		TMap<FString, FPathFinderPaintSample> PaintSamplesBySegmentKey;
	};

	FVehicleRouteCacheEntry BuildCacheEntry(UWorld* World, APlayerController* PlayerController, AFGWheeledVehicleIdentifier* VehicleIdentifier) const;
	void AddRouteContribution(const FVehicleRouteCacheEntry& CacheEntry, FPathFinderVehicleRouteCacheUpdateResult& Result);
	void RemoveRouteContribution(const FVehicleRouteCacheEntry& CacheEntry, FPathFinderVehicleRouteCacheUpdateResult& Result);

	TMap<TObjectKey<AFGWheeledVehicleIdentifier>, FVehicleRouteCacheEntry> CacheEntriesByVehicle;
	TMap<FString, int32> RouteSegmentReferenceCounts;
	TMap<FString, FPathFinderPaintSample> PaintSamplesBySegmentKey;
	TSet<FString> DirtySegmentKeys;
};
