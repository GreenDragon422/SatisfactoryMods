#include "CodexBridgeConsoleService.h"

#include "CodexBridgeLog.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/App.h"
#include "Misc/Guid.h"
#include "Misc/OutputDevice.h"
#include "Misc/Paths.h"

namespace
{
	constexpr int32 MaximumDiscoveryPage = 100;
	constexpr int32 MaximumCommandLength = 4096;
	constexpr int64 MaximumLogBytes = 64 * 1024;
	constexpr int32 MaximumLogLines = 500;
	constexpr int32 MaximumDiscoverySnapshotItems = 10000;
	constexpr int32 MaximumDiscoverySnapshots = 8;
	constexpr int32 MaximumDeliveredChatEventIds = 64;
	const FTimespan DiscoveryCursorLifetime = FTimespan::FromMinutes(2.0);

	FCodexBridgeResponse Success(const TSharedPtr<FJsonObject>& Payload)
	{
		FCodexBridgeResponse Response;
		Response.IsSuccessful = true;
		Response.Payload = Payload;
		return Response;
	}

	FCodexBridgeResponse Failure(const FString& Error)
	{
		FCodexBridgeResponse Response;
		Response.IsSuccessful = false;
		Response.Error = Error;
		Response.Payload = MakeShared<FJsonObject>();
		return Response;
	}

}

FCodexBridgeResponse FCodexBridgeConsoleService::Handle(
	const FCodexBridgeRequest& Request) const
{
	if (Request.Method == TEXT("game.status"))
	{
		return GetStatus();
	}
	if (Request.Method == TEXT("console.discover"))
	{
		return DiscoverCommands(Request.Payload);
	}
	if (Request.Method == TEXT("console.execute"))
	{
		return ExecuteCommand(Request.Payload);
	}
	if (Request.Method == TEXT("game.message"))
	{
		return ShowMessage(Request.Payload);
	}
	if (Request.Method == TEXT("log.recent"))
	{
		return ReadRecentLog(Request.Payload);
	}
	if (Request.Method == TEXT("result.read"))
	{
		return ReadResultPage(Request.Payload);
	}
	if (Request.Method == TEXT("chat.deliver"))
	{
		return DeliverChatEvent(Request);
	}
	return Failure(TEXT("method_not_supported"));
}

void FCodexBridgeConsoleService::HandleEvent(const FCodexBridgeRequest& Event) const
{
	FString Message;
	if (Event.Method == TEXT("chat.reply"))
	{
		if (!Event.Payload.IsValid() ||
			!Event.Payload->TryGetStringField(TEXT("message"), Message) ||
			Message.IsEmpty())
		{
			UE_LOG(LogCodexBridge, Warning, TEXT("Received an invalid chat.reply event."));
			return;
		}
		FString ThreadId;
		Event.Payload->TryGetStringField(TEXT("threadId"), ThreadId);
		UE_LOG(LogCodexBridge, Log, TEXT("Codex task %s returned a game reply."), *ThreadId);
	}
	else if (Event.Method == TEXT("chat.error"))
	{
		FString Error;
		if (!Event.Payload.IsValid() ||
			!Event.Payload->TryGetStringField(TEXT("error"), Error))
		{
			Error = TEXT("unknown_error");
		}
		Message = FString::Printf(TEXT("Error: %s"), *Error.Left(7900));
	}
	else if (Event.Method == TEXT("chat.accepted"))
	{
		double Pending = 0.0;
		if (Event.Payload.IsValid())
		{
			Event.Payload->TryGetNumberField(TEXT("pending"), Pending);
		}
		Message = FString::Printf(
			TEXT("request accepted by the local bridge (pending: %d)"),
			static_cast<int32>(Pending));
	}
	else if (Event.Method == TEXT("chat.thread_reset"))
	{
		Message = TEXT("new bridge task ready; the next message will start it");
	}
	else
	{
		UE_LOG(LogCodexBridge, Warning, TEXT("Ignored unknown bridge event %s."), *Event.Method);
		return;
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("message"), Message);
	ShowMessage(Payload);
}

