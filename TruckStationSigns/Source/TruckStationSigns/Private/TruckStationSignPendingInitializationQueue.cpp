#include "TruckStationSignPendingInitializationQueue.h"

#include "Buildables/FGBuildableDockingStation.h"

namespace TruckStationSigns::Lifecycle
{

void FPendingInitializationQueue::Enqueue(AFGBuildableDockingStation* station)
{
	if (IsValid(station))
	{
		pendingStations.AddUnique(station);
	}
}

void FPendingInitializationQueue::Drain(
	TFunctionRef<void(AFGBuildableDockingStation*)> initializeStation)
{
	TArray<TWeakObjectPtr<AFGBuildableDockingStation>> stationsToInitialize =
		MoveTemp(pendingStations);
	pendingStations.Reset();

	for (const TWeakObjectPtr<AFGBuildableDockingStation>& pendingStation : stationsToInitialize)
	{
		AFGBuildableDockingStation* station = pendingStation.Get();
		if (IsValid(station))
		{
			initializeStation(station);
		}
	}
}

void FPendingInitializationQueue::Reset()
{
	pendingStations.Reset();
}

int32 FPendingInitializationQueue::Num() const
{
	return pendingStations.Num();
}

}
