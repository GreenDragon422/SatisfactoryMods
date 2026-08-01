#pragma once

#include "CoreMinimal.h"
#include "TruckStationSignOwnershipRegistry.h"
#include "UObject/StrongObjectPtr.h"

class AFGBuildableDockingStation;
class AFGBuildableWidgetSign;
class AFGDockingStationIdentifier;
class UFGSignPrefabWidget;
class UFGSignTypeDescriptor;
class UWorld;

class FTruckStationSignController final
{
public:
	bool Initialize();
	void Reset();
	int32 RefreshWorld(UWorld* world);

	AFGBuildableWidgetSign* EnsureSign(AFGBuildableDockingStation* station);
	void OnStationEndPlay(AFGBuildableDockingStation* station, EEndPlayReason::Type reason);
	void OnStationNameChanged(AFGDockingStationIdentifier* identifier);
	void DisableInteraction(AFGBuildableWidgetSign* sign) const;
	bool IsSupportedStation(const AFGBuildableDockingStation* station) const;
	AFGBuildableWidgetSign* RecreateSignAtRelativeTransformForVisualTest(
		AFGBuildableDockingStation* station,
		const FTransform& relativeTransform);

private:
	AFGBuildableWidgetSign* FindTrackedOrAttachedSign(AFGBuildableDockingStation* station);
	AFGBuildableWidgetSign* SpawnSign(
		AFGBuildableDockingStation* station,
		const FTransform& relativeTransform);
	void ApplyNameToSign(AFGBuildableWidgetSign* sign, const FText& stationName);
	void DestroyGeneratedSign(AFGBuildableWidgetSign* sign);

	TStrongObjectPtr<UClass> standardTruckStationClass;
	TStrongObjectPtr<UClass> fluidTruckStationClass;
	TStrongObjectPtr<UClass> signClass;
	TStrongObjectPtr<UClass> prefabLayoutClass;
	TStrongObjectPtr<UClass> signTypeDescriptorClass;
	FTruckStationSignOwnershipRegistry ownershipRegistry;
	TMap<TWeakObjectPtr<AFGBuildableWidgetSign>, FString> appliedNames;
	bool automaticSignGenerationEnabled = false;
};
