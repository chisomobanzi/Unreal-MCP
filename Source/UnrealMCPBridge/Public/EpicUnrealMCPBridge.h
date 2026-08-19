#pragma once

#include <atomic>

#include "CoreMinimal.h"
#include "Async/Future.h"
#include "Containers/Ticker.h"
#include "EditorSubsystem.h"
#include "Sockets.h"
#include "SocketSubsystem.h"
#include "Http.h"
#include "Json.h"
#include "Interfaces/IPv4/IPv4Address.h"
#include "Interfaces/IPv4/IPv4Endpoint.h"
#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPBlueprintGraphCommands.h"
#include "Commands/EpicUnrealMCPMaterialCommands.h"
#include "Commands/EpicUnrealMCPDataTableCommands.h"
#include "Commands/EpicUnrealMCPAssetCommands.h"
#include "Commands/EpicUnrealMCPDataAssetCommands.h"
#include "Commands/EpicUnrealMCPWidgetCommands.h"
#include "Commands/EpicUnrealMCPEnhancedInputCommands.h"
#include "EpicUnrealMCPBridge.generated.h"

class FMCPServerRunnable;
class SWindow;

/**
 * Per-command token usage statistics, accumulated while debug mode is active.
 */
struct FMCPCommandTokenStats
{
	int32 CallCount = 0;
	int64 TotalResponseBytes = 0;
	int64 TotalEstimatedTokens = 0;
	int64 MaxResponseBytes = 0;
	int64 MaxEstimatedTokens = 0;
	int64 MinResponseBytes = INT64_MAX;
};

/**
 * Editor subsystem for MCP Bridge
 * Handles communication between external tools and the Unreal Editor
 * through a TCP socket connection. Commands are received as JSON and
 * routed to appropriate command handlers.
 */