FCodexBridgeResponse FCodexBridgeConsoleService::DeliverChatEvent(
	const FCodexBridgeRequest& Request) const
{
	if (DeliveredChatEventIds.Contains(Request.Id))
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetBoolField(TEXT("delivered"), true);
		Payload->SetBoolField(TEXT("duplicate"), true);
		return Success(Payload);
	}

	FString EventMethod;
	const TSharedPtr<FJsonObject>* EventPayload = nullptr;
	if (!Request.Payload.IsValid() ||
		!Request.Payload->TryGetStringField(TEXT("eventMethod"), EventMethod) ||
		!Request.Payload->TryGetObjectField(TEXT("eventPayload"), EventPayload) ||
		EventPayload == nullptr ||
		EventMethod.IsEmpty())
	{
		return Failure(TEXT("chat_delivery_invalid"));
	}

	FCodexBridgeRequest Event;
	Event.Id = Request.Id;
	Event.Method = EventMethod;
	Event.Payload = *EventPayload;
	Event.ExpectsResponse = false;
	HandleEvent(Event);
	DeliveredChatEventIds.Add(Request.Id);
	if (DeliveredChatEventIds.Num() > MaximumDeliveredChatEventIds)
	{
		DeliveredChatEventIds.RemoveAt(0, 1, EAllowShrinking::No);
	}

	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("delivered"), true);
	Payload->SetBoolField(TEXT("duplicate"), false);
	return Success(Payload);
}

FCodexBridgeResponse FCodexBridgeConsoleService::GetStatus() const
{
	UWorld* World = ResolveWorld();
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetBoolField(TEXT("gameReady"), World != nullptr && World->HasBegunPlay());
	Payload->SetStringField(TEXT("world"), World != nullptr ? World->GetName() : TEXT(""));
	Payload->SetStringField(TEXT("buildVersion"), FApp::GetBuildVersion());
	Payload->SetStringField(TEXT("modVersion"), TEXT("0.1.0"));
	Payload->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
	Payload->SetStringField(TEXT("executablePath"), FPlatformProcess::ExecutablePath());
	return Success(Payload);
}

FCodexBridgeResponse FCodexBridgeConsoleService::DiscoverCommands(
	const TSharedPtr<FJsonObject>& Payload) const
{
	if (!Payload.IsValid())
	{
		return Failure(TEXT("payload_invalid"));
	}
	FString Query;
	Payload->TryGetStringField(TEXT("query"), Query);
	double RequestedLimit = 50.0;
	Payload->TryGetNumberField(TEXT("limit"), RequestedLimit);
	const int32 Limit = FMath::Clamp(static_cast<int32>(RequestedLimit), 1, MaximumDiscoveryPage);
	CleanupDiscoveryCursors();
	int32 Offset = 0;
	FString Cursor;
	TArray<TSharedPtr<FJsonValue>> AllItems;
	if (Payload->TryGetStringField(TEXT("cursor"), Cursor) && !Cursor.IsEmpty())
	{
		FDiscoveryCursor* StoredCursor = DiscoveryCursors.Find(Cursor);
		if (StoredCursor == nullptr || StoredCursor->Query != Query)
		{
			return Failure(TEXT("cursor_invalid"));
		}
		FDiscoveryCursor Snapshot = MoveTemp(*StoredCursor);
		DiscoveryCursors.Remove(Cursor);
		Offset = Snapshot.Offset;
		AllItems = MoveTemp(Snapshot.Items);
	}
	else
	{
		struct FConsoleEntry
		{
			FString Name;
			FString Kind;
			FString Help;
			uint32 Flags = 0;
		};
		TArray<FConsoleEntry> Entries;
		IConsoleManager::Get().ForEachConsoleObjectThatContains(
			FConsoleObjectVisitor::CreateLambda(
				[&Entries](const TCHAR* Name, IConsoleObject* Object)
				{
					if (Name == nullptr || Object == nullptr)
					{
						return;
					}
					FConsoleEntry Entry;
					Entry.Name = Name;
					Entry.Help = FString(Object->GetHelp()).Left(512);
					Entry.Flags = Object->GetFlags();
					Entry.Kind = Object->AsVariable() != nullptr
						? TEXT("variable")
						: TEXT("command");
					Entries.Add(MoveTemp(Entry));
				}),
			*Query);
		Entries.Sort([](const FConsoleEntry& Left, const FConsoleEntry& Right)
		{
			return Left.Name < Right.Name;
		});
		if (Entries.Num() > MaximumDiscoverySnapshotItems)
		{
			return Failure(TEXT("too_many_results_refine_query"));
		}
		for (const FConsoleEntry& Entry : Entries)
		{
			TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
			Item->SetStringField(TEXT("name"), Entry.Name);
			Item->SetStringField(TEXT("kind"), Entry.Kind);
			Item->SetStringField(TEXT("help"), Entry.Help);
			Item->SetNumberField(TEXT("flags"), Entry.Flags);
			AllItems.Add(MakeShared<FJsonValueObject>(Item));
		}
	}

	if (Offset > AllItems.Num())
	{
		return Failure(TEXT("cursor_expired"));
	}
	TArray<TSharedPtr<FJsonValue>> Items;
	const int32 End = FMath::Min(Offset + Limit, AllItems.Num());
	for (int32 Index = Offset; Index < End; Index++)
	{
		Items.Add(AllItems[Index]);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("items"), Items);
	Result->SetNumberField(TEXT("total"), AllItems.Num());
	Result->SetBoolField(TEXT("hasMore"), End < AllItems.Num());
	if (End < AllItems.Num())
	{
		Result->SetStringField(TEXT("cursor"), StoreDiscoveryCursor(Query, AllItems, End));
	}
	return Success(Result);
}

