#pragma once

#include "CoreMinimal.h"

class FCodexBridgeGameClient;
class IConsoleObject;

class CODEXBRIDGE_API FCodexBridgeConsoleCommands final
{
public:
	explicit FCodexBridgeConsoleCommands(FCodexBridgeGameClient& GameClient);
	~FCodexBridgeConsoleCommands();

	void Register();
	void Unregister();

private:
	void SendMessage(const TArray<FString>& Arguments, FOutputDevice& Output);
	void ShowStatus(FOutputDevice& Output) const;
	void StartNewThread(FOutputDevice& Output);
	void ShowHelp(FOutputDevice& Output) const;
	void Add(IConsoleObject* ConsoleObject);

	FCodexBridgeGameClient& GameClient;
	TArray<IConsoleObject*> RegisteredCommands;
};
