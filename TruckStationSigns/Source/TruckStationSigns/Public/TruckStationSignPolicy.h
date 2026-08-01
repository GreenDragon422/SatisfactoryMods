#pragma once

#include "CoreMinimal.h"
#include "FGSignTypes.h"

class UFGSignPrefabWidget;
class UFGSignTypeDescriptor;
class AActor;

class TRUCKSTATIONSIGNS_API FTruckStationSignPolicy final
{
public:
	static FTransform GetFrontSignRelativeTransform();
	static FTransform GetSignRelativeTransform(
		const UClass* stationClass,
		const UClass* standardTruckStationClass,
		const UClass* fluidTruckStationClass);
	static FName GetCurrentGeneratedSignTag();
	static bool IsCurrentGeneratedSign(const AActor* actor);
	static bool ShouldSaveGeneratedSign();
	static bool ShouldDeleteGeneratedSign(EEndPlayReason::Type reason);

	static bool IsSupportedStationClass(
		const UClass* candidateClass,
		const UClass* standardTruckStationClass,
		const UClass* fluidTruckStationClass);

	static FPrefabSignData CreateSignData(
		const FText& stationName,
		TSubclassOf<UFGSignPrefabWidget> prefabLayoutClass,
		TSubclassOf<UFGSignTypeDescriptor> signTypeDescriptorClass);
};
