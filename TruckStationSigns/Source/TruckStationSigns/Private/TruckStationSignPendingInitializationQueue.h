#pragma once

#include "CoreMinimal.h"

class AFGBuildableDockingStation;

namespace TruckStationSigns::Lifecycle
{

class FPendingInitializationQueue
{
public:
	void Enqueue(AFGBuildableDockingStation* station);
	void Drain(TFunctionRef<void(AFGBuildableDockingStation*)> initializeStation);
	void Reset();
	int32 Num() const;

private:
	TArray<TWeakObjectPtr<AFGBuildableDockingStation>> pendingStations;
};

}
