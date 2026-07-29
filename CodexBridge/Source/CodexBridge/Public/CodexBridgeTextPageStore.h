#pragma once

#include "CoreMinimal.h"

struct FCodexBridgeTextPage
{
	bool IsSuccessful = false;
	FString Error;
	FString Text;
	FString ResultId;
	FString Cursor;
	bool HasMore = false;
	bool Overflow = false;
	int32 DiscardedCharacters = 0;
};

class CODEXBRIDGE_API FCodexBridgeTextPageStore final
{
public:
	FCodexBridgeTextPage Store(const FString& Text);
	FCodexBridgeTextPage Read(const FString& ResultId, const FString& Cursor);

private:
	struct FStoredResult
	{
		FString Text;
		FDateTime ExpiresAt;
	};

	struct FStoredCursor
	{
		FString ResultId;
		int32 Offset = 0;
		FDateTime ExpiresAt;
	};

	void Cleanup();
	FString CreateCursor(const FString& ResultId, int32 Offset, const FDateTime& ExpiresAt);
	FCodexBridgeTextPage CreatePage(
		const FString& ResultId,
		const FString& Text,
		int32 Offset,
		const FDateTime& ExpiresAt,
		bool Overflow,
		int32 DiscardedCharacters);

	TMap<FString, FStoredResult> Results;
	TMap<FString, FStoredCursor> Cursors;
};
