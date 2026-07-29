#include "CodexBridgeConsoleCommands.h"

#include "CodexBridgeGameClient.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

FCodexBridgeConsoleCommands::FCodexBridgeConsoleCommands(
	FCodexBridgeGameClient& InGameClient)
	: GameClient(InGameClient)
{
}

FCodexBridgeConsoleCommands::~FCodexBridgeConsoleCommands()
{
	Unregister();
}

void FCodexBridgeConsoleCommands::Register()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("Codex"),
		TEXT("Send a message to the dedicated Codex game conversation."),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(
			this,
			&FCodexBridgeConsoleCommands::SendMessage),
		ECVF_Default));
	Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("CodexBridge.Status"),
		TEXT("Show the local Codex bridge connection status."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(
			this,
			&FCodexBridgeConsoleCommands::ShowStatus),
		ECVF_Default));
	Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("CodexBridge.Help"),
		TEXT("Show Codex bridge commands."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(
			this,
			&FCodexBridgeConsoleCommands::ShowHelp),
		ECVF_Default));
	Add(ConsoleManager.RegisterConsoleCommand(
		TEXT("CodexBridge.NewThread"),
		TEXT("Forget the dedicated bridge task so the next message starts a new Codex task."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(
			this,
			&FCodexBridgeConsoleCommands::StartNewThread),
		ECVF_Default));
}

void FCodexBridgeConsoleCommands::Unregister()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject* Command : RegisteredCommands)
	{
		if (Command != nullptr)
		{
			ConsoleManager.UnregisterConsoleObject(Command, false);
		}
	}
	RegisteredCommands.Empty();
}

void FCodexBridgeConsoleCommands::SendMessage(
	const TArray<FString>& Arguments,
	FOutputDevice& Output)
{
	const FString Message = FString::Join(Arguments, TEXT(" ")).TrimStartAndEnd();
	if (Message.IsEmpty())
	{
		Output.Log(TEXT("Usage: Codex <message>"));
		return;
	}
	if (!GameClient.IsConnected())
	{
		Output.Logf(TEXT("Codex bridge is not connected: %s"), *GameClient.GetConnectionDescription());
		return;
	}
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("message"), Message);
	if (GameClient.EnqueueEvent(TEXT("chat.request"), Payload))
	{
		Output.Log(TEXT("Codex: sending message to the local bridge..."));
	}
	else
	{
		Output.Log(TEXT("Codex: local bridge queue is busy; message was not sent."));
	}
}

void FCodexBridgeConsoleCommands::ShowStatus(FOutputDevice& Output) const
{
	Output.Logf(
		TEXT("CodexBridge: %s"),
		*GameClient.GetConnectionDescription());
}

void FCodexBridgeConsoleCommands::StartNewThread(FOutputDevice& Output)
{
	if (!GameClient.IsConnected())
	{
		Output.Logf(TEXT("Codex bridge is not connected: %s"), *GameClient.GetConnectionDescription());
		return;
	}
	if (GameClient.EnqueueEvent(TEXT("chat.new_thread"), MakeShared<FJsonObject>()))
	{
		Output.Log(TEXT("Codex: requesting a new bridge task..."));
	}
	else
	{
		Output.Log(TEXT("Codex: local bridge queue is busy; request was not sent."));
	}
}

void FCodexBridgeConsoleCommands::ShowHelp(FOutputDevice& Output) const
{
	Output.Log(TEXT("Codex <message> - send a message to the dedicated Codex task"));
	Output.Log(TEXT("CodexBridge.Status - show connection status"));
	Output.Log(TEXT("CodexBridge.NewThread - use a new Codex task for the next message"));
	Output.Log(TEXT("CodexBridge.Help - show this help"));
}

void FCodexBridgeConsoleCommands::Add(IConsoleObject* ConsoleObject)
{
	if (ConsoleObject != nullptr)
	{
		RegisteredCommands.Add(ConsoleObject);
	}
}
