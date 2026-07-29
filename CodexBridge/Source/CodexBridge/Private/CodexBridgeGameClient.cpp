#include "CodexBridgeGameClient.h"

#include "CodexBridgeLog.h"
#include "Dom/JsonObject.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "HAL/RunnableThread.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include "Windows/AllowWindowsPlatformTypes.h"
#include <Windows.h>
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	constexpr uint32 ProtocolVersion = 1;
	constexpr uint32 MaximumFrameBytes = 1024 * 1024;
	constexpr double RetryDelaySeconds = 1.0;
	constexpr double HeartbeatIntervalSeconds = 5.0;
	constexpr double IoTimeoutSeconds = 5.0;
	constexpr int32 QueueCapacity = 64;
	const TCHAR* PipePath = TEXT("\\\\.\\pipe\\CodexBridge-v1");
}

FCodexBridgeGameClient::FCodexBridgeGameClient()
	: ClientInstanceId(FGuid::NewGuid().ToString(EGuidFormats::Digits))
{
}

FCodexBridgeGameClient::~FCodexBridgeGameClient()
{
	Shutdown();
}

void FCodexBridgeGameClient::Start()
{
	if (Thread == nullptr)
	{
		StopRequested.Store(false);
		Thread = FRunnableThread::Create(this, TEXT("CodexBridge.GameClient"));
	}
}

uint32 FCodexBridgeGameClient::Run()
{
	while (!StopRequested.Load())
	{
		if (!TryConnectAndHandshake())
		{
			if (ServiceLaunchNeeded)
			{
				LaunchServiceIfNeeded();
			}
			FPlatformProcess::Sleep(static_cast<float>(RetryDelaySeconds));
			continue;
		}

		LastHeartbeatSeconds = FPlatformTime::Seconds();
		while (!StopRequested.Load() && IsConnected())
		{
			DrainOutbound();
			TSharedPtr<FJsonObject> Envelope;
			if (TryReadEnvelope(Envelope))
			{
				FString Kind;
				FString RequestId;
				FString Method;
				if (!Envelope->TryGetStringField(TEXT("kind"), Kind) ||
					!Envelope->TryGetStringField(TEXT("id"), RequestId) ||
					!Envelope->TryGetStringField(TEXT("method"), Method))
				{
					SetConnectionState(false, TEXT("invalid broker envelope"));
					continue;
				}
				if (Kind == TEXT("request") || Kind == TEXT("event"))
				{
					FCodexBridgeRequest Request;
					Request.Id = RequestId;
					Request.Method = Method;
					Request.ExpectsResponse = Kind == TEXT("request");
					const TSharedPtr<FJsonObject>* RequestPayload = nullptr;
					Request.Payload = Envelope->TryGetObjectField(TEXT("payload"), RequestPayload) &&
						RequestPayload != nullptr
						? *RequestPayload
						: MakeShared<FJsonObject>();
					if (InboundRequestCount.Increment() <= QueueCapacity)
					{
						InboundRequests.Enqueue(MoveTemp(Request));
					}
					else
					{
						InboundRequestCount.Decrement();
						if (Request.ExpectsResponse)
						{
							FCodexBridgeResponse BusyResponse;
							BusyResponse.Error = TEXT("busy");
							BusyResponse.Payload = MakeShared<FJsonObject>();
							EnqueueResponse(Request, BusyResponse);
						}
						else
						{
							UE_LOG(LogCodexBridge, Warning, TEXT("Inbound event queue is full; event %s was dropped."), *Method);
						}
					}
				}
			}

			const double Now = FPlatformTime::Seconds();
			if (Now - LastHeartbeatSeconds >= HeartbeatIntervalSeconds)
			{
				SendHeartbeat();
				LastHeartbeatSeconds = Now;
			}

			FPlatformProcess::Sleep(0.02f);
		}

		ClosePipe();
		SetConnectionState(false, TEXT("disconnected; retrying"));
	}

	ClosePipe();
	SetConnectionState(false, TEXT("stopped"));
	return 0;
}

