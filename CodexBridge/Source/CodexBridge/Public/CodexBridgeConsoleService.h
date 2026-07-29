#pragma once

#include "CoreMinimal.h"
#include "CodexBridgeRequest.h"
#include "CodexBridgeTextPageStore.h"

class CODEXBRIDGE_API FCodexBridgeConsoleService final
{
public:
	FCodexBridgeResponse Handle(const FCodexBridgeRequest& Request) const;
	void HandleEvent(const FCodexBridgeRequest& Event) const;

private:
	FCodexBridgeResponse GetStatus() const;
	FCodexBridgeResponse DiscoverCommands(const TSharedPtr<FJsonObject>& Payload) const;
	FCodexBridgeResponse ExecuteCommand(const TSharedPtr<FJsonObject>& Payload) const;
	FCodexBridgeResponse ShowMessage(const TSharedPtr<FJsonObject>& Payload) const;
	FCodexBridgeResponse ReadRecentLog(const TSharedPtr<FJsonObject>& Payload) const;
	FCodexBridgeResponse ReadResultPage(const TSharedPtr<FJsonObject>& Payload) const;
	FCodexBridgeResponse DeliverChatEvent(const FCodexBridgeRequest& Request) const;
	UWorld* ResolveWorld() const;
	void CleanupDiscoveryCursors() const;
	FString StoreDiscoveryCursor(
		const FString& Query,
		const TArray<TSharedPtr<FJsonValue>>& Items,
		int32 Offset) const;

	struct FDiscoveryCursor
	{
		FString Query;
		TArray<TSharedPtr<FJsonValue>> Items;
		int32 Offset = 0;
		FDateTime ExpiresAt;
	};

	mutable TMap<FString, FDiscoveryCursor> DiscoveryCursors;
	mutable FCodexBridgeTextPageStore TextPageStore;
	mutable TArray<FString> DeliveredChatEventIds;
};
