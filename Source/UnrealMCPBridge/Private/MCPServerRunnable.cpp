#include "MCPServerRunnable.h"
#include "EpicUnrealMCPBridge.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "HAL/PlatformProcess.h"

FMCPServerRunnable::FMCPServerRunnable(UEpicUnrealMCPBridge* InBridge, TSharedPtr<FSocket> InListenerSocket)
	: Bridge(InBridge)
	, ListenerSocket(InListenerSocket)
	, bRunning(true)
{
	UE_LOG(LogTemp, Display, TEXT("MCPServerRunnable: Created server runnable"));
}

FMCPServerRunnable::~FMCPServerRunnable()
{
}

bool FMCPServerRunnable::Init()
{
	return true;
}

uint32 FMCPServerRunnable::Run()
{
	UE_LOG(LogTemp, Display, TEXT("UnrealMCP: Server thread starting..."));

	while (bRunning)
	{
		// Completion/timeout of the in-flight game-thread command (never
		// blocks — this is what keeps the fast path alive during hangs).
		ServicePendingCommand();

		// A client that is owed a game-thread response keeps its connection: accepting a
		// newcomer here used to CLOSE the previous socket, so any second client polling the
		// bridge (health watchers connect every few seconds) beheaded every command slower
		// than its poll interval — the caller saw "Connection closed after 0/4 bytes" while
		// the editor finished the work and had nowhere to send it (2026-08-07, a full evening
		// of asset-loading commands lost to a 3s get_pie_status poller). Newcomers wait in
		// the OS listen backlog until the response is delivered.
		const bool bClientOwedResponse =
			bHasPending && PendingSocket.IsValid() && PendingSocket == ClientSocket;

		bool bPending = false;
		if (!bClientOwedResponse && ListenerSocket->HasPendingConnection(bPending) && bPending)
		{
			// If we already have an idle client, close it to make room for the new one.
			// The old client's Python side will reconnect automatically on its next command.
			if (ClientSocket.IsValid())
			{
				UE_LOG(LogTemp, Display, TEXT("UnrealMCP: New client connecting, closing previous connection"));
				ClientSocket->Close();
				ClientSocket.Reset();
			}
			ResetRecvState();

			ClientSocket = MakeShareable(ListenerSocket->Accept(TEXT("MCPClient")));
			if (ClientSocket.IsValid())
			{
				UE_LOG(LogTemp, Display, TEXT("UnrealMCP: Client connected"));

				ClientSocket->SetNoDelay(true);
				ClientSocket->SetNonBlocking(true);
				int32 SocketBufferSize = 4 * 1024 * 1024;
				ClientSocket->SetSendBufferSize(SocketBufferSize, SocketBufferSize);
				ClientSocket->SetReceiveBufferSize(SocketBufferSize, SocketBufferSize);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("UnrealMCP: Failed to accept client connection"));
			}
		}

		// Service the current client (non-blocking)
		if (ClientSocket.IsValid())
		{
			// Read any available data into the accumulation buffer
			uint8 TempBuf[8192];
			int32 BytesRead = 0;

			if (ClientSocket->Recv(TempBuf, sizeof(TempBuf), BytesRead))
			{
				// Recv returns true with BytesRead > 0 when data arrives.
				// Recv returns true with BytesRead == 0 for EWOULDBLOCK on
				// streaming sockets (no data available — NOT a disconnect).
				// Graceful disconnect (recv returning 0) makes Recv return
				// false for streaming sockets, handled in the else branch.
				if (BytesRead > 0)
				{
					RecvBuffer.Append(TempBuf, BytesRead);
				}
			}
			else
			{
				int32 LastError = (int32)ISocketSubsystem::Get()->GetLastErrorCode();

				if (LastError == SE_EWOULDBLOCK)
				{
					// No data available — normal for non-blocking sockets
				}
				else if (LastError == SE_EINTR)
				{
					// Interrupted, just retry next loop
				}
				else
				{
					// Real error or graceful disconnect
					UE_LOG(LogTemp, Display, TEXT("UnrealMCP: Client disconnected (error %d)"), LastError);
					ClientSocket->Close();
					ClientSocket.Reset();
					ResetRecvState();
				}
			}

			// Check if we have a complete length-prefixed message
			FString ReceivedText;
			if (TryExtractMessage(ReceivedText))
			{
				TSharedPtr<FJsonObject> JsonObject;
				TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReceivedText);

				if (FJsonSerializer::Deserialize(Reader, JsonObject))
				{
					FString CommandType;
					if (JsonObject->TryGetStringField(TEXT("type"), CommandType))
					{
						UE_LOG(LogTemp, Display, TEXT("UnrealMCP: Executing: %s"), *CommandType);

						if (Bridge->IsFastPathCommand(CommandType))
						{
							// Answers inline on this thread — available even
							// while a game-thread command is pending/hung.
							const FString Response = Bridge->ExecuteCommand(
								CommandType, JsonObject->GetObjectField(TEXT("params")));
							if (!SendFramedResponse(ClientSocket, Response, CommandType))
							{
								ClientSocket->Close();
								ClientSocket.Reset();
								ResetRecvState();
							}
						}
						else if (bHasPending)
						{
							// Single in-flight game-thread command by design.
							const FString Busy = FString::Printf(
								TEXT("{\"status\":\"error\",\"error\":\"bridge busy: '%s' still executing on the game thread\",\"retryable\":true}"),
								*PendingCommandType);
							if (!SendFramedResponse(ClientSocket, Busy, CommandType))
							{
								ClientSocket->Close();
								ClientSocket.Reset();
								ResetRecvState();
							}
						}
						else
						{
							bHasPending = true;
							PendingCommandType = CommandType;
							PendingStartTime = FPlatformTime::Seconds();
							PendingSocket = ClientSocket;
							PendingFuture = Bridge->DispatchCommandAsync(
								CommandType, JsonObject->GetObjectField(TEXT("params")));
						}
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("UnrealMCP: Missing 'type' field in command"));
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning,
						TEXT("UnrealMCP: Failed to parse JSON (%d chars)"),
						ReceivedText.Len());
					// Protocol is corrupted — reset and let Python reconnect
					ClientSocket->Close();
					ClientSocket.Reset();
					ResetRecvState();
				}
			}
		}

		FPlatformProcess::Sleep(0.002f);
	}

	UE_LOG(LogTemp, Display, TEXT("UnrealMCP: Server thread stopping"));
	return 0;
}