void FCodexBridgeGameClient::Stop()
{
	StopRequested.Store(true);
	CancelPendingIo();
}

void FCodexBridgeGameClient::Shutdown()
{
	Stop();
	if (Thread != nullptr)
	{
		Thread->WaitForCompletion();
		delete Thread;
		Thread = nullptr;
	}
}

bool FCodexBridgeGameClient::IsConnected() const
{
	FScopeLock Lock(&StateMutex);
	return Connected;
}

FString FCodexBridgeGameClient::GetConnectionDescription() const
{
	FScopeLock Lock(&StateMutex);
	return ConnectionDescription;
}

bool FCodexBridgeGameClient::DequeueRequest(FCodexBridgeRequest& Request)
{
	if (!InboundRequests.Dequeue(Request))
	{
		return false;
	}
	InboundRequestCount.Decrement();
	return true;
}

void FCodexBridgeGameClient::EnqueueResponse(
	const FCodexBridgeRequest& Request,
	const FCodexBridgeResponse& Response)
{
	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetNumberField(TEXT("version"), ProtocolVersion);
	Envelope->SetStringField(TEXT("kind"), TEXT("response"));
	Envelope->SetStringField(TEXT("id"), Request.Id);
	Envelope->SetStringField(TEXT("role"), TEXT("game"));
	Envelope->SetStringField(TEXT("method"), Request.Method);
	Envelope->SetObjectField(
		TEXT("payload"),
		Response.Payload.IsValid() ? Response.Payload : MakeShared<FJsonObject>());
	Envelope->SetBoolField(TEXT("ok"), Response.IsSuccessful);
	if (!Response.IsSuccessful)
	{
		Envelope->SetStringField(TEXT("error"), Response.Error);
	}
	if (OutboundEnvelopeCount.Increment() <= QueueCapacity)
	{
		OutboundEnvelopes.Enqueue(Envelope);
	}
	else
	{
		OutboundEnvelopeCount.Decrement();
		UE_LOG(LogCodexBridge, Error, TEXT("Outbound response queue is full; request %s cannot be answered."), *Request.Id);
	}
}

