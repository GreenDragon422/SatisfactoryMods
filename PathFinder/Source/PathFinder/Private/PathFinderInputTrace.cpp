#include "PathFinderInputTrace.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathFinderInputTrace, Log, All);

void FPathFinderInputTrace::SetEnabled(bool bNewEnabled)
{
	bEnabled = bNewEnabled;
	UE_LOG(LogPathFinderInputTrace, Log, TEXT("Input trace %s."), bEnabled ? TEXT("enabled") : TEXT("disabled"));

	if (bEnabled)
	{
		LogSnapshot();
	}
}

bool FPathFinderInputTrace::IsEnabled() const
{
	return bEnabled;
}

void FPathFinderInputTrace::LogSnapshot() const
{
	if (!bEnabled)
	{
		return;
	}

	UE_LOG(LogPathFinderInputTrace, Log, TEXT("Known vehicle input actions to watch at runtime:"));
	UE_LOG(LogPathFinderInputTrace, Log, TEXT("  IA_WheeledVehicle_OpenRecorder -> Input.Vehicle.WheeledVehicle.OpenRecorder"));
	UE_LOG(LogPathFinderInputTrace, Log, TEXT("  IA_Vehicles_TogglePathVisualization -> Input.Vehicle.TogglePathVisualization"));
	UE_LOG(LogPathFinderInputTrace, Log, TEXT("  IA_Vehicles_Leave -> Input.Vehicle.LeaveVehicle"));
	UE_LOG(LogPathFinderInputTrace, Log, TEXT("  IA_Vehicles_ToggleCamera -> Input.Vehicle.ToggleCamera"));
	UE_LOG(LogPathFinderInputTrace, Log, TEXT("  IA_Vehicles_ToggleLights -> Input.Vehicle.ToggleLights"));
	UE_LOG(LogPathFinderInputTrace, Log, TEXT("Runtime binding/widget capture still needs in-game validation."));
}