void FMCPServerRunnable::Stop()
{
	bRunning = false;
}

void FMCPServerRunnable::Exit()
{
}

void FMCPServerRunnable::ResetRecvState()
{
	RecvBuffer.Reset();
	RecvBufferReadOffset = 0;
}

bool FMCPServerRunnable::TryExtractMessage(FString& OutMessage)
{
	const int32 Available = RecvBuffer.Num() - RecvBufferReadOffset;

	// Need at least the 4-byte length header
	if (Available < 4)
	{
		return false;
	}

	// Read big-endian payload length from the header
	const uint8* HeaderPtr = RecvBuffer.GetData() + RecvBufferReadOffset;
	const int32 PayloadSize =
		(static_cast<int32>(HeaderPtr[0]) << 24) |
		(static_cast<int32>(HeaderPtr[1]) << 16) |
		(static_cast<int32>(HeaderPtr[2]) << 8) |
		static_cast<int32>(HeaderPtr[3]);

	// Sanity check — reject obviously corrupt headers
	if (PayloadSize <= 0 || PayloadSize > 10 * 1024 * 1024)
	{
		UE_LOG(LogTemp, Error,
			TEXT("UnrealMCP: Invalid payload size %d — resetting connection"),
			PayloadSize);
		// Force disconnect so the client reconnects with a clean stream
		ClientSocket->Close();
		ClientSocket.Reset();
		ResetRecvState();
		return false;
	}

	// Check if the full payload has arrived
	if (Available < 4 + PayloadSize)
	{
		return false;
	}

	// Extract the payload as a UTF-8 string
	const uint8* PayloadPtr = HeaderPtr + 4;
	FUTF8ToTCHAR Converter(
		reinterpret_cast<const ANSICHAR*>(PayloadPtr), PayloadSize);
	OutMessage = FString(Converter.Length(), Converter.Get());

	// Advance past the consumed header + payload
	RecvBufferReadOffset += 4 + PayloadSize;

	// Compact the buffer once the consumed portion is large enough
	if (RecvBufferReadOffset > 4096)
	{
		RecvBuffer.RemoveAt(0, RecvBufferReadOffset);
		RecvBufferReadOffset = 0;
	}

	return true;
}

