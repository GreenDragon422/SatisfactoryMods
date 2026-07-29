#include "CodexBridgeTextPageStore.h"

#include "Misc/Guid.h"

namespace
{
	constexpr int32 PageCharacters = 64 * 1024;
	constexpr int32 MaximumStoredCharacters = 16 * 1024 * 1024;
	constexpr int32 MaximumResults = 16;
	const FTimespan ResultLifetime = FTimespan::FromMinutes(2.0);
}

FCodexBridgeTextPage FCodexBridgeTextPageStore::Store(const FString& Text)
{
	Cleanup();
	const FDateTime ExpiresAt = FDateTime::UtcNow() + ResultLifetime;
	const bool Overflow = Text.Len() > MaximumStoredCharacters;
	const int32 StoredLength = FMath::Min(Text.Len(), MaximumStoredCharacters);
	const int32 DiscardedCharacters = Text.Len() - StoredLength;
	const FString StoredText = Text.Left(StoredLength);
	if (StoredText.Len() <= PageCharacters)
	{
		FCodexBridgeTextPage Page;
		Page.IsSuccessful = true;
		Page.Text = StoredText;
		Page.Overflow = Overflow;
		Page.DiscardedCharacters = DiscardedCharacters;
		return Page;
	}

	while (Results.Num() >= MaximumResults)
	{
		FString OldestId;
		FDateTime OldestExpiry = FDateTime::MaxValue();
		for (const TPair<FString, FStoredResult>& Pair : Results)
		{
			if (Pair.Value.ExpiresAt < OldestExpiry)
			{
				OldestId = Pair.Key;
				OldestExpiry = Pair.Value.ExpiresAt;
			}
		}
		if (OldestId.IsEmpty())
		{
			break;
		}
		Results.Remove(OldestId);
	}

	const FString ResultId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Results.Add(ResultId, {StoredText, ExpiresAt});
	return CreatePage(
		ResultId,
		StoredText,
		0,
		ExpiresAt,
		Overflow,
		DiscardedCharacters);
}

FCodexBridgeTextPage FCodexBridgeTextPageStore::Read(
	const FString& ResultId,
	const FString& Cursor)
{
	Cleanup();
	FStoredCursor* StoredCursor = Cursors.Find(Cursor);
	FStoredResult* Result = Results.Find(ResultId);
	if (StoredCursor == nullptr || Result == nullptr || StoredCursor->ResultId != ResultId)
	{
		FCodexBridgeTextPage Page;
		Page.Error = TEXT("result_cursor_invalid");
		return Page;
	}
	const int32 Offset = StoredCursor->Offset;
	Cursors.Remove(Cursor);
	FCodexBridgeTextPage Page = CreatePage(
		ResultId,
		Result->Text,
		Offset,
		Result->ExpiresAt,
		false,
		0);
	if (!Page.HasMore)
	{
		Results.Remove(ResultId);
	}
	return Page;
}

void FCodexBridgeTextPageStore::Cleanup()
{
	const FDateTime Now = FDateTime::UtcNow();
	for (auto Iterator = Results.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Value().ExpiresAt <= Now)
		{
			Iterator.RemoveCurrent();
		}
	}
	for (auto Iterator = Cursors.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Value().ExpiresAt <= Now || !Results.Contains(Iterator.Value().ResultId))
		{
			Iterator.RemoveCurrent();
		}
	}
}

FString FCodexBridgeTextPageStore::CreateCursor(
	const FString& ResultId,
	int32 Offset,
	const FDateTime& ExpiresAt)
{
	const FString Cursor = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	Cursors.Add(Cursor, {ResultId, Offset, ExpiresAt});
	return Cursor;
}

FCodexBridgeTextPage FCodexBridgeTextPageStore::CreatePage(
	const FString& ResultId,
	const FString& Text,
	int32 Offset,
	const FDateTime& ExpiresAt,
	bool Overflow,
	int32 DiscardedCharacters)
{
	FCodexBridgeTextPage Page;
	if (Offset < 0 || Offset >= Text.Len())
	{
		Page.Error = TEXT("result_cursor_invalid");
		return Page;
	}
	Page.IsSuccessful = true;
	Page.ResultId = ResultId;
	Page.Text = Text.Mid(Offset, PageCharacters);
	const int32 NextOffset = Offset + Page.Text.Len();
	Page.HasMore = NextOffset < Text.Len();
	Page.Overflow = Overflow;
	Page.DiscardedCharacters = DiscardedCharacters;
	if (Page.HasMore)
	{
		Page.Cursor = CreateCursor(ResultId, NextOffset, ExpiresAt);
	}
	return Page;
}
