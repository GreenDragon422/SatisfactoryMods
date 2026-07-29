#pragma once

#include "CoreMinimal.h"

class AFGWheeledVehicle;
class AFGWheeledVehicleIdentifier;
class APlayerController;
class UFGVehiclePathPreset;
class UWorld;

struct FPathFinderVehicleContext
{
	UWorld* World{nullptr};
	APlayerController* PlayerController{nullptr};
	APawn* Pawn{nullptr};
	AFGWheeledVehicle* WheeledVehicle{nullptr};
	AFGWheeledVehicleIdentifier* VehicleIdentifier{nullptr};
	UFGVehiclePathPreset* VehiclePathPreset{nullptr};
	FString FailureReason;

	bool IsValid() const;
	bool IsRouteTraceValid() const;
	FString ToDebugString() const;
};

class FPathFinderVehicleContextResolver
{
public:
	static FPathFinderVehicleContext Resolve(UWorld* World);
};
