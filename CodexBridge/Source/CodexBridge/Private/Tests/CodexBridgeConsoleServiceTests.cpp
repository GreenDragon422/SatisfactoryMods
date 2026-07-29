#if WITH_AUTOMATION_TESTS

#include "CodexBridgeConsoleService.h"
#include "CodexBridgeTextPageStore.h"

#include "Dom/JsonValue.h"
#include "HAL/IConsoleManager.h"
#include "Misc/AutomationTest.h"

namespace
{
	TAutoConsoleVariable<int32> AutomationVariableA(
		TEXT("CodexBridge.Automation.Alpha"),
		17,
		TEXT("CodexBridge discovery automation variable alpha."));
	TAutoConsoleVariable<int32> AutomationVariableB(
		TEXT("CodexBridge.Automation.Beta"),
		23,
		TEXT("CodexBridge discovery automation variable beta."));
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCodexBridgeDiscoveryPaginationTest,
	"CodexBridge.Console.DiscoveryPagination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodexBridgeDiscoveryPaginationTest::RunTest(const FString& Parameters)
{
	FCodexBridgeConsoleService Service;
	FCodexBridgeRequest FirstRequest;
	FirstRequest.Id = TEXT("discovery-1");
	FirstRequest.Method = TEXT("console.discover");
	FirstRequest.Payload = MakeShared<FJsonObject>();
	FirstRequest.Payload->SetStringField(TEXT("query"), TEXT("CodexBridge.Automation"));
	FirstRequest.Payload->SetNumberField(TEXT("limit"), 1);
	const FCodexBridgeResponse FirstResponse = Service.Handle(FirstRequest);

	TestTrue(TEXT("first page succeeds"), FirstResponse.IsSuccessful);
	const TArray<TSharedPtr<FJsonValue>>& FirstItems =
		FirstResponse.Payload->GetArrayField(TEXT("items"));
	TestEqual(TEXT("first page size"), FirstItems.Num(), 1);
	TestTrue(TEXT("first page has continuation"), FirstResponse.Payload->GetBoolField(TEXT("hasMore")));
	const FString Cursor = FirstResponse.Payload->GetStringField(TEXT("cursor"));
	TestEqual(TEXT("cursor is server-owned random identifier"), Cursor.Len(), 32);

	FCodexBridgeRequest SecondRequest = FirstRequest;
	SecondRequest.Id = TEXT("discovery-2");
	SecondRequest.Payload = MakeShared<FJsonObject>();
	SecondRequest.Payload->SetStringField(TEXT("query"), TEXT("CodexBridge.Automation"));
	SecondRequest.Payload->SetNumberField(TEXT("limit"), 1);
	SecondRequest.Payload->SetStringField(TEXT("cursor"), Cursor);
	const FCodexBridgeResponse SecondResponse = Service.Handle(SecondRequest);
	const TArray<TSharedPtr<FJsonValue>>& SecondItems =
		SecondResponse.Payload->GetArrayField(TEXT("items"));

	TestTrue(TEXT("second page succeeds"), SecondResponse.IsSuccessful);
	TestEqual(TEXT("second page size"), SecondItems.Num(), 1);
	TestNotEqual(
		TEXT("pages contain different commands"),
		FirstItems[0]->AsObject()->GetStringField(TEXT("name")),
		SecondItems[0]->AsObject()->GetStringField(TEXT("name")));
	TestEqual(
		TEXT("entry kind is variable"),
		SecondItems[0]->AsObject()->GetStringField(TEXT("kind")),
		FString(TEXT("variable")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCodexBridgeTextPaginationTest,
	"CodexBridge.Console.TextPagination",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodexBridgeTextPaginationTest::RunTest(const FString& Parameters)
{
	FCodexBridgeTextPageStore Store;
	const FString Source = FString::ChrN(150000, TEXT('x'));
	const FCodexBridgeTextPage First = Store.Store(Source);
	const FCodexBridgeTextPage Second = Store.Read(First.ResultId, First.Cursor);
	const FCodexBridgeTextPage Replayed = Store.Read(First.ResultId, First.Cursor);
	const FCodexBridgeTextPage Third = Store.Read(Second.ResultId, Second.Cursor);

	TestTrue(TEXT("first page succeeds"), First.IsSuccessful);
	TestTrue(TEXT("first page has continuation"), First.HasMore);
	TestTrue(TEXT("second page succeeds"), Second.IsSuccessful);
	TestTrue(TEXT("second page has continuation"), Second.HasMore);
	TestFalse(TEXT("cursor cannot be replayed"), Replayed.IsSuccessful);
	TestTrue(TEXT("third page succeeds"), Third.IsSuccessful);
	TestFalse(TEXT("third page is final"), Third.HasMore);
	TestEqual(
		TEXT("all text is retained"),
		First.Text + Second.Text + Third.Text,
		Source);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCodexBridgeCommandValidationTest,
	"CodexBridge.Console.CommandValidation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodexBridgeCommandValidationTest::RunTest(const FString& Parameters)
{
	FCodexBridgeConsoleService Service;
	FCodexBridgeRequest Request;
	Request.Id = TEXT("execute-invalid");
	Request.Method = TEXT("console.execute");
	Request.Payload = MakeShared<FJsonObject>();
	const FCodexBridgeResponse Response = Service.Handle(Request);

	TestFalse(TEXT("missing command is rejected"), Response.IsSuccessful);
	TestEqual(TEXT("error is stable"), Response.Error, FString(TEXT("command_required")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FCodexBridgeChatDeliveryDeduplicationTest,
	"CodexBridge.Conversation.DeliveryDeduplication",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FCodexBridgeChatDeliveryDeduplicationTest::RunTest(const FString& Parameters)
{
	FCodexBridgeConsoleService Service;
	FCodexBridgeRequest Request;
	Request.Id = TEXT("delivery-1");
	Request.Method = TEXT("chat.deliver");
	Request.Payload = MakeShared<FJsonObject>();
	Request.Payload->SetStringField(TEXT("eventMethod"), TEXT("chat.thread_reset"));
	Request.Payload->SetObjectField(TEXT("eventPayload"), MakeShared<FJsonObject>());

	const FCodexBridgeResponse FirstResponse = Service.Handle(Request);
	const FCodexBridgeResponse DuplicateResponse = Service.Handle(Request);

	TestTrue(TEXT("first delivery succeeds"), FirstResponse.IsSuccessful);
	TestFalse(TEXT("first delivery is not duplicate"), FirstResponse.Payload->GetBoolField(TEXT("duplicate")));
	TestTrue(TEXT("duplicate delivery succeeds"), DuplicateResponse.IsSuccessful);
	TestTrue(TEXT("duplicate is acknowledged without redisplay"), DuplicateResponse.Payload->GetBoolField(TEXT("duplicate")));
	return true;
}

#endif
