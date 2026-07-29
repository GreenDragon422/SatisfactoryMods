#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FCodexBridgeRequest
{
	FString Id;
	FString Method;
	TSharedPtr<FJsonObject> Payload;
	bool ExpectsResponse = true;
};

struct FCodexBridgeResponse
{
	bool IsSuccessful = false;
	FString Error;
	TSharedPtr<FJsonObject> Payload;
};