bool FCodexBridgeGameClient::EnqueueEvent(
	const FString& Method,
	const TSharedPtr<FJsonObject>& Payload)
{
	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetNumberField(TEXT("version"), ProtocolVersion);
	Envelope->SetStringField(TEXT("kind"), TEXT("event"));
	Envelope->SetStringField(TEXT("id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Envelope->SetStringField(TEXT("role"), TEXT("game"));
	Envelope->SetStringField(TEXT("method"), Method);
	Envelope->SetObjectField(TEXT("payload"), Payload);
	if (OutboundEnvelopeCount.Increment() <= QueueCapacity)
	{
		OutboundEnvelopes.Enqueue(Envelope);
		return true;
	}
	OutboundEnvelopeCount.Decrement();
	UE_LOG(LogCodexBridge, Warning, TEXT("Outbound event queue is full; event %s was not queued."), *Method);
	return false;
}

bool FCodexBridgeGameClient::TryConnectAndHandshake()
{
	ServiceLaunchNeeded = false;
	SetConnectionState(false, TEXT("connecting"));
	if (!TryOpenPipe())
	{
		ServiceLaunchNeeded = true;
		return false;
	}
	if (!PerformHandshake())
	{
		ClosePipe();
		return false;
	}

	SetConnectionState(
		true,
		FString::Printf(TEXT("connected (generation %lld)"), ConnectionGeneration));
	UE_LOG(LogCodexBridge, Log, TEXT("Connected to local broker as game generation %lld."), ConnectionGeneration);
	return true;
}

bool FCodexBridgeGameClient::TryOpenPipe()
{
	HANDLE Handle = CreateFileW(
		PipePath,
		GENERIC_READ | GENERIC_WRITE,
		0,
		nullptr,
		OPEN_EXISTING,
		FILE_FLAG_OVERLAPPED,
		nullptr);
	if (Handle == INVALID_HANDLE_VALUE)
	{
		return false;
	}

	FScopeLock Lock(&PipeMutex);
	PipeHandle = Handle;
	return true;
}

bool FCodexBridgeGameClient::PerformHandshake()
{
	FString Token;
	if (!FFileHelper::LoadFileToString(Token, *GetTokenPath()))
	{
		return false;
	}
	Token.TrimStartAndEndInline();
	if (Token.Len() != 64)
	{
		return false;
	}

	ConnectionGeneration++;
	const FString RequestId = FGuid::NewGuid().ToString(EGuidFormats::Digits);
	TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
	Payload->SetStringField(TEXT("token"), Token);
	Payload->SetStringField(TEXT("clientInstanceId"), ClientInstanceId);
	Payload->SetNumberField(TEXT("processId"), FPlatformProcess::GetCurrentProcessId());
	Payload->SetStringField(TEXT("executablePath"), FPlatformProcess::ExecutablePath());
	Payload->SetNumberField(TEXT("generation"), static_cast<double>(ConnectionGeneration));
	TSharedPtr<FJsonObject> Hello = MakeShared<FJsonObject>();
	Hello->SetNumberField(TEXT("version"), ProtocolVersion);
	Hello->SetStringField(TEXT("kind"), TEXT("hello"));
	Hello->SetStringField(TEXT("id"), RequestId);
	Hello->SetStringField(TEXT("role"), TEXT("game"));
	Hello->SetStringField(TEXT("method"), TEXT("hello"));
	Hello->SetObjectField(TEXT("payload"), Payload);
	if (!WriteEnvelope(Hello))
	{
		return false;
	}

	const double Deadline = FPlatformTime::Seconds() + 5.0;
	while (!StopRequested.Load() && FPlatformTime::Seconds() < Deadline)
	{
		TSharedPtr<FJsonObject> Response;
		if (TryReadEnvelope(Response))
		{
			bool IsAccepted = false;
			if (Response->TryGetBoolField(TEXT("ok"), IsAccepted) && IsAccepted)
			{
				return true;
			}
			FString Error;
			Response->TryGetStringField(TEXT("error"), Error);
			SetConnectionState(
				false,
				Error.IsEmpty() ? TEXT("handshake rejected") : FString::Printf(TEXT("handshake rejected: %s"), *Error));
			return false;
		}

		FPlatformProcess::Sleep(0.02f);
	}

	return false;
}

bool FCodexBridgeGameClient::TryReadEnvelope(TSharedPtr<FJsonObject>& Envelope)
{
	HANDLE Handle;
	{
		FScopeLock Lock(&PipeMutex);
		Handle = static_cast<HANDLE>(PipeHandle);
	}
	if (Handle == nullptr)
	{
		return false;
	}

	DWORD AvailableBytes = 0;
	if (!PeekNamedPipe(Handle, nullptr, 0, nullptr, &AvailableBytes, nullptr))
	{
		SetConnectionState(false, TEXT("pipe read failed"));
		return false;
	}
	if (AvailableBytes < 4)
	{
		return false;
	}

	return ReadFrame(Envelope);
}

bool FCodexBridgeGameClient::ReadFrame(TSharedPtr<FJsonObject>& Envelope)
{
	uint8 Header[4];
	if (!ReadBytes(Header, 4))
	{
		return false;
	}
	const uint32 Length = static_cast<uint32>(Header[0]) |
		(static_cast<uint32>(Header[1]) << 8) |
		(static_cast<uint32>(Header[2]) << 16) |
		(static_cast<uint32>(Header[3]) << 24);
	if (Length == 0 || Length > MaximumFrameBytes)
	{
		SetConnectionState(false, TEXT("invalid frame length"));
		return false;
	}

	TArray<uint8> Data;
	Data.SetNumUninitialized(Length + 1);
	if (!ReadBytes(Data.GetData(), Length))
	{
		return false;
	}
	Data[Length] = 0;
	const FString Json = UTF8_TO_TCHAR(reinterpret_cast<const char*>(Data.GetData()));
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
	if (!FJsonSerializer::Deserialize(Reader, Envelope) || !Envelope.IsValid())
	{
		SetConnectionState(false, TEXT("invalid frame JSON"));
		return false;
	}
	return true;
}

bool FCodexBridgeGameClient::WriteEnvelope(const TSharedPtr<FJsonObject>& Envelope)
{
	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	if (!FJsonSerializer::Serialize(Envelope.ToSharedRef(), Writer))
	{
		return false;
	}
	FTCHARToUTF8 Utf8(*Json);
	const uint32 Length = static_cast<uint32>(Utf8.Length());
	if (Length == 0 || Length > MaximumFrameBytes)
	{
		return false;
	}
	uint8 Header[4] = {
		static_cast<uint8>(Length & 0xff),
		static_cast<uint8>((Length >> 8) & 0xff),
		static_cast<uint8>((Length >> 16) & 0xff),
		static_cast<uint8>((Length >> 24) & 0xff)
	};
	return WriteBytes(Header, 4) &&
		WriteBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Length);
}

bool FCodexBridgeGameClient::WriteBytes(const uint8* Data, uint32 Length)
{
	HANDLE Handle;
	{
		FScopeLock Lock(&PipeMutex);
		Handle = static_cast<HANDLE>(PipeHandle);
	}
	uint32 Offset = 0;
	while (Handle != nullptr && Offset < Length)
	{
		OVERLAPPED Overlapped = {};
		Overlapped.hEvent = CreateEventW(nullptr, 1, 0, nullptr);
		if (Overlapped.hEvent == nullptr)
		{
			return false;
		}
		uint32 Written = 0;
		const BOOL Started = WriteFile(
			Handle,
			Data + Offset,
			Length - Offset,
			reinterpret_cast<LPDWORD>(&Written),
			&Overlapped);
		const DWORD Error = Started ? ERROR_SUCCESS : GetLastError();
		const bool Completed = Started ||
			(Error == ERROR_IO_PENDING && CompleteOverlapped(Handle, &Overlapped, Written));
		CloseHandle(Overlapped.hEvent);
		if (!Completed || Written == 0)
		{
			SetConnectionState(false, TEXT("pipe write failed"));
			return false;
		}
		Offset += Written;
	}
	return Offset == Length;
}

bool FCodexBridgeGameClient::ReadBytes(uint8* Data, uint32 Length)
{
	HANDLE Handle;
	{
		FScopeLock Lock(&PipeMutex);
		Handle = static_cast<HANDLE>(PipeHandle);
	}
	uint32 Offset = 0;
	while (Handle != nullptr && Offset < Length)
	{
		OVERLAPPED Overlapped = {};
		Overlapped.hEvent = CreateEventW(nullptr, 1, 0, nullptr);
		if (Overlapped.hEvent == nullptr)
		{
			return false;
		}
		uint32 Read = 0;
		const BOOL Started = ReadFile(
			Handle,
			Data + Offset,
			Length - Offset,
			reinterpret_cast<LPDWORD>(&Read),
			&Overlapped);
		const DWORD Error = Started ? ERROR_SUCCESS : GetLastError();
		const bool Completed = Started ||
			(Error == ERROR_IO_PENDING && CompleteOverlapped(Handle, &Overlapped, Read));
		CloseHandle(Overlapped.hEvent);
		if (!Completed || Read == 0)
		{
			SetConnectionState(false, TEXT("pipe read failed"));
			return false;
		}
		Offset += Read;
	}
	return Offset == Length;
}

bool FCodexBridgeGameClient::CompleteOverlapped(
	void* RawHandle,
	void* RawOverlapped,
	uint32& Transferred)
{
	HANDLE Handle = static_cast<HANDLE>(RawHandle);
	OVERLAPPED* Overlapped = static_cast<OVERLAPPED*>(RawOverlapped);
	const double Deadline = FPlatformTime::Seconds() + IoTimeoutSeconds;
	while (!StopRequested.Load() && FPlatformTime::Seconds() < Deadline)
	{
		const DWORD WaitResult = WaitForSingleObject(Overlapped->hEvent, 100);
		if (WaitResult == WAIT_OBJECT_0)
		{
			DWORD NativeTransferred = 0;
			if (!GetOverlappedResult(Handle, Overlapped, &NativeTransferred, 0))
			{
				return false;
			}
			Transferred = NativeTransferred;
			return true;
		}
		if (WaitResult == WAIT_FAILED)
		{
			return false;
		}
	}

	CancelIoEx(Handle, Overlapped);
	WaitForSingleObject(Overlapped->hEvent, INFINITE);
	return false;
}

void FCodexBridgeGameClient::DrainOutbound()
{
	TSharedPtr<FJsonObject> Envelope;
	while (OutboundEnvelopes.Dequeue(Envelope))
	{
		OutboundEnvelopeCount.Decrement();
		if (!WriteEnvelope(Envelope))
		{
			return;
		}
	}
}

void FCodexBridgeGameClient::SendHeartbeat()
{
	TSharedPtr<FJsonObject> Envelope = MakeShared<FJsonObject>();
	Envelope->SetNumberField(TEXT("version"), ProtocolVersion);
	Envelope->SetStringField(TEXT("kind"), TEXT("heartbeat"));
	Envelope->SetStringField(TEXT("id"), FGuid::NewGuid().ToString(EGuidFormats::Digits));
	Envelope->SetStringField(TEXT("role"), TEXT("game"));
	Envelope->SetStringField(TEXT("method"), TEXT("heartbeat"));
	Envelope->SetObjectField(TEXT("payload"), MakeShared<FJsonObject>());
	WriteEnvelope(Envelope);
}

void FCodexBridgeGameClient::LaunchServiceIfNeeded()
{
	const double Now = FPlatformTime::Seconds();
	if (Now - LastServiceLaunchSeconds < 8.0)
	{
		return;
	}
	LastServiceLaunchSeconds = Now;
	const FString CompanionPath = GetCompanionPath();
	if (!FPaths::FileExists(CompanionPath))
	{
		SetConnectionState(false, FString::Printf(TEXT("companion missing: %s"), *CompanionPath));
		return;
	}

	uint32 ProcessId = 0;
	FProcHandle Process = FPlatformProcess::CreateProc(
		*CompanionPath,
		TEXT("--service"),
		true,
		true,
		true,
		&ProcessId,
		0,
		*FPaths::GetPath(CompanionPath),
		nullptr,
		nullptr);
	if (Process.IsValid())
	{
		FPlatformProcess::CloseProc(Process);
		SetConnectionState(false, TEXT("broker launched; connecting"));
		UE_LOG(LogCodexBridge, Log, TEXT("Launched hidden CodexBridge broker process %u."), ProcessId);
	}
	else
	{
		SetConnectionState(false, TEXT("failed to launch broker"));
	}
}

void FCodexBridgeGameClient::CancelPendingIo()
{
	FScopeLock Lock(&PipeMutex);
	HANDLE Handle = static_cast<HANDLE>(PipeHandle);
	if (Handle != nullptr && Handle != INVALID_HANDLE_VALUE)
	{
		CancelIoEx(Handle, nullptr);
	}
}

void FCodexBridgeGameClient::ClosePipe()
{
	FScopeLock Lock(&PipeMutex);
	HANDLE Handle = static_cast<HANDLE>(PipeHandle);
	PipeHandle = nullptr;
	if (Handle != nullptr && Handle != INVALID_HANDLE_VALUE)
	{
		CancelIoEx(Handle, nullptr);
		CloseHandle(Handle);
	}
}

void FCodexBridgeGameClient::SetConnectionState(
	bool IsNowConnected,
	const FString& Description)
{
	FScopeLock Lock(&StateMutex);
	Connected = IsNowConnected;
	ConnectionDescription = Description;
}

FString FCodexBridgeGameClient::GetTokenPath() const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("CodexBridge"), TEXT("broker.token"));
}

FString FCodexBridgeGameClient::GetCompanionPath() const
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("CodexBridge"));
	if (!Plugin.IsValid())
	{
		return FString();
	}
	return FPaths::Combine(
		Plugin->GetBaseDir(),
		TEXT("Resources"),
		TEXT("CodexBridge.Companion.exe"));
}