UCLASS()
class UNREALMCPBRIDGE_API UEpicUnrealMCPBridge : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	UEpicUnrealMCPBridge();
	virtual ~UEpicUnrealMCPBridge();

	// UEditorSubsystem implementation
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// Server functions
	void StartServer();
	void StopServer();
	bool IsRunning() const { return bIsRunning; }

	// Command execution (synchronous: fast path answers inline, game-thread
	// commands block up to 120s)
	FString ExecuteCommand(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	// True for commands the socket thread answers without the game thread
	// (ping/health_check/set_unattended_mode/get_bridge_events/...). These
	// must stay available while a game-thread command is in flight or hung.
	bool IsFastPathCommand(const FString& CommandType) const;

	// Non-blocking dispatch of a game-thread command. The socket loop uses
	// this so it can keep servicing fast-path commands while the result is
	// pending (and apply its own 120s timeout).
	TFuture<FString> DispatchCommandAsync(const FString& CommandType, const TSharedPtr<FJsonObject>& Params);

	// Returns the port the server is currently listening on
	uint16 GetPort() const { return Port; }

	// Append a structured event to the bridge event ring buffer (thread-safe).
	// Drained by the get_bridge_events fast-path command — readable even while
	// the game thread is blocked, which is the whole point.
	void AddBridgeEvent(const FString& EventType, TSharedPtr<FJsonObject> Payload = nullptr);

private:
	// Slate pre-tick: re-asserts persistent unattended mode and dismisses
	// modal windows. Registered on FSlateApplication::OnPreTick because,
	// unlike FTSTicker, it still fires while a modal loop pumps Slate.
	void OnSlatePreTick(float DeltaTime);

	// Port file management for Python MCP server auto-discovery
	void WritePortFile() const;
	void DeletePortFile() const;

	// Editor readiness cache: written on the game thread (state ticker +
	// on-demand before dispatch), readable from the socket thread for
	// health_check. Guards against executing commands during editor boot.
	void RefreshReadinessCache();

	// Command journal (idempotency-lite): responses of completed commands that
	// carried a client-supplied command_id, so a client whose socket died after
	// sending can ask "did my command run?" (get_command_result) instead of
	// blind-resending — the double-execution hazard the no-retry-after-send
	// rule exists to prevent. Written at ExecuteCommand's slow-path tail and
	// read by the get_command_result fast-path, both on the socket thread; the
	// lock still guards the pair against future multi-threading of the server.
	FCriticalSection JournalLock;
	TMap<FString, FString> CommandJournal;
	TArray<FString> JournalOrder;
	static constexpr int32 JournalCapacity = 128;

	// Token debug: estimate tokens from a JSON result and optionally inject _debug field
	void InjectTokenDebugInfo(
		const FString& CommandType,
		TSharedPtr<FJsonObject>& ResponseJson,
		const TSharedPtr<FJsonObject>& ResultJson);

	// Server state
	bool bIsRunning;
	TSharedPtr<FSocket> ListenerSocket;

	TSharedPtr<FSocket> ConnectionSocket;
	FRunnableThread* ServerThread;

	// Server configuration
	FIPv4Address ServerAddress;
	uint16 Port;

	// Saved editor throttle setting, restored when MCP server stops
	bool bOriginalThrottleSetting;

	// Token estimation debug mode
	bool bDebugTokenEstimation;
	TMap<FString, FMCPCommandTokenStats> TokenStats;

	// Editor readiness snapshot (see RefreshReadinessCache)
	std::atomic<bool> bCachedEditorReady{false};
	mutable FCriticalSection ReadyStateCS;
	FString CachedWorldName;
	FString CachedNotReadyReason;
	bool bCachedLoadingAssets = true;
	bool bCachedPIEActive = false;

	// Game-thread ticker: refreshes the readiness cache and re-asserts the
	// port file if something (e.g. a cook wiping Saved/) deleted it.
	FTSTicker::FDelegateHandle StateTickerHandle;
	uint64 StateTickCount = 0;

	// Non-interactive (unattended) mode: persistent GIsRunningUnattendedScript
	// + modal auto-dismiss. Set from the socket thread, applied on the game
	// thread by OnSlatePreTick.
	std::atomic<bool> bUnattendedMode{false};
	std::atomic<bool> bDismissModals{true};
	// Game-thread-only sentinel state. While unattended mode is on, Slate
	// cancels all non-slow-task modals itself, so any modal that appears
	// AFTER the mode was enabled is a slow-task progress window — leave it
	// alone. Only a modal that predates the enable time (editor already hung
	// when the agent flipped the mode) gets dismissed: the recovery case.
	bool bAppliedUnattended = false;
	double UnattendedEnableTime = 0.0;
	TWeakPtr<SWindow> LastObservedModal;
	double ModalFirstSeenTime = 0.0;
	bool bModalPresenceRecorded = false;
	FDelegateHandle SlatePreTickHandle;
	// Cross-thread modal visibility for health_check (title under EventsCS).
	std::atomic<bool> bModalActive{false};
	FString CachedModalTitle;

	// Bridge event ring buffer (modal_dismissed, command_timeout, ...).
	mutable FCriticalSection EventsCS;
	TArray<TSharedPtr<FJsonObject>> BridgeEvents;
	uint64 EventSeq = 0;

	// Command handler instances
	TSharedPtr<FEpicUnrealMCPEditorCommands> EditorCommands;
	TSharedPtr<FEpicUnrealMCPBlueprintCommands> BlueprintCommands;
	TSharedPtr<FEpicUnrealMCPBlueprintGraphCommands> BlueprintGraphCommands;
	TSharedPtr<FEpicUnrealMCPMaterialCommands> MaterialCommands;
	TSharedPtr<FEpicUnrealMCPDataTableCommands> DataTableCommands;
	TSharedPtr<FEpicUnrealMCPAssetCommands> AssetCommands;
	TSharedPtr<FEpicUnrealMCPDataAssetCommands> DataAssetCommands;
	TSharedPtr<FEpicUnrealMCPWidgetCommands> WidgetCommands;
	TSharedPtr<FEpicUnrealMCPEnhancedInputCommands> EnhancedInputCommands;
};