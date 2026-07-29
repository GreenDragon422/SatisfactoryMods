#pragma once

#include "CoreMinimal.h"
#include "PathFinderRouteTypes.h"

struct FPathFinderVehicleContext;
struct FVehiclePathNetworkSegmentData;
class UFGVehiclePathNetwork;
class UFGVehiclePathPreset;

class FPathFinderTraceService
{
public:
	static FPathFinderRouteScanResult ScanRoute(const FPathFinderVehicleContext& Context);
	static FPathFinderLegResult TraceLeg(const FPathFinderVehicleContext& Context, const FPathFinderRouteLeg& Leg);

private:
	static void PopulateNetworkSummary(UFGVehiclePathNetwork* PathNetwork, const UFGVehiclePathPreset* VehiclePathPreset, FPathFinderLegResult& Result);
	static void RunIndependentSearch(UFGVehiclePathNetwork* PathNetwork, const UFGVehiclePathPreset* VehiclePathPreset, FPathFinderLegResult& Result);
	static bool TryResolveNodeLocation(UFGVehiclePathNetwork* PathNetwork, const FGuid& PathNodeGuid, FVector& NodeLocation);
	static void PopulateSampleWorldPoints(UFGVehiclePathNetwork* PathNetwork, const FVehiclePathNetworkSegmentData& SegmentData, FPathFinderPaintSample& PaintSample);
	static void BuildSuccessfulPathSamples(UFGVehiclePathNetwork* PathNetwork, const TArray<FGuid>& PathNodeGuids, TArray<FPathFinderPaintSample>& PaintSamples);
	static void BuildReachableSamples(UFGVehiclePathNetwork* PathNetwork, const FGuid& StartNodeGuid, const UFGVehiclePathPreset* VehiclePathPreset, TArray<FPathFinderPaintSample>& PaintSamples);
	static void NormalizeSamples(TArray<FPathFinderPaintSample>& PaintSamples);
};
