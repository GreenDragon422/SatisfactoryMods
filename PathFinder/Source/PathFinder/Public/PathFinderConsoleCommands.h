#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "PathFinderInputTrace.h"
#include "PathFinderRouteOverlayRenderer.h"
#include "PathFinderTrafficLoad.h"
#include "PathFinderVehicleRouteCache.h"

class IConsoleObject;
class FOutputDevice;
class UWorld;

class FPathFinderConsoleCommands
{
public:
	void Register();
	void Unregister();

private:
	TArray<IConsoleObject*> RegisteredCommands;
	TUniquePtr<FPathFinderInputTrace> InputTrace;
	FTSTicker::FDelegateHandle StationVehicleTrackerTickerHandle;
	FTSTicker::FDelegateHandle VehicleTracingTickerHandle;
	FString StationVehicleTrackerQuery;
	bool bStationVehicleTrackerEnabled{false};
	bool bVehicleTracingEnabled{false};
	bool bNetworkUsageTracingEnabled{false};
	bool bVehicleTracingHasTicked{false};
	bool bLastVehicleTracingHadWorld{false};
	bool bLastVehicleTracingHadPlayer{false};
	bool bLastVehicleTracingHadVehicleSubsystem{false};
	int32 StationVehicleTrackerLastMatchCount{INDEX_NONE};
	TMap<FString, FPathFinderTrafficLoadWindow> VehicleTracingLoadWindowsBySegmentKey;
	FPathFinderVehicleRouteCache VehicleTracingRouteCache;
	FPathFinderRouteOverlayRenderer RouteOverlayRenderer;
	int32 LastVehicleTracingVehiclesScanned{0};
	int32 LastVehicleTracingValidRoutes{0};
	int32 LastVehicleTracingRouteLegs{0};
	int32 LastVehicleTracingPaintSamples{0};
	int32 LastVehicleTracingCandidateSegments{0};
	int32 LastVehicleTracingDrawAttempts{0};
	int32 LastVehicleTracingDrawnSegments{0};
	int32 LastVehicleTracingFailedDraws{0};
	int32 LastVehicleTracingActiveRenderStates{0};

	UWorld* ResolveWorld() const;

	void RegisterCommand(IConsoleObject* ConsoleObject);
	void Help() const;
	void HelpWithOutput(FOutputDevice& Output) const;
	void HelpInternal(FOutputDevice* Output) const;
	void GiveControlToolWithOutput(FOutputDevice& Output) const;
	void TriggerControlToolSecondaryWithOutput(FOutputDevice& Output) const;
	void ShowVehicleContext() const;
	void ShowRoute() const;
	void ScanRoute() const;
	void FindVehiclesForStation(const TArray<FString>& Args) const;
	void FindVehiclesForStationWithOutput(const TArray<FString>& Args, FOutputDevice& Output) const;
	void FindVehiclesForStationInternal(const TArray<FString>& Args, FOutputDevice* Output) const;
	void TrackStationVehicles(const TArray<FString>& Args);
	void TrackStationVehiclesWithOutput(const TArray<FString>& Args, FOutputDevice& Output);
	void TrackStationVehiclesInternal(const TArray<FString>& Args, FOutputDevice* Output);
	void SetVehicleTracingWithOutput(const TArray<FString>& Args, FOutputDevice& Output);
	void SetVehicleTracingInternal(const TArray<FString>& Args, FOutputDevice* Output);
	void WriteVehicleTracingStatus(FOutputDevice* Output) const;
	void ShowNearestTrafficLabelWithOutput(FOutputDevice& Output) const;
	void MoveToNearestTrafficLabelWithOutput(FOutputDevice& Output) const;
	void TraceLeg(const TArray<FString>& Args) const;
	void ToggleMapWithOutput(FOutputDevice& Output) const;
	void ShowRouteMapStatusWithOutput(FOutputDevice& Output) const;
	void SetInputTrace(const TArray<FString>& Args);
	void Clear();
	void ClearWithOutput(FOutputDevice& Output);

	bool TickStationVehicleTracker(float DeltaSeconds);
	void StopStationVehicleTracker();
	bool TickVehicleTracing(float DeltaSeconds);
	void StopVehicleTracing();
};