FCodexBridgeResponse FCodexBridgeConsoleService::ExecuteCommand(
	const TSharedPtr<FJsonObject>& Payload) const
{
	FString Command;
	if (!Payload.IsValid() || !Payload->TryGetStringField(TEXT("command"), Command))
	{
		return Failure(TEXT("command_required"));
	}
	Command.TrimStartAndEndInline();
	if (Command.IsEmpty() || Command.Len() > MaximumCommandLength)
	{
		return Failure(TEXT("command_invalid"));
	}
	UWorld* World = ResolveWorld();
	if (GEngine == nullptr || World == nullptr)
	{
		return Failure(TEXT("game_not_ready"));
	}

	FStringOutputDevice Output;
	const bool Handled = GEngine->Exec(World, *Command, Output);
	FCodexBridgeTextPage Page = TextPageStore.Store(Output);
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("handled"), Handled);
	Result->SetStringField(TEXT("output"), Page.Text);
	Result->SetBoolField(TEXT("truncated"), Page.HasMore || Page.Overflow);
	Result->SetBoolField(TEXT("hasMore"), Page.HasMore);
	if (Page.HasMore)
	{
		Result->SetStringField(TEXT("resultId"), Page.ResultId);
		Result->SetStringField(TEXT("cursor"), Page.Cursor);
	}
	if (Page.Overflow)
	{
		Result->SetBoolField(TEXT("outputLimitExceeded"), true);
		Result->SetNumberField(TEXT("discardedCharacters"), Page.DiscardedCharacters);
	}
	return Success(Result);
}

FCodexBridgeResponse FCodexBridgeConsoleService::ReadResultPage(
	const TSharedPtr<FJsonObject>& Payload) const
{
	FString ResultId;
	FString Cursor;
	if (!Payload.IsValid() ||
		!Payload->TryGetStringField(TEXT("resultId"), ResultId) ||
		!Payload->TryGetStringField(TEXT("cursor"), Cursor))
	{
		return Failure(TEXT("result_cursor_required"));
	}
	FCodexBridgeTextPage Page = TextPageStore.Read(ResultId, Cursor);
	if (!Page.IsSuccessful)
	{
		return Failure(Page.Error);
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("output"), Page.Text);
	Result->SetBoolField(TEXT("hasMore"), Page.HasMore);
	if (Page.HasMore)
	{
		Result->SetStringField(TEXT("resultId"), Page.ResultId);
		Result->SetStringField(TEXT("cursor"), Page.Cursor);
	}
	return Success(Result);
}

void FCodexBridgeConsoleService::CleanupDiscoveryCursors() const
{
	const FDateTime Now = FDateTime::UtcNow();
	for (auto Iterator = DiscoveryCursors.CreateIterator(); Iterator; ++Iterator)
	{
		if (Iterator.Value().ExpiresAt <= Now)
		{
			Iterator.RemoveCurrent();
		}
	}
}

