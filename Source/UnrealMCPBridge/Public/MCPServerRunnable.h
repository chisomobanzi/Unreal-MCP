#pragma once

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "HAL/Runnable.h"
#include "Sockets.h"
#include "Interfaces/IPv4/IPv4Address.h"

class UEpicUnrealMCPBridge;

/**
 * Runnable class for the MCP server thread
 */
class FMCPServerRunnable : public FRunnable
{
public:
	FMCPServerRunnable(UEpicUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket);
	virtual ~FMCPServerRunnable();

	// FRunnable interface
	virtual bool Init() override;
	virtual uint32 Run() override;
	virtual void Stop() override;
	virtual void Exit() override;

private:
	/** Send all bytes to a socket with EWOULDBLOCK retry. Returns false on fatal error. */
	bool SendAllBytes(const TSharedPtr<FSocket>& Socket, const uint8* Data, int32 Size);

	/** Frame (4-byte big-endian length) and send a JSON response. */
	bool SendFramedResponse(const TSharedPtr<FSocket>& Socket, const FString& Response, const FString& CommandType);

	/** Service the in-flight game-thread command, if any (completion/timeout). */
	void ServicePendingCommand();

	/**
	 * Try to extract a complete length-prefixed message from RecvBuffer.
	 * Returns true and fills OutMessage if a complete message is available.
	 * The consumed bytes are removed from RecvBuffer.
	 */
	bool TryExtractMessage(FString& OutMessage);

	/** Reset receive state (on disconnect or protocol error). */
	void ResetRecvState();

	UEpicUnrealMCPBridge* Bridge;
	TSharedPtr<FSocket> ListenerSocket;
	TSharedPtr<FSocket> ClientSocket;
	bool bRunning;

	// Message accumulation buffer for length-prefix framed requests.
	// Data is appended by non-blocking Recv calls and consumed when
	// a complete message (4-byte header + payload) is available.
	TArray<uint8> RecvBuffer;
	int32 RecvBufferReadOffset = 0;

	// At most one game-thread command is in flight. The socket loop does NOT
	// block on it — it keeps accepting connections and answering fast-path
	// commands (health_check/set_unattended_mode/get_bridge_events/...) so
	// the bridge stays reachable while the game thread is busy or hung.
	bool bHasPending = false;
	TFuture<FString> PendingFuture;
	FString PendingCommandType;
	double PendingStartTime = 0.0;
	TSharedPtr<FSocket> PendingSocket;
};