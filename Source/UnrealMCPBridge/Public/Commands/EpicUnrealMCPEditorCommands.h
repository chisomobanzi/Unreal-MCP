#pragma once

#include "CoreMinimal.h"
#include "Json.h"

/**
 * Handler for Editor MCP commands.
 * Viewport control, actor manipulation, and level management.
 */
class UNREALMCPBRIDGE_API FEpicUnrealMCPEditorCommands
{
public:
	FEpicUnrealMCPEditorCommands();

	TSharedPtr<FJsonObject> HandleCommand(
		const FString& CommandType,
		const TSharedPtr<FJsonObject>& Params);

private:
	TSharedPtr<FJsonObject> HandleGetActorsInLevel(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleFindActorsByName(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSpawnActor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleDeleteActor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetActorTransform(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSpawnBlueprintActor(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetSelectedActors(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetWorldInfo(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSpawnActorFromClass(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetActorProperties(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSetActorProperty(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetActorPropertyMetadata(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleSpawnActorByClass(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleFindActors(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleTakeScreenshot(const TSharedPtr<FJsonObject>& Params);

	// PIE control
	TSharedPtr<FJsonObject> HandleStartPIE(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleStopPIE(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetPIEStatus(const TSharedPtr<FJsonObject>& Params);

	// Viewport control
	TSharedPtr<FJsonObject> HandleSetViewportCamera(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleFrameActor(const TSharedPtr<FJsonObject>& Params);

	// Console
	TSharedPtr<FJsonObject> HandleExecuteConsoleCommand(const TSharedPtr<FJsonObject>& Params);

	// Shot capture
	TSharedPtr<FJsonObject> HandleCaptureFromCamera(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleContactSheet(const TSharedPtr<FJsonObject>& Params);

	// Telemetry
	TSharedPtr<FJsonObject> HandleGetResourceStats(const TSharedPtr<FJsonObject>& Params);

	// Test-only: raises a raw Slate modal outside any command scope, to drill
	// the unattended-mode modal sentinel (the unattended flag cannot suppress
	// this class of dialog).
	TSharedPtr<FJsonObject> HandleDebugOpenModal(const TSharedPtr<FJsonObject>& Params);

	// Agent-as-player support (PIE world). Python can drive an existing
	// WidgetInteractionComponent but cannot create/register one, nor reach a
	// live UserWidget's WidgetTree — these two fill exactly those gaps.
	TSharedPtr<FJsonObject> HandleAttachInteractionRay(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleGetRuntimeWidgetTree(const TSharedPtr<FJsonObject>& Params);

	// Full hand-ray click in one command: pose the ray at the target's
	// widget face, drive the interaction simulation manually (TickComponent
	// is public), press + release the pointer key. Deterministic — no
	// dependency on the component being ticked by the world.
	TSharedPtr<FJsonObject> HandleHandRayClick(const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleCallSubsystem(const TSharedPtr<FJsonObject>& Params);
};
