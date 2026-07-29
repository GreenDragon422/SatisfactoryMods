#pragma once

#include "CoreMinimal.h"

class AFGBuildableDockingStation;
class AFGBuildableWidgetSign;
class FOutputDevice;
class FTruckStationSignController;
class IConsoleObject;
class UWorld;

class FTruckStationSignConsoleCommands final
{
public:
	explicit FTruckStationSignConsoleCommands(FTruckStationSignController* Controller);

	void Register();
	void Unregister();

private:
	void Help(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void ListStations(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void ShowNearestStationSign(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void InspectNearby(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void ListMatchingNamedSigns(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void ShowDefaultSignPosition(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void MovePlayerRelative(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void ResetNearestNamedSigns(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void ResetAllNamedSigns(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void DeleteSign(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void EnsureNearest(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void SaveCurrentGame(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void SetNearestTransform(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void RefreshWorld(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;
	void PurgeNearestGenerated(const TArray<FString>& Arguments, UWorld* World, FOutputDevice& Output) const;

	AFGBuildableDockingStation* FindNearestStation(UWorld* World) const;
	AFGBuildableWidgetSign* FindCurrentGeneratedSign(AFGBuildableDockingStation* Station) const;
	FString GetStationName(AFGBuildableDockingStation* Station) const;
	bool SignContainsExactText(AFGBuildableWidgetSign* Sign, const FString& Text) const;
	void WritePlayerState(UWorld* World, FOutputDevice& Output) const;
	void WriteStationState(AFGBuildableDockingStation* Station, FOutputDevice& Output) const;
	void WriteSignState(
		AFGBuildableWidgetSign* Sign,
		int32 SignIndex,
		FOutputDevice& Output,
		const AFGBuildableDockingStation* RelativeToStation = nullptr) const;
	void RegisterCommand(IConsoleObject* ConsoleObject);

	FTruckStationSignController* Controller;
	TArray<IConsoleObject*> RegisteredCommands;
};