FString FCodexBridgeConsoleService::StoreDiscoveryCursor(
	const FString& Query,
	const TArray<TSharedPtr<FJsonValue>>& Items,
	int32 Offset) const
{
	CleanupDiscoveryCursors();
	while (DiscoveryCursors.Num() >= MaximumDiscoverySnapshots)
	{
		FString OldestCursor;
		FDateTime OldestExpiry = FDateTime::MaxValue();
		for (const TPair<FString, FDiscoveryCursor>& Pair : DiscoveryCursors)
		{
			if (Pair.Value.ExpiresAt < OldestExpiry)
			{
				OldestCursor = Pair.Key;
				OldestExpiry = Pair.Value.ExpiresAt;
			}
		}
		DiscoveryCursors.Remove(OldestCursor);
	}
	const FString Cursor = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	FDiscoveryCursor Snapshot;
	Snapshot.Query = Query;
	Snapshot.Items = Items;
	Snapshot.Offset = Offset;
	Snapshot.ExpiresAt = FDateTime::UtcNow() + DiscoveryCursorLifetime;
	DiscoveryCursors.Add(Cursor, MoveTemp(Snapshot));
	return Cursor;
}

FCodexBridgeResponse FCodexBridgeConsoleService::ShowMessage(
	const TSharedPtr<FJsonObject>& Payload) const
{
	FString Message;
	if (!Payload.IsValid() || !Payload->TryGetStringField(TEXT("message"), Message))
	{
		return Failure(TEXT("message_required"));
	}
	Message.TrimStartAndEndInline();
	if (Message.IsEmpty() || Message.Len() > 8192)
	{
		return Failure(TEXT("message_invalid"));
	}
	UE_LOG(LogCodexBridge, Display, TEXT("Codex: %s"), *Message);
	if (GEngine != nullptr)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,
			8.0f,
			FColor::Cyan,
			FString::Printf(TEXT("Codex: %s"), *Message));
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("displayed"), true);
	return Success(Result);
}

FCodexBridgeResponse FCodexBridgeConsoleService::ReadRecentLog(
	const TSharedPtr<FJsonObject>& Payload) const
{
	double RequestedLines = 200.0;
	if (Payload.IsValid())
	{
		Payload->TryGetNumberField(TEXT("maxLines"), RequestedLines);
	}
	const int32 MaxLines = FMath::Clamp(static_cast<int32>(RequestedLines), 1, MaximumLogLines);
	const FString RecentLogPath = FPaths::Combine(FPaths::ProjectLogDir(), TEXT("FactoryGame.log"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	TUniquePtr<IFileHandle> File(PlatformFile.OpenRead(*RecentLogPath));
	if (!File.IsValid())
	{
		return Failure(TEXT("log_unavailable"));
	}
	const int64 Size = File->Size();
	const int64 ReadSize = FMath::Min(Size, MaximumLogBytes);
	if (!File->Seek(Size - ReadSize))
	{
		return Failure(TEXT("log_seek_failed"));
	}
	TArray<uint8> Bytes;
	Bytes.SetNumUninitialized(ReadSize + 1);
	if (!File->Read(Bytes.GetData(), ReadSize))
	{
		return Failure(TEXT("log_read_failed"));
	}
	Bytes[ReadSize] = 0;
	FString Text = UTF8_TO_TCHAR(reinterpret_cast<const char*>(Bytes.GetData()));
	TArray<FString> Lines;
	Text.ParseIntoArrayLines(Lines, false);
	const int32 Start = FMath::Max(0, Lines.Num() - MaxLines);
	TArray<TSharedPtr<FJsonValue>> JsonLines;
	for (int32 Index = Start; Index < Lines.Num(); Index++)
	{
		JsonLines.Add(MakeShared<FJsonValueString>(Lines[Index]));
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("path"), RecentLogPath);
	Result->SetArrayField(TEXT("lines"), JsonLines);
	Result->SetBoolField(TEXT("truncated"), Size > ReadSize || Start > 0);
	return Success(Result);
}

UWorld* FCodexBridgeConsoleService::ResolveWorld() const
{
	if (GEngine == nullptr)
	{
		return nullptr;
	}
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		UWorld* World = Context.World();
		if (World != nullptr && World->IsGameWorld())
		{
			return World;
		}
	}
	return nullptr;
}
