#include "PathFinderVehicleRouteCache.h"

#include "PathFinderTraceService.h"
#include "PathFinderVehicleContext.h"
#include "WheeledVehicles/FGWheeledVehicle.h"
#include "WheeledVehicles/FGWheeledVehicleIdentifier.h"

namespace
{
	FPathFinderVehicleContext MakeRouteCacheVehicleContext(UWorld* World, APlayerController* PlayerController, AFGWheeledVehicleIdentifier* VehicleIdentifier)
	{
		FPathFinderVehicleContext Context;
		Context.World = World;
		Context.PlayerController = PlayerController;
		Context.VehicleIdentifier = VehicleIdentifier;

		if (VehicleIdentifier == nullptr)
		{
			Context.FailureReason = TEXT("vehicle identifier is unavailable");
			return Context;
		}

		Context.WheeledVehicle = VehicleIdentifier->GetOwnerVehicle();
		Context.Pawn = Cast<APawn>(Context.WheeledVehicle);
		if (Context.WheeledVehicle != nullptr)
		{
			Context.VehiclePathPreset = Context.WheeledVehicle->GetVehiclePathPreset();
		}

		if (Context.VehiclePathPreset == nullptr)
		{
			Context.VehiclePathPreset = AFGWheeledVehicle::GetPathPathPresetForVehicleType(VehicleIdentifier->GetVehicleClass());
		}

		if (Context.VehiclePathPreset == nullptr)
		{
			Context.FailureReason = TEXT("vehicle path preset is unavailable");
		}

		return Context;
	}
}

void FPathFinderVehicleRouteCache::Clear()
{
	CacheEntriesByVehicle.Empty();
	RouteSegmentReferenceCounts.Empty();
	PaintSamplesBySegmentKey.Empty();
	DirtySegmentKeys.Empty();
}

FPathFinderVehicleRouteCacheUpdateResult FPathFinderVehicleRouteCache::Update(UWorld* World, APlayerController* PlayerController, const TArray<AFGWheeledVehicleIdentifier*>& VehicleIdentifiers)
{
	FPathFinderVehicleRouteCacheUpdateResult Result;
	TSet<TObjectKey<AFGWheeledVehicleIdentifier>> CurrentVehicleKeys;

	for (AFGWheeledVehicleIdentifier* VehicleIdentifier : VehicleIdentifiers)
	{
		if (VehicleIdentifier == nullptr)
		{
			continue;
		}

		++Result.VehiclesScanned;
		const TObjectKey<AFGWheeledVehicleIdentifier> VehicleKey(VehicleIdentifier);
		CurrentVehicleKeys.Add(VehicleKey);

		FVehicleRouteCacheEntry* ExistingCacheEntry = CacheEntriesByVehicle.Find(VehicleKey);
		const int32 CurrentRouteChangelist = VehicleIdentifier->GetVehicleRouteChangelist();
		const TArray<FGuid>& CurrentRouteNodeGuids = VehicleIdentifier->GetVehicleRoute();
		if (ExistingCacheEntry == nullptr ||
			ExistingCacheEntry->RouteChangelist != CurrentRouteChangelist ||
			ExistingCacheEntry->RouteNodeGuids != CurrentRouteNodeGuids)
		{
			Result.bCacheEntriesChanged = true;
			if (ExistingCacheEntry != nullptr)
			{
				RemoveRouteContribution(*ExistingCacheEntry, Result);
			}

			FVehicleRouteCacheEntry NewCacheEntry = BuildCacheEntry(World, PlayerController, VehicleIdentifier);
			FVehicleRouteCacheEntry& StoredCacheEntry = CacheEntriesByVehicle.FindOrAdd(VehicleKey);
			StoredCacheEntry = MoveTemp(NewCacheEntry);
			AddRouteContribution(StoredCacheEntry, Result);
			ExistingCacheEntry = &StoredCacheEntry;
		}

		if (ExistingCacheEntry != nullptr && ExistingCacheEntry->bRouteTraceValid)
		{
			++Result.ValidRoutes;
			Result.RouteLegs += ExistingCacheEntry->RouteLegs;
			Result.PaintSampleCount += ExistingCacheEntry->PaintSampleCount;
		}
	}

	TArray<TObjectKey<AFGWheeledVehicleIdentifier>> StaleVehicleKeys;
	for (const TPair<TObjectKey<AFGWheeledVehicleIdentifier>, FVehicleRouteCacheEntry>& CacheEntry : CacheEntriesByVehicle)
	{
		if (!CurrentVehicleKeys.Contains(CacheEntry.Key))
		{
			StaleVehicleKeys.Add(CacheEntry.Key);
		}
	}

	for (const TObjectKey<AFGWheeledVehicleIdentifier>& StaleVehicleKey : StaleVehicleKeys)
	{
		FVehicleRouteCacheEntry* StaleCacheEntry = CacheEntriesByVehicle.Find(StaleVehicleKey);
		if (StaleCacheEntry != nullptr)
		{
			Result.bCacheEntriesChanged = true;
			RemoveRouteContribution(*StaleCacheEntry, Result);
		}

		CacheEntriesByVehicle.Remove(StaleVehicleKey);
	}

	return Result;
}

