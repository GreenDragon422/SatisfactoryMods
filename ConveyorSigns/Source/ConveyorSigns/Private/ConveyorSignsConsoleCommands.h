#pragma once

#include "CoreMinimal.h"

class FOutputDevice;
class IConsoleObject;
class UWorld;

class FConveyorSignsConsoleCommands
{
public:
	void Register();
	void Unregister();

private:
	TArray<IConsoleObject*> RegisteredCommands;

	UWorld* ResolveWorld() const;
	void RegisterCommand(IConsoleObject* ConsoleObject);
	void HelpWithOutput(FOutputDevice& Output) const;
	void ListLiftSamplesWithOutput(const TArray<FString>& Arguments, FOutputDevice& Output) const;
	void InspectNearestLiftWithOutput(const TArray<FString>& Arguments, FOutputDevice& Output) const;
	void InspectLiftWithOutput(const TArray<FString>& Arguments, FOutputDevice& Output) const;
	void MoveToLiftSampleWithOutput(const TArray<FString>& Arguments, FOutputDevice& Output) const;
	void MoveToLiftWithOutput(const TArray<FString>& Arguments, FOutputDevice& Output) const;
	void CreateTestSignWithOutput(const TArray<FString>& Arguments, FOutputDevice& Output) const;
	void DeleteTestSignsWithOutput(FOutputDevice& Output) const;
};
