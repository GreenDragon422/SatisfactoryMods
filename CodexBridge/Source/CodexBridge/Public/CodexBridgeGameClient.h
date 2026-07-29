#pragma once

#include "CoreMinimal.h"
#include "CodexBridgeRequest.h"
#include "HAL/Runnable.h"
#include "HAL/ThreadSafeCounter.h"
#include "Containers/Queue.h"

class FRunnableThread;

class CODEXBRIDGE_API FCodexBridgeGameClient final : public FRunnable
{
public:
	FCodexBridgeGameClient();
	virtual ~FCodexBridgeGameClient() override;

	void Start();
	virtual uint32 Run() override;
	virtual void Stop() override;
	void Shutdown();

	bool IsConnected() const;
	FString GetConnectionDescription() const;
	bool DequeueRequest(FCodexBridgeRequest& Request);
	void EnqueueResponse(const FCodexBridgeRequest& Request, const FCodexBridgeResponse& Response);
	bool EnqueueEvent(const FString& Method, const TSharedPtr<FJsonObject>& Payload);

private:
	bool TryConnectAndHandshake();
	bool TryOpenPipe();
	bool PerformHandshake();
	bool TryReadEnvelope(TSharedPtr<FJsonObject>& Envelope);
	bool ReadFrame(TSharedPtr<FJsonObject>& Envelope);
	bool WriteEnvelope(const TSharedPtr<FJsonObject>& Envelope);
	bool WriteBytes(const uint8* Data, uint32 Length);
	bool ReadBytes(uint8* Data, uint32 Length);
	bool CompleteOverlapped(void* Handle, void* Overlapped, uint32& Transferred);
	void DrainOutbound();
	void SendHeartbeat();
	void LaunchServiceIfNeeded();
	void CancelPendingIo();
	void ClosePipe();
	void SetConnectionState(bool Connected, const FString& Description);
	FString GetTokenPath() const;
	FString GetCompanionPath() const;

	FRunnableThread* Thread = nullptr;
	TAtomic<bool> StopRequested = false;
	void* PipeHandle = nullptr;
	mutable FCriticalSection StateMutex;
	mutable FCriticalSection PipeMutex;
	bool Connected = false;
	bool ServiceLaunchNeeded = false;
	FString ConnectionDescription = TEXT("starting");
	FString ClientInstanceId;
	int64 ConnectionGeneration = 0;
	double LastServiceLaunchSeconds = -1000.0;
	double LastHeartbeatSeconds = 0.0;
	TQueue<FCodexBridgeRequest, EQueueMode::Mpsc> InboundRequests;
	TQueue<TSharedPtr<FJsonObject>, EQueueMode::Mpsc> OutboundEnvelopes;
	FThreadSafeCounter InboundRequestCount;
	FThreadSafeCounter OutboundEnvelopeCount;
};
