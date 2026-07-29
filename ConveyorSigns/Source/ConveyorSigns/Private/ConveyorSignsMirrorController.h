#pragma once

#include "CoreMinimal.h"

class AFGBuildableConveyorLift;
class AFGBuildableWidgetSign;
class UWorld;

class FConveyorSignsMirrorController final
{
public:
	void Reset();
	int32 RefreshWorld(UWorld* world);
	AFGBuildableWidgetSign* EnsureMirrorForSource(AFGBuildableWidgetSign* source);
	void SyncSource(AFGBuildableWidgetSign* source);
	void OnSignEndPlay(AFGBuildableWidgetSign* sign, EEndPlayReason::Type reason);
	void OnLiftEndPlay(AFGBuildableConveyorLift* lift, EEndPlayReason::Type reason);

private:
	bool TryGetMirrorTransform(
		AFGBuildableWidgetSign* source,
		const TArray<AFGBuildableConveyorLift*>& lifts,
		FTransform& mirrorTransform,
		FTransform& canonicalSourceTransform,
		AFGBuildableConveyorLift*& matchedLift) const;
	AFGBuildableWidgetSign* EnsureMirror(
		AFGBuildableWidgetSign* source,
		const FTransform& mirrorTransform,
		AFGBuildableConveyorLift* lift);
	AFGBuildableWidgetSign* FindExistingMirror(
		UWorld* world,
		AFGBuildableWidgetSign* source) const;
	void CopySignData(
		AFGBuildableWidgetSign* source,
		AFGBuildableWidgetSign* mirror);
	void ConfigureMirrorInteraction(AFGBuildableWidgetSign* mirror) const;
	void RemoveSavedMirrors(UWorld* world);

	TMap<
		TWeakObjectPtr<AFGBuildableWidgetSign>,
		TWeakObjectPtr<AFGBuildableWidgetSign>> mirrorsBySource;
	TMap<
		TWeakObjectPtr<AFGBuildableWidgetSign>,
		TWeakObjectPtr<AFGBuildableConveyorLift>> liftsBySource;
	TSet<const AFGBuildableWidgetSign*> ignoredEndPlaySigns;
	TSet<TWeakObjectPtr<UWorld>> initializedWorlds;
};
