#pragma once

#include "CoreMinimal.h"

class AFGBuildableDockingStation;
class AFGDockingStationIdentifier;

namespace TruckStationSignStationAccess
{
	AFGDockingStationIdentifier* GetIdentifier(const AFGBuildableDockingStation* station);
	AFGBuildableDockingStation* GetStation(const AFGDockingStationIdentifier* identifier);
	bool GetName(const AFGDockingStationIdentifier* identifier, FText& name);
}
