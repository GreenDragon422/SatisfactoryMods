#include "Modules/ModuleManager.h"

#include "CodexBridgeConsoleCommands.h"
#include "CodexBridgeConsoleService.h"
#include "CodexBridgeGameClient.h"
#include "CodexBridgeLog.h"
#include "Containers/Ticker.h"

DEFINE_LOG_CATEGORY(LogCodexBridge);

class FCodexBridgeModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if !WITH_EDITOR
		GameClient = MakeUnique<FCodexBridgeGameClient>();
		ConsoleService = MakeUnique<FCodexBridgeConsoleService>();
		ConsoleCommands = MakeUnique<FCodexBridgeConsoleCommands>(*GameClient);
		ConsoleCommands->Register();
		TickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			TEXT("CodexBridge.Dispatch"),
			0.01f,
			[this](float)
			{
				return DispatchRequests();
			});
		GameClient->Start();
		UE_LOG(LogCodexBridge, Log, TEXT("CodexBridge module started."));
#endif
	}

	virtual void ShutdownModule() override
	{
		if (TickerHandle.IsValid())
		{
			FTSTicker::RemoveTicker(TickerHandle);
			TickerHandle.Reset();
		}
		if (ConsoleCommands.IsValid())
		{
			ConsoleCommands->Unregister();
			ConsoleCommands.Reset();
		}
		if (GameClient.IsValid())
		{
			GameClient->Shutdown();
			GameClient.Reset();
		}
		ConsoleService.Reset();
	}

private:
	bool DispatchRequests()
	{
		if (!GameClient.IsValid() || !ConsoleService.IsValid())
		{
			return false;
		}
		FCodexBridgeRequest Request;
		int32 Processed = 0;
		while (Processed < 16 && GameClient->DequeueRequest(Request))
		{
			const double StartedAt = FPlatformTime::Seconds();
			if (!Request.ExpectsResponse)
			{
				ConsoleService->HandleEvent(Request);
				Processed++;
				continue;
			}
			FCodexBridgeResponse Response = ConsoleService->Handle(Request);
			GameClient->EnqueueResponse(Request, Response);
			UE_LOG(
				LogCodexBridge,
				Verbose,
				TEXT("Request %s method=%s ok=%s durationMs=%.2f"),
				*Request.Id,
				*Request.Method,
				Response.IsSuccessful ? TEXT("true") : TEXT("false"),
				(FPlatformTime::Seconds() - StartedAt) * 1000.0);
			Processed++;
		}
		return true;
	}

	TUniquePtr<FCodexBridgeGameClient> GameClient;
	TUniquePtr<FCodexBridgeConsoleService> ConsoleService;
	TUniquePtr<FCodexBridgeConsoleCommands> ConsoleCommands;
	FTSTicker::FDelegateHandle TickerHandle;
};

IMPLEMENT_GAME_MODULE(FCodexBridgeModule, CodexBridge);