void FMCPServerRunnable::ServicePendingCommand()
{
	if (!bHasPending)
	{
		return;
	}

	if (PendingFuture.IsReady())
	{
		const FString Response = PendingFuture.Get();
		if (PendingSocket.IsValid() && PendingSocket == ClientSocket)
		{
			if (!SendFramedResponse(PendingSocket, Response, PendingCommandType))
			{
				ClientSocket->Close();
				ClientSocket.Reset();
				ResetRecvState();
			}
		}
		else
		{
			// The requesting client was replaced/disconnected while its
			// command ran; nowhere to deliver the result.
			UE_LOG(LogTemp, Warning,
				TEXT("UnrealMCP: dropping result of '%s' — client gone"),
				*PendingCommandType);
		}
		bHasPending = false;
		PendingFuture = TFuture<FString>();
		PendingSocket.Reset();
		return;
	}

	constexpr double TimeoutSeconds = 120.0;
	if (FPlatformTime::Seconds() - PendingStartTime > TimeoutSeconds)
	{
		UE_LOG(LogTemp, Error,
			TEXT("UnrealMCP: Command '%s' timed out after %.0fs waiting for game thread"),
			*PendingCommandType, TimeoutSeconds);

		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetStringField(TEXT("command"), PendingCommandType);
		Payload->SetNumberField(TEXT("timeout_seconds"), TimeoutSeconds);
		Bridge->AddBridgeEvent(TEXT("command_timeout"), Payload);

		if (PendingSocket.IsValid() && PendingSocket == ClientSocket)
		{
			SendFramedResponse(PendingSocket,
				TEXT("{\"status\":\"error\",\"error\":\"Command timed out waiting for game thread\"}"),
				PendingCommandType);
		}
		// Abandon the future — if the game thread ever finishes the lambda,
		// the result has no reader and is discarded.
		bHasPending = false;
		PendingFuture = TFuture<FString>();
		PendingSocket.Reset();
	}
}

bool FMCPServerRunnable::SendFramedResponse(
	const TSharedPtr<FSocket>& Socket, const FString& Response, const FString& CommandType)
{
	if (!Socket.IsValid())
	{
		return false;
	}

	FTCHARToUTF8 UTF8Response(*Response);
	const uint8* JsonData = (const uint8*)UTF8Response.Get();
	const int32 JsonSize = UTF8Response.Length();

	// Length-prefix framing: 4-byte big-endian header + JSON payload
	uint8 LengthHeader[4];
	LengthHeader[0] = (uint8)((JsonSize >> 24) & 0xFF);
	LengthHeader[1] = (uint8)((JsonSize >> 16) & 0xFF);
	LengthHeader[2] = (uint8)((JsonSize >> 8) & 0xFF);
	LengthHeader[3] = (uint8)(JsonSize & 0xFF);

	if (SendAllBytes(Socket, LengthHeader, 4) && SendAllBytes(Socket, JsonData, JsonSize))
	{
		UE_LOG(LogTemp, Display,
			TEXT("UnrealMCP: Sent %d bytes for %s"), JsonSize, *CommandType);
		return true;
	}

	UE_LOG(LogTemp, Error,
		TEXT("UnrealMCP: Failed to send response for %s"), *CommandType);
	return false;
}

bool FMCPServerRunnable::SendAllBytes(const TSharedPtr<FSocket>& Socket, const uint8* Data, int32 Size)
{
	int32 TotalSent = 0;
	int32 StallCount = 0;
	constexpr int32 MaxStalls = 500; // ~5 seconds max wait

	while (TotalSent < Size)
	{
		int32 BytesSent = 0;
		bool bOk = Socket->Send(Data + TotalSent, Size - TotalSent, BytesSent);

		if (bOk && BytesSent > 0)
		{
			TotalSent += BytesSent;
			StallCount = 0;
		}
		else
		{
			int32 LastError = (int32)ISocketSubsystem::Get()->GetLastErrorCode();
			if (LastError == SE_EWOULDBLOCK || BytesSent == 0)
			{
				if (++StallCount >= MaxStalls)
				{
					UE_LOG(LogTemp, Error,
						TEXT("UnrealMCP: Send stalled after %d/%d bytes"),
						TotalSent, Size);
					return false;
				}
				FPlatformProcess::Sleep(0.01f);
				continue;
			}

			UE_LOG(LogTemp, Error,
				TEXT("UnrealMCP: Send failed (error %d) after %d/%d bytes"),
				LastError, TotalSent, Size);
			return false;
		}
	}
	return true;
}