FPathFinderVehicleRouteCacheUpdateResult FPathFinderVehicleRouteCache::RefreshVehicle(UWorld* World, APlayerController* PlayerController, AFGWheeledVehicleIdentifier* VehicleIdentifier)
{
	FPathFinderVehicleRouteCacheUpdateResult Result;
	if (VehicleIdentifier == nullptr)
	{
		return Result;
	}

	Result.VehiclesScanned = 1;
	Result.bCacheEntriesChanged = true;
	const TObjectKey<AFGWheeledVehicleIdentifier> VehicleKey(VehicleIdentifier);
	FVehicleRouteCacheEntry* ExistingCacheEntry = CacheEntriesByVehicle.Find(VehicleKey);
	if (ExistingCacheEntry != nullptr)
	{
		RemoveRouteContribution(*ExistingCacheEntry, Result);
	}

	FVehicleRouteCacheEntry NewCacheEntry = BuildCacheEntry(World, PlayerController, VehicleIdentifier);
	FVehicleRouteCacheEntry& StoredCacheEntry = CacheEntriesByVehicle.FindOrAdd(VehicleKey);
	StoredCacheEntry = MoveTemp(NewCacheEntry);
	AddRouteContribution(StoredCacheEntry, Result);
	if (StoredCacheEntry.bRouteTraceValid)
	{
		Result.ValidRoutes = 1;
		Result.RouteLegs = StoredCacheEntry.RouteLegs;
		Result.PaintSampleCount = StoredCacheEntry.PaintSampleCount;
	}
	return Result;
}

FPathFinderVehicleRouteCacheUpdateResult FPathFinderVehicleRouteCache::RemoveVehicle(const TObjectKey<AFGWheeledVehicleIdentifier>& VehicleKey)
{
	FPathFinderVehicleRouteCacheUpdateResult Result;
	FVehicleRouteCacheEntry* ExistingCacheEntry = CacheEntriesByVehicle.Find(VehicleKey);
	if (ExistingCacheEntry == nullptr)
	{
		return Result;
	}

	Result.bCacheEntriesChanged = true;
	RemoveRouteContribution(*ExistingCacheEntry, Result);
	CacheEntriesByVehicle.Remove(VehicleKey);
	return Result;
}

const TMap<FString, int32>& FPathFinderVehicleRouteCache::GetRouteSegmentReferenceCounts() const
{
	return RouteSegmentReferenceCounts;
}

const TMap<FString, FPathFinderPaintSample>& FPathFinderVehicleRouteCache::GetPaintSamplesBySegmentKey() const
{
	return PaintSamplesBySegmentKey;
}

TSet<FString> FPathFinderVehicleRouteCache::ConsumeDirtySegmentKeys()
{
	TSet<FString> DirtySegmentKeysToReturn = MoveTemp(DirtySegmentKeys);
	DirtySegmentKeys.Empty();
	return DirtySegmentKeysToReturn;
}

void FPathFinderVehicleRouteCache::MarkSegmentDirty(const FString& SegmentKey)
{
	if (RouteSegmentReferenceCounts.Contains(SegmentKey))
	{
		DirtySegmentKeys.Add(SegmentKey);
	}
}

FString FPathFinderVehicleRouteCache::MakeSegmentKey(const FGuid& FromNodeGuid, const FGuid& ToNodeGuid)
{
	return FromNodeGuid.ToString(EGuidFormats::DigitsWithHyphens) + TEXT("->") + ToNodeGuid.ToString(EGuidFormats::DigitsWithHyphens);
}

FPathFinderVehicleRouteCache::FVehicleRouteCacheEntry FPathFinderVehicleRouteCache::BuildCacheEntry(UWorld* World, APlayerController* PlayerController, AFGWheeledVehicleIdentifier* VehicleIdentifier) const
{
	FVehicleRouteCacheEntry CacheEntry;
	CacheEntry.VehicleIdentifier = VehicleIdentifier;

	if (VehicleIdentifier == nullptr)
	{
		return CacheEntry;
	}

	CacheEntry.RouteChangelist = VehicleIdentifier->GetVehicleRouteChangelist();
	CacheEntry.RouteNodeGuids = VehicleIdentifier->GetVehicleRoute();
	const FPathFinderVehicleContext VehicleContext = MakeRouteCacheVehicleContext(World, PlayerController, VehicleIdentifier);
	if (!VehicleContext.IsRouteTraceValid())
	{
		return CacheEntry;
	}

	const FPathFinderRouteScanResult ScanResult = FPathFinderTraceService::ScanRoute(VehicleContext);
	CacheEntry.bRouteTraceValid = true;
	CacheEntry.RouteLegs = ScanResult.LegResults.Num();
	for (const FPathFinderLegResult& LegResult : ScanResult.LegResults)
	{
		for (const FPathFinderPaintSample& PaintSample : LegResult.PaintSamples)
		{
			++CacheEntry.PaintSampleCount;
			const FString SegmentKey = MakeSegmentKey(PaintSample.FromNodeGuid, PaintSample.ToNodeGuid);
			if (!CacheEntry.PaintSamplesBySegmentKey.Contains(SegmentKey))
			{
				CacheEntry.PaintSamplesBySegmentKey.Add(SegmentKey, PaintSample);
			}
		}
	}

	return CacheEntry;
}

void FPathFinderVehicleRouteCache::AddRouteContribution(const FVehicleRouteCacheEntry& CacheEntry, FPathFinderVehicleRouteCacheUpdateResult& Result)
{
	if (!CacheEntry.bRouteTraceValid)
	{
		return;
	}

	for (const TPair<FString, FPathFinderPaintSample>& PaintSampleEntry : CacheEntry.PaintSamplesBySegmentKey)
	{
		int32& ReferenceCount = RouteSegmentReferenceCounts.FindOrAdd(PaintSampleEntry.Key);
		const bool bWasInactive = ReferenceCount <= 0;
		++ReferenceCount;
		if (bWasInactive)
		{
			PaintSamplesBySegmentKey.Add(PaintSampleEntry.Key, PaintSampleEntry.Value);
			DirtySegmentKeys.Add(PaintSampleEntry.Key);
			Result.bRouteMembershipChanged = true;
		}
	}
}

void FPathFinderVehicleRouteCache::RemoveRouteContribution(const FVehicleRouteCacheEntry& CacheEntry, FPathFinderVehicleRouteCacheUpdateResult& Result)
{
	if (!CacheEntry.bRouteTraceValid)
	{
		return;
	}

	for (const TPair<FString, FPathFinderPaintSample>& PaintSampleEntry : CacheEntry.PaintSamplesBySegmentKey)
	{
		int32* ReferenceCount = RouteSegmentReferenceCounts.Find(PaintSampleEntry.Key);
		if (ReferenceCount == nullptr)
		{
			continue;
		}

		--(*ReferenceCount);
		if (*ReferenceCount <= 0)
		{
			RouteSegmentReferenceCounts.Remove(PaintSampleEntry.Key);
			PaintSamplesBySegmentKey.Remove(PaintSampleEntry.Key);
			DirtySegmentKeys.Remove(PaintSampleEntry.Key);
			Result.bRouteMembershipChanged = true;
		}
	}
}
