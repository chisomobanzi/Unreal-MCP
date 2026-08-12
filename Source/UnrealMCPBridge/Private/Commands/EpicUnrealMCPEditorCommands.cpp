#include "Commands/EpicUnrealMCPEditorCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/EpicUnrealMCPBlueprintCommands.h"
#include "Commands/EpicUnrealMCPPropertyUtils.h"
#include "Subsystems/EditorActorSubsystem.h"
#include "UObject/UObjectIterator.h"
#include "JsonObjectConverter.h"
#include "Editor.h"
#include "EditorViewportClient.h"
#include "LevelEditorViewport.h"
#include "Misc/FileHelper.h"
#include "LevelEditor.h"
#include "IAssetViewport.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "RHI.h"
#include "DynamicRHI.h"
#include "UnrealEngine.h"
#include "Async/Async.h"
#include "Containers/Ticker.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"
#include "Components/WidgetComponent.h"
#include "Components/WidgetInteractionComponent.h"
#include "Components/PanelWidget.h"
#include "Components/ContentWidget.h"
#include "Components/TextBlock.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/CameraComponent.h"
#include "Framework/Application/SlateApplication.h"


#include "GameFramework/Actor.h"
#include "Engine/Selection.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SpotLight.h"
#include "Camera/CameraActor.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "EditorAssetLibrary.h"
#include "EngineUtils.h"

FEpicUnrealMCPEditorCommands::FEpicUnrealMCPEditorCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCommand(
	const FString& CommandType,
	const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("get_actors_in_level"))
	{
		return HandleGetActorsInLevel(Params);
	}
	else if (CommandType == TEXT("find_actors_by_name"))
	{
		return HandleFindActorsByName(Params);
	}
	else if (CommandType == TEXT("spawn_actor"))
	{
		return HandleSpawnActor(Params);
	}
	else if (CommandType == TEXT("delete_actor"))
	{
		return HandleDeleteActor(Params);
	}
	else if (CommandType == TEXT("set_actor_transform"))
	{
		return HandleSetActorTransform(Params);
	}
	else if (CommandType == TEXT("spawn_blueprint_actor"))
	{
		return HandleSpawnBlueprintActor(Params);
	}
	else if (CommandType == TEXT("get_selected_actors"))
	{
		return HandleGetSelectedActors(Params);
	}
	else if (CommandType == TEXT("get_world_info"))
	{
		return HandleGetWorldInfo(Params);
	}
	else if (CommandType == TEXT("spawn_actor_from_class"))
	{
		return HandleSpawnActorFromClass(Params);
	}
	else if (CommandType == TEXT("get_actor_properties"))
	{
		return HandleGetActorProperties(Params);
	}
	else if (CommandType == TEXT("set_actor_property"))
	{
		return HandleSetActorProperty(Params);
	}
	else if (CommandType == TEXT("get_actor_property_metadata"))
	{
		return HandleGetActorPropertyMetadata(Params);
	}
	else if (CommandType == TEXT("spawn_actor_by_class"))
	{
		return HandleSpawnActorByClass(Params);
	}
	else if (CommandType == TEXT("find_actors"))
	{
		return HandleFindActors(Params);
	}
	else if (CommandType == TEXT("take_screenshot"))
	{
		return HandleTakeScreenshot(Params);
	}
	else if (CommandType == TEXT("start_pie"))
	{
		return HandleStartPIE(Params);
	}
	else if (CommandType == TEXT("stop_pie"))
	{
		return HandleStopPIE(Params);
	}
	else if (CommandType == TEXT("get_pie_status"))
	{
		return HandleGetPIEStatus(Params);
	}
	else if (CommandType == TEXT("set_viewport_camera"))
	{
		return HandleSetViewportCamera(Params);
	}
	else if (CommandType == TEXT("frame_actor"))
	{
		return HandleFrameActor(Params);
	}
	else if (CommandType == TEXT("execute_console_command"))
	{
		return HandleExecuteConsoleCommand(Params);
	}
	else if (CommandType == TEXT("capture_from_camera"))
	{
		return HandleCaptureFromCamera(Params);
	}
	else if (CommandType == TEXT("contact_sheet"))
	{
		return HandleContactSheet(Params);
	}
	else if (CommandType == TEXT("get_resource_stats"))
	{
		return HandleGetResourceStats(Params);
	}
	else if (CommandType == TEXT("debug_open_modal"))
	{
		return HandleDebugOpenModal(Params);
	}
	else if (CommandType == TEXT("attach_interaction_ray"))
	{
		return HandleAttachInteractionRay(Params);
	}
	else if (CommandType == TEXT("get_runtime_widget_tree"))
	{
		return HandleGetRuntimeWidgetTree(Params);
	}
	else if (CommandType == TEXT("hand_ray_click"))
	{
		return HandleHandRayClick(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown editor command: %s"), *CommandType));
}

// No public header declares these in 5.6; engine code declares them extern
// at use sites (e.g. EngineAnalyticsSessionSummary.cpp) — same here.
extern ENGINE_API float GAverageFPS;
extern ENGINE_API float GAverageMS;

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetResourceStats(
	const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	constexpr double MB = 1024.0 * 1024.0;

	const FPlatformMemoryStats MemStats = FPlatformMemory::GetStats();
	Result->SetNumberField(TEXT("process_physical_mb"), MemStats.UsedPhysical / MB);
	Result->SetNumberField(TEXT("process_physical_peak_mb"), MemStats.PeakUsedPhysical / MB);
	Result->SetNumberField(TEXT("process_virtual_mb"), MemStats.UsedVirtual / MB);
	Result->SetNumberField(TEXT("system_physical_free_mb"), MemStats.AvailablePhysical / MB);
	Result->SetNumberField(TEXT("system_physical_total_mb"), MemStats.TotalPhysical / MB);

	FTextureMemoryStats TexStats;
	RHIGetTextureMemoryStats(TexStats);
	if (TexStats.DedicatedVideoMemory > 0)
	{
		Result->SetNumberField(TEXT("vram_dedicated_mb"), TexStats.DedicatedVideoMemory / MB);
	}
	if (TexStats.TotalGraphicsMemory > 0)
	{
		Result->SetNumberField(TEXT("vram_total_mb"), TexStats.TotalGraphicsMemory / MB);
	}
	Result->SetNumberField(TEXT("texture_streaming_mb"), TexStats.StreamingMemorySize / MB);
	Result->SetNumberField(TEXT("texture_nonstreaming_mb"), TexStats.NonStreamingMemorySize / MB);
	if (TexStats.TexturePoolSize > 0)
	{
		Result->SetNumberField(TEXT("texture_pool_mb"), TexStats.TexturePoolSize / MB);
	}

	Result->SetNumberField(TEXT("average_fps"), GAverageFPS);
	Result->SetNumberField(TEXT("average_frame_ms"), GAverageMS);
	Result->SetBoolField(TEXT("pie_active"), GEditor && GEditor->PlayWorld != nullptr);
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

namespace
{
	AActor* FindPIEActorByQuery(UWorld* World, const FString& Query)
	{
		const FString Q = Query.ToLower();
		AActor* Best = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			AActor* A = *It;
			const FString Label = A->GetActorLabel().ToLower();
			const FString Name = A->GetName().ToLower();
			if (Label == Q || Name == Q)
			{
				return A;
			}
			if (!Best && (Label.Contains(Q) || Name.Contains(Q)))
			{
				Best = A;
			}
		}
		return Best;
	}

	void WalkWidgetTree(UWidget* Widget, int32 Depth,
		TArray<TSharedPtr<FJsonValue>>& Out, int32& Budget)
	{
		if (!Widget || Budget <= 0)
		{
			return;
		}
		--Budget;

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), Widget->GetName());
		Entry->SetStringField(TEXT("class"), Widget->GetClass()->GetName());
		Entry->SetNumberField(TEXT("depth"), Depth);
		Entry->SetStringField(TEXT("visibility"),
			StaticEnum<ESlateVisibility>()->GetNameStringByValue((int64)Widget->GetVisibility()));
		Entry->SetBoolField(TEXT("hit_testable"),
			Widget->GetVisibility() == ESlateVisibility::Visible);
		Entry->SetBoolField(TEXT("enabled"), Widget->GetIsEnabled());
		// Full object path so Python can resolve the live widget with
		// unreal.find_object (live trees aren't otherwise reachable from
		// Python) — e.g. to broadcast a Button's OnClicked.
		Entry->SetStringField(TEXT("path"), Widget->GetPathName());
		if (const UTextBlock* Text = Cast<UTextBlock>(Widget))
		{
			Entry->SetStringField(TEXT("text"), Text->GetText().ToString());
		}
		Out.Add(MakeShared<FJsonValueObject>(Entry));

		if (const UUserWidget* User = Cast<UUserWidget>(Widget))
		{
			if (User->WidgetTree)
			{
				WalkWidgetTree(User->WidgetTree->RootWidget, Depth + 1, Out, Budget);
			}
		}
		else if (const UPanelWidget* Panel = Cast<UPanelWidget>(Widget))
		{
			for (int32 i = 0; i < Panel->GetChildrenCount(); ++i)
			{
				WalkWidgetTree(Panel->GetChildAt(i), Depth + 1, Out, Budget);
			}
		}
		else if (const UContentWidget* Content = Cast<UContentWidget>(Widget))
		{
			WalkWidgetTree(Content->GetContent(), Depth + 1, Out, Budget);
		}
	}
} // namespace

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleAttachInteractionRay(
	const TSharedPtr<FJsonObject>& Params)
{
	UWorld* PIE = GEditor ? GEditor->PlayWorld : nullptr;
	if (!PIE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PIE is not running"));
	}
	APlayerController* PC = PIE->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No player pawn in PIE"));
	}

	double Distance = 10000.0;
	if (Params.IsValid() && Params->HasField(TEXT("interaction_distance")))
	{
		Distance = Params->GetNumberField(TEXT("interaction_distance"));
	}
	bool bPreferExisting = true;
	if (Params.IsValid() && Params->HasField(TEXT("prefer_existing")))
	{
		bPreferExisting = Params->GetBoolField(TEXT("prefer_existing"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TArray<UWidgetInteractionComponent*> Existing;
	Pawn->GetComponents(Existing);
	for (UWidgetInteractionComponent* C : Existing)
	{
		if (C->GetFName() == TEXT("MCPHandRay"))
		{
			Result->SetBoolField(TEXT("success"), true);
			Result->SetStringField(TEXT("component"), C->GetName());
			Result->SetStringField(TEXT("kind"), TEXT("transient"));
			return Result;
		}
	}
	if (bPreferExisting && Existing.Num() > 0)
	{
		// The pawn's real hand-ray (VR) — drive that one.
		Result->SetBoolField(TEXT("success"), true);
		Result->SetStringField(TEXT("component"), Existing[0]->GetName());
		Result->SetStringField(TEXT("kind"), TEXT("pawn"));
		return Result;
	}

	UWidgetInteractionComponent* Ray =
		NewObject<UWidgetInteractionComponent>(Pawn, FName(TEXT("MCPHandRay")));
	if (!Ray)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create ray component"));
	}
	Ray->InteractionDistance = static_cast<float>(Distance);
	Ray->InteractionSource = EWidgetInteractionSource::World;
	Ray->RegisterComponent();
	// Runtime-created components don't auto-activate/tick — without these the
	// interaction never traces (observed live: last hit permanently empty).
	Ray->Activate(true);
	Ray->SetComponentTickEnabled(true);

	USceneComponent* Attach = Pawn->FindComponentByClass<UCameraComponent>();
	if (!Attach)
	{
		Attach = Pawn->GetRootComponent();
	}
	Ray->AttachToComponent(Attach, FAttachmentTransformRules::SnapToTargetNotIncludingScale);

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("component"), Ray->GetName());
	Result->SetStringField(TEXT("kind"), TEXT("transient"));
	Result->SetStringField(TEXT("attached_to"), Attach ? Attach->GetName() : TEXT("none"));
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetRuntimeWidgetTree(
	const TSharedPtr<FJsonObject>& Params)
{
	UWorld* PIE = GEditor ? GEditor->PlayWorld : nullptr;
	if (!PIE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PIE is not running"));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	int32 Budget = 300;
	if (Params.IsValid() && Params->HasField(TEXT("max_widgets")))
	{
		Budget = FMath::Clamp((int32)Params->GetNumberField(TEXT("max_widgets")), 10, 5000);
	}

	FString ActorQuery;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("actor"), ActorQuery);
	}

	if (!ActorQuery.IsEmpty())
	{
		AActor* A = FindPIEActorByQuery(PIE, ActorQuery);
		if (!A)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("No PIE actor matches: %s"), *ActorQuery));
		}
		Result->SetStringField(TEXT("actor"), A->GetActorLabel());

		TArray<TSharedPtr<FJsonValue>> Comps;
		TArray<UWidgetComponent*> WidgetComps;
		A->GetComponents(WidgetComps);
		for (UWidgetComponent* WC : WidgetComps)
		{
			TSharedPtr<FJsonObject> CompObj = MakeShared<FJsonObject>();
			CompObj->SetStringField(TEXT("component"), WC->GetName());
			CompObj->SetStringField(TEXT("collision_enabled"),
				StaticEnum<ECollisionEnabled::Type>()->GetNameStringByValue(
					(int64)WC->GetCollisionEnabled()));
			CompObj->SetBoolField(TEXT("blocks_visibility"),
				WC->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
			TArray<TSharedPtr<FJsonValue>> Tree;
			if (UUserWidget* W = Cast<UUserWidget>(WC->GetWidget()))
			{
				CompObj->SetStringField(TEXT("widget_class"), W->GetClass()->GetName());
				WalkWidgetTree(W, 0, Tree, Budget);
			}
			CompObj->SetArrayField(TEXT("tree"), Tree);
			Comps.Add(MakeShared<FJsonValueObject>(CompObj));
		}
		Result->SetArrayField(TEXT("widget_components"), Comps);
	}

	bool bViewport = false;
	if (Params.IsValid() && Params->HasField(TEXT("include_viewport")))
	{
		bViewport = Params->GetBoolField(TEXT("include_viewport"));
	}
	if (bViewport || ActorQuery.IsEmpty())
	{
		TArray<UUserWidget*> Widgets;
		UWidgetBlueprintLibrary::GetAllWidgetsOfClass(
			PIE, Widgets, UUserWidget::StaticClass(), /*TopLevelOnly=*/true);
		TArray<TSharedPtr<FJsonValue>> ViewportArr;
		for (UUserWidget* W : Widgets)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("class"), W->GetClass()->GetName());
			TArray<TSharedPtr<FJsonValue>> Tree;
			WalkWidgetTree(W, 0, Tree, Budget);
			Obj->SetArrayField(TEXT("tree"), Tree);
			ViewportArr.Add(MakeShared<FJsonValueObject>(Obj));
		}
		Result->SetArrayField(TEXT("viewport_widgets"), ViewportArr);
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("truncated"), Budget <= 0);
	return Result;
}

namespace HandRayAim
{
	/** Depth-first search for a named widget inside a live widget component,
	 *  diving into nested user widgets (an embedded WBP has its own tree).
	 *  Exact name wins; a substring match is the fallback. */
	static UWidget* FindSubWidget(UUserWidget* Root, const FString& Name)
	{
		if (!Root || !Root->WidgetTree || Name.IsEmpty())
		{
			return nullptr;
		}
		UWidget* Exact = nullptr;
		UWidget* Loose = nullptr;
		Root->WidgetTree->ForEachWidgetAndDescendants([&](UWidget* W)
		{
			if (!W || Exact)
			{
				return;
			}
			const FString WName = W->GetName();
			if (WName.Equals(Name, ESearchCase::IgnoreCase))
			{
				Exact = W;
			}
			else if (!Loose && WName.Contains(Name))
			{
				Loose = W;
			}
		});
		return Exact ? Exact : Loose;
	}

	/** A sub-widget's rect in the SAME space UWidgetComponent::GetLocalHitLocation
	 *  outputs (widget-space pixels). Derived by normalising the child's cached
	 *  Slate geometry against the root's, so it is immune to whatever DPI or
	 *  content scale the virtual window happens to be running at. */
	static bool GetSubWidgetLocalRect(UWidgetComponent* WC, UWidget* Sub,
		FVector2D& OutMin, FVector2D& OutSize)
	{
		UUserWidget* RootUW = WC ? WC->GetUserWidgetObject() : nullptr;
		if (!RootUW || !Sub)
		{
			return false;
		}
		const FGeometry& RootGeo = RootUW->GetCachedGeometry();
		const FGeometry& SubGeo = Sub->GetCachedGeometry();
		const FVector2D RootPos = RootGeo.GetAbsolutePosition();
		const FVector2D RootSize = RootGeo.GetAbsoluteSize();
		const FVector2D SubSize = SubGeo.GetAbsoluteSize();
		if (RootSize.X <= KINDA_SMALL_NUMBER || RootSize.Y <= KINDA_SMALL_NUMBER
			|| SubSize.X <= KINDA_SMALL_NUMBER || SubSize.Y <= KINDA_SMALL_NUMBER)
		{
			return false; // never laid out (collapsed, or the widget hasn't ticked yet)
		}
		const FVector2D SubPos = SubGeo.GetAbsolutePosition();
		const FVector2D Draw = WC->GetCurrentDrawSize();
		OutMin = (SubPos - RootPos) / RootSize * Draw;
		OutSize = SubSize / RootSize * Draw;
		return true;
	}

	/** The world point on the widget's plane that maps to the given widget-space
	 *  pixel. Solved by probing the component's OWN forward mapping at three
	 *  points and inverting the 2x2 — so it stays correct whatever axis flip or
	 *  pivot convention GetLocalHitLocation uses, this engine version or the next. */
	static bool SolveWorldPointForLocal(UWidgetComponent* WC,
		const FVector2D& TargetLocal, FVector& OutWorld)
	{
		const FVector C = WC->GetComponentLocation();
		const FVector R = WC->GetRightVector();
		const FVector U = WC->GetUpVector();
		constexpr double Probe = 10.0;

		FVector2D L0, LR, LU;
		WC->GetLocalHitLocation(C, L0);
		WC->GetLocalHitLocation(C + R * Probe, LR);
		WC->GetLocalHitLocation(C + U * Probe, LU);

		const FVector2D dR = (LR - L0) / Probe;
		const FVector2D dU = (LU - L0) / Probe;
		const double Det = dR.X * dU.Y - dU.X * dR.Y;
		if (FMath::IsNearlyZero(Det))
		{
			return false; // degenerate mapping — fall back to centre aim
		}
		const FVector2D T = TargetLocal - L0;
		const double A = (T.X * dU.Y - dU.X * T.Y) / Det;
		const double B = (dR.X * T.Y - T.X * dR.Y) / Det;
		OutWorld = C + R * A + U * B;
		return true;
	}

	static TSharedPtr<FJsonObject> Vec2Json(const FVector2D& V)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetNumberField(TEXT("x"), V.X);
		Obj->SetNumberField(TEXT("y"), V.Y);
		return Obj;
	}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleHandRayClick(
	const TSharedPtr<FJsonObject>& Params)
{
	UWorld* PIE = GEditor ? GEditor->PlayWorld : nullptr;
	if (!PIE)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("PIE is not running"));
	}
	APlayerController* PC = PIE->GetFirstPlayerController();
	APawn* Pawn = PC ? PC->GetPawn() : nullptr;
	if (!Pawn)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No player pawn in PIE"));
	}

	FString TargetQuery;
	if (!Params.IsValid() || !Params->TryGetStringField(TEXT("target"), TargetQuery))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target' parameter"));
	}
	FString ButtonName = TEXT("LeftMouseButton");
	if (Params->HasField(TEXT("button")))
	{
		ButtonName = Params->GetStringField(TEXT("button"));
	}
	double HandDistance = 120.0;
	if (Params->HasField(TEXT("hand_distance")))
	{
		HandDistance = Params->GetNumberField(TEXT("hand_distance"));
	}

	AActor* Target = FindPIEActorByQuery(PIE, TargetQuery);
	if (!Target)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("No PIE actor matches: %s"), *TargetQuery));
	}

	// Reuse (or create) the transient ray.
	UWidgetInteractionComponent* Ray = nullptr;
	{
		TArray<UWidgetInteractionComponent*> Existing;
		Pawn->GetComponents(Existing);
		for (UWidgetInteractionComponent* C : Existing)
		{
			if (C->GetFName() == TEXT("MCPHandRay"))
			{
				Ray = C;
				break;
			}
		}
		if (!Ray)
		{
			Ray = NewObject<UWidgetInteractionComponent>(Pawn, FName(TEXT("MCPHandRay")));
			Ray->InteractionSource = EWidgetInteractionSource::World;
			Ray->RegisterComponent();
			USceneComponent* Attach = Pawn->FindComponentByClass<UCameraComponent>();
			if (!Attach)
			{
				Attach = Pawn->GetRootComponent();
			}
			Ray->AttachToComponent(Attach, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
		}
		Ray->InteractionDistance = 10000.0f;
		Ray->Activate(true);
		Ray->SetComponentTickEnabled(true);
	}

	// Optionally aim at a NAMED child widget instead of the panel's centre —
	// a five-button panel is five distinct targets, not one.
	FString SubWidgetName, WidgetComponentName;
	Params->TryGetStringField(TEXT("widget"), SubWidgetName);
	Params->TryGetStringField(TEXT("widget_component"), WidgetComponentName);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Pose like a raised VR hand: just off the widget face along its normal,
	// firing straight at it (aiming from the pawn's eyes gets blocked by the
	// target's own mesh).
	FVector AimPoint = Target->GetActorLocation();
	FVector FaceNormal = Target->GetActorForwardVector();

	// Pick the widget component: the one carrying the named child if we're
	// looking for one, otherwise (as before) the actor's first.
	TArray<UWidgetComponent*> WidgetComps;
	Target->GetComponents(WidgetComps);
	UWidgetComponent* AimComp = nullptr;
	UWidget* SubWidget = nullptr;
	for (UWidgetComponent* WC : WidgetComps)
	{
		if (!WC || (!WidgetComponentName.IsEmpty()
			&& !WC->GetName().Contains(WidgetComponentName)))
		{
			continue;
		}
		if (!AimComp)
		{
			AimComp = WC;
		}
		if (!SubWidgetName.IsEmpty())
		{
			if (UWidget* Found = HandRayAim::FindSubWidget(WC->GetUserWidgetObject(), SubWidgetName))
			{
				AimComp = WC;
				SubWidget = Found;
				break;
			}
		}
	}
	if (AimComp)
	{
		AimPoint = AimComp->GetComponentLocation();
		FaceNormal = AimComp->GetForwardVector();
	}

	FVector2D RectMin = FVector2D::ZeroVector;
	FVector2D RectSize = FVector2D::ZeroVector;
	bool bAimedAtSubWidget = false;
	if (!SubWidgetName.IsEmpty())
	{
		if (!SubWidget)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(FString::Printf(
				TEXT("Actor '%s' has no live widget named '%s' (use get_widget_tree_runtime "
					 "to list what is actually there)"), *Target->GetActorLabel(), *SubWidgetName));
		}
		Result->SetStringField(TEXT("aimed_widget"), SubWidget->GetName());
		Result->SetStringField(TEXT("aimed_widget_class"), SubWidget->GetClass()->GetName());
		Result->SetStringField(TEXT("aimed_widget_visibility"),
			StaticEnum<ESlateVisibility>()->GetNameStringByValue(
				(int64)SubWidget->GetVisibility()));
		Result->SetStringField(TEXT("widget_component"), AimComp->GetName());
		Result->SetStringField(TEXT("geometry_mode"),
			StaticEnum<EWidgetGeometryMode>()->GetNameStringByValue(
				(int64)AimComp->GetGeometryMode()));

		FVector Solved;
		if (HandRayAim::GetSubWidgetLocalRect(AimComp, SubWidget, RectMin, RectSize)
			&& HandRayAim::SolveWorldPointForLocal(AimComp, RectMin + RectSize * 0.5, Solved))
		{
			AimPoint = Solved;
			bAimedAtSubWidget = true;
			Result->SetObjectField(TEXT("widget_rect_px"), HandRayAim::Vec2Json(RectMin));
			Result->SetObjectField(TEXT("widget_size_px"), HandRayAim::Vec2Json(RectSize));
			// Round-trip through the engine's own forward mapping: if this isn't
			// ~0 the aim solve is wrong, and the caller should not trust the click.
			FVector2D Back;
			AimComp->GetLocalHitLocation(Solved, Back);
			Result->SetNumberField(TEXT("aim_roundtrip_error_px"),
				(Back - (RectMin + RectSize * 0.5)).Size());
		}
		else
		{
			Result->SetStringField(TEXT("aim_warning"),
				TEXT("widget has no laid-out geometry (collapsed, or not ticked yet) — "
					 "fell back to the panel centre"));
		}
	}
	Result->SetBoolField(TEXT("aimed_at_sub_widget"), bAimedAtSubWidget);

	const FVector Hand = AimPoint + FaceNormal * HandDistance;
	Ray->SetAbsolute(true, true, false);
	Ray->SetWorldLocationAndRotation(Hand, (AimPoint - Hand).Rotation());

	// Optionally ignore the target's own non-widget primitives for the
	// duration of the click. Some authored actors fully enclose their
	// readout in visibility-blocking mesh (the Engine Room pods do — a
	// player's ray can't reach them either; surface that separately).
	bool bIgnoreOccluders = false;
	if (Params->HasField(TEXT("ignore_target_occluders")))
	{
		bIgnoreOccluders = Params->GetBoolField(TEXT("ignore_target_occluders"));
	}
	TArray<TPair<UPrimitiveComponent*, ECollisionResponse>> SavedResponses;
	if (bIgnoreOccluders)
	{
		TArray<UPrimitiveComponent*> Prims;
		Target->GetComponents(Prims);
		for (UPrimitiveComponent* P : Prims)
		{
			if (!P->IsA<UWidgetComponent>()
				&& P->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block)
			{
				SavedResponses.Emplace(P, ECR_Block);
				P->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
			}
		}
	}

	// Drive the simulation ourselves — deterministic, no world-tick reliance.
	Ray->TickComponent(0.016f, LEVELTICK_All, nullptr);

	UWidgetComponent* Hovered = Ray->GetHoveredWidgetComponent();
	Result->SetStringField(TEXT("hovered"),
		Hovered ? Hovered->GetOwner()->GetActorLabel() : TEXT(""));
	Result->SetBoolField(TEXT("hovered_interactable"), Ray->IsOverInteractableWidget());
	const FHitResult& Hit = Ray->GetLastHitResult();
	Result->SetStringField(TEXT("last_hit_actor"),
		Hit.GetActor() ? Hit.GetActor()->GetActorLabel() : TEXT(""));
	Result->SetStringField(TEXT("last_hit_component"),
		Hit.GetComponent() ? Hit.GetComponent()->GetName() : TEXT(""));

	// Where the ray ACTUALLY landed in widget space, and whether that is inside
	// the rect we were asked for. Hitting the panel but missing the button is a
	// miss — reporting it as a click is the bug this whole change exists to kill.
	if (bAimedAtSubWidget && Hovered == AimComp)
	{
		FVector2D HitLocal;
		AimComp->GetLocalHitLocation(Hit.ImpactPoint, HitLocal);
		Result->SetObjectField(TEXT("hit_local_px"), HandRayAim::Vec2Json(HitLocal));
		Result->SetBoolField(TEXT("hit_inside_widget"),
			HitLocal.X >= RectMin.X && HitLocal.X <= RectMin.X + RectSize.X
			&& HitLocal.Y >= RectMin.Y && HitLocal.Y <= RectMin.Y + RectSize.Y);
	}
	else if (bAimedAtSubWidget)
	{
		Result->SetBoolField(TEXT("hit_inside_widget"), false);
	}

	bool bClicked = false;
	if (Hovered)
	{
		Ray->PressPointerKey(FKey(*ButtonName));
		Ray->TickComponent(0.016f, LEVELTICK_All, nullptr);
		Ray->ReleasePointerKey(FKey(*ButtonName));
		bClicked = true;
	}

	for (const TPair<UPrimitiveComponent*, ECollisionResponse>& Saved : SavedResponses)
	{
		if (IsValid(Saved.Key))
		{
			Saved.Key->SetCollisionResponseToChannel(ECC_Visibility, Saved.Value);
		}
	}
	Result->SetNumberField(TEXT("occluders_ignored"), SavedResponses.Num());

	Result->SetBoolField(TEXT("clicked"), bClicked);
	Result->SetStringField(TEXT("target"), Target->GetActorLabel());
	Result->SetBoolField(TEXT("success"), true);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleDebugOpenModal(
	const TSharedPtr<FJsonObject>& Params)
{
	// Deferred ~0.5s so the modal opens well AFTER this command returns —
	// replicating dialogs raised outside a command window (PIE-start
	// prompts, deferred imports). With unattended mode OFF this hangs the
	// game thread for real; the recovery path (fast-path
	// set_unattended_mode(true) + sentinel dismissal) must un-wedge it.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[](float) -> bool
	{
		if (!FSlateApplication::IsInitialized())
		{
			return false;
		}
		TSharedRef<SWindow> Window = SNew(SWindow)
			.Title(FText::FromString(TEXT("MCP sentinel drill modal")))
			.ClientSize(FVector2D(420, 120))
			.SupportsMaximize(false)
			.SupportsMinimize(false)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT(
					"If you can read this, the unattended-mode modal sentinel "
					"failed to dismiss it. Close manually.")))
			];
		FSlateApplication::Get().AddModalWindow(Window, nullptr);
		return false; // one-shot
	}), 0.5f);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("note"),
		TEXT("modal opens in ~0.5s outside command scope; recovery must dismiss it"));
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetActorsInLevel(
	const TSharedPtr<FJsonObject>& Params)
{
	// Editor world, not GWorld: GWorld is the PIE world during play.
	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(
		FEpicUnrealMCPCommonUtils::GetEditorWorld(), AActor::StaticClass(), AllActors);

	FString ClassFilter;
	Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
	double MaxResultsD = 200.0;
	Params->TryGetNumberField(TEXT("max_results"), MaxResultsD);
	const int32 MaxResults = FMath::Max(1, (int32)MaxResultsD);

	int32 Matched = 0;
	TArray<TSharedPtr<FJsonValue>> ActorArray;
	for (AActor* Actor : AllActors)
	{
		if (!Actor)
		{
			continue;
		}
		if (!ClassFilter.IsEmpty() &&
			!Actor->GetClass()->GetName().Contains(ClassFilter))
		{
			continue;
		}
		++Matched;
		if (ActorArray.Num() < MaxResults)
		{
			ActorArray.Add(FEpicUnrealMCPCommonUtils::ActorToJson(Actor));
		}
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetArrayField(TEXT("actors"), ActorArray);
	ResultObj->SetNumberField(TEXT("total_matched"), Matched);
	if (Matched > ActorArray.Num())
	{
		ResultObj->SetStringField(TEXT("note"), FString::Printf(
			TEXT("Showing %d of %d — raise max_results or narrow class_filter"),
			ActorArray.Num(), Matched));
	}
	return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleFindActorsByName(
	const TSharedPtr<FJsonObject>& Params)
{
	FString Pattern;
	if (!Params->TryGetStringField(TEXT("pattern"), Pattern))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'pattern' parameter"));
	}

	// Editor world, not GWorld; match display labels as well as object names.
	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(
		FEpicUnrealMCPCommonUtils::GetEditorWorld(), AActor::StaticClass(), AllActors);

	double MaxResultsD = 100.0;
	Params->TryGetNumberField(TEXT("max_results"), MaxResultsD);
	const int32 MaxResults = FMath::Max(1, (int32)MaxResultsD);

	int32 Matched = 0;
	TArray<TSharedPtr<FJsonValue>> MatchingActors;
	for (AActor* Actor : AllActors)
	{
		if (Actor && (Actor->GetName().Contains(Pattern) ||
			Actor->GetActorLabel().Contains(Pattern)))
		{
			++Matched;
			if (MatchingActors.Num() < MaxResults)
			{
				MatchingActors.Add(FEpicUnrealMCPCommonUtils::ActorToJson(Actor));
			}
		}
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetArrayField(TEXT("actors"), MatchingActors);
	ResultObj->SetNumberField(TEXT("total_matched"), Matched);
	return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnActor(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ActorType;
	if (!Params->TryGetStringField(TEXT("type"), ActorType))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'type' parameter"));
	}

	FString ActorName;
	if (!Params->TryGetStringField(TEXT("name"), ActorName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
	}

	FVector Location(0.0f, 0.0f, 0.0f);
	FRotator Rotation(0.0f, 0.0f, 0.0f);
	FVector Scale(1.0f, 1.0f, 1.0f);

	if (Params->HasField(TEXT("location")))
	{
		Location = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		Rotation = FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"));
	}
	if (Params->HasField(TEXT("scale")))
	{
		Scale = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale"));
	}

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
	}

	TArray<AActor*> AllActors;
	UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
	for (AActor* Actor : AllActors)
	{
		if (Actor && Actor->GetName() == ActorName)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Actor with name '%s' already exists"), *ActorName));
		}
	}

	AActor* NewActor = nullptr;
	FActorSpawnParameters SpawnParams;
	SpawnParams.Name = *ActorName;

	if (ActorType == TEXT("StaticMeshActor"))
	{
		AStaticMeshActor* NewMeshActor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), Location, Rotation, SpawnParams);
		if (NewMeshActor)
		{
			FString MeshPath;
			if (Params->TryGetStringField(TEXT("static_mesh"), MeshPath))
			{
				UStaticMesh* Mesh = Cast<UStaticMesh>(UEditorAssetLibrary::LoadAsset(MeshPath));
				if (Mesh)
				{
					NewMeshActor->GetStaticMeshComponent()->SetStaticMesh(Mesh);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("Could not find static mesh at path: %s"), *MeshPath);
				}
			}
		}
		NewActor = NewMeshActor;
	}
	else if (ActorType == TEXT("PointLight"))
	{
		NewActor = World->SpawnActor<APointLight>(
			APointLight::StaticClass(), Location, Rotation, SpawnParams);
	}
	else if (ActorType == TEXT("SpotLight"))
	{
		NewActor = World->SpawnActor<ASpotLight>(
			ASpotLight::StaticClass(), Location, Rotation, SpawnParams);
	}
	else if (ActorType == TEXT("DirectionalLight"))
	{
		NewActor = World->SpawnActor<ADirectionalLight>(
			ADirectionalLight::StaticClass(), Location, Rotation, SpawnParams);
	}
	else if (ActorType == TEXT("CameraActor"))
	{
		NewActor = World->SpawnActor<ACameraActor>(
			ACameraActor::StaticClass(), Location, Rotation, SpawnParams);
	}
	else
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Unknown actor type: %s"), *ActorType));
	}

	if (NewActor)
	{
		// SpawnActor only takes location and rotation, apply scale separately
		FTransform Transform = NewActor->GetTransform();
		Transform.SetScale3D(Scale);
		NewActor->SetActorTransform(Transform);

		return FEpicUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to create actor"));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleDeleteActor(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName;
	if (!Params->TryGetStringField(TEXT("name"), ActorName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : GWorld;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
	}

	AActor* Target = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A && (A->GetActorLabel() == ActorName || A->GetName() == ActorName))
		{
			Target = A;
			break;
		}
	}

	if (!Target)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: %s"), *ActorName));
	}

	TSharedPtr<FJsonObject> ActorInfo = FEpicUnrealMCPCommonUtils::ActorToJsonObject(Target);

	// Modify() registers with the transaction system so Ctrl+Z restores the actor.
	Target->Modify();
	if (ULevel* Level = Target->GetLevel())
	{
		Level->Modify();
	}

	if (UEditorActorSubsystem* EAS = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
	{
		EAS->DestroyActor(Target);
	}
	else
	{
		World->EditorDestroyActor(Target, true);
	}

	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetBoolField(TEXT("success"), true);
	ResultObj->SetObjectField(TEXT("deleted_actor"), ActorInfo);
	return ResultObj;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSetActorTransform(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ActorName;
	if (!Params->TryGetStringField(TEXT("name"), ActorName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'name' parameter"));
	}

	AActor* TargetActor = FEpicUnrealMCPCommonUtils::ResolveActor(ActorName);
	if (!TargetActor)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Actor not found (by label, name, or path): %s"), *ActorName));
	}

	FTransform NewTransform = TargetActor->GetTransform();

	if (Params->HasField(TEXT("location")))
	{
		NewTransform.SetLocation(
			FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")));
	}
	if (Params->HasField(TEXT("rotation")))
	{
		NewTransform.SetRotation(
			FQuat(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation"))));
	}
	if (Params->HasField(TEXT("scale")))
	{
		NewTransform.SetScale3D(
			FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")));
	}

	TargetActor->SetActorTransform(NewTransform);
	return FEpicUnrealMCPCommonUtils::ActorToJsonObject(TargetActor, true);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnBlueprintActor(
	const TSharedPtr<FJsonObject>& Params)
{
	FEpicUnrealMCPBlueprintCommands BlueprintCommands;
	return BlueprintCommands.HandleCommand(TEXT("spawn_blueprint_actor"), Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetSelectedActors(
	const TSharedPtr<FJsonObject>& Params)
{
	TArray<TSharedPtr<FJsonValue>> ActorArray;

	USelection* Selection = GEditor->GetSelectedActors();
	for (FSelectionIterator It(*Selection); It; ++It)
	{
		AActor* Actor = Cast<AActor>(*It);
		if (!Actor)
		{
			continue;
		}

		TSharedPtr<FJsonObject> ActorObj = MakeShared<FJsonObject>();
		ActorObj->SetStringField(TEXT("name"), Actor->GetName());
		ActorObj->SetStringField(TEXT("label"), Actor->GetActorLabel());
		ActorObj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
		const FVector Loc = Actor->GetActorLocation();
		ActorObj->SetStringField(TEXT("location"),
			FString::Printf(TEXT("(%.1f, %.1f, %.1f)"), Loc.X, Loc.Y, Loc.Z));
		ActorArray.Add(MakeShared<FJsonValueObject>(ActorObj));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("actors"), ActorArray);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetWorldInfo(
	const TSharedPtr<FJsonObject>& Params)
{
	UWorld* World = GEditor->GetEditorWorldContext().World();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("world"), World ? World->GetName() : TEXT("None"));

	if (World)
	{
		TArray<AActor*> AllActors;
		UGameplayStatics::GetAllActorsOfClass(World, AActor::StaticClass(), AllActors);
		Result->SetNumberField(TEXT("actor_count"), AllActors.Num());

		FString ClassFilter;
		Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
		bool bNamesOnly = false;
		Params->TryGetBoolField(TEXT("names_only"), bNamesOnly);
		double MaxActorsD = 100.0;
		Params->TryGetNumberField(TEXT("max_actors"), MaxActorsD);
		const int32 MaxActors = FMath::Max(0, (int32)MaxActorsD);

		int32 Matched = 0;
		TArray<TSharedPtr<FJsonValue>> ActorArray;
		for (AActor* Actor : AllActors)
		{
			if (!Actor)
			{
				continue;
			}
			if (!ClassFilter.IsEmpty() &&
				!Actor->GetClass()->GetName().Contains(ClassFilter))
			{
				continue;
			}
			++Matched;
			if (ActorArray.Num() >= MaxActors)
			{
				continue;
			}
			if (bNamesOnly)
			{
				ActorArray.Add(MakeShared<FJsonValueString>(Actor->GetActorLabel()));
			}
			else
			{
				TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
				Obj->SetStringField(TEXT("name"), Actor->GetName());
				Obj->SetStringField(TEXT("class"), Actor->GetClass()->GetName());
				ActorArray.Add(MakeShared<FJsonValueObject>(Obj));
			}
		}
		Result->SetArrayField(TEXT("actors"), ActorArray);

		if (Matched > ActorArray.Num())
		{
			Result->SetStringField(TEXT("note"), FString::Printf(
				TEXT("Showing %d of %d matched — raise max_actors, use class_filter/names_only"),
				ActorArray.Num(), Matched));
		}
	}

	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnActorFromClass(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ClassName;
	if (!Params->TryGetStringField(TEXT("class_name"), ClassName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'class_name' parameter"));
	}

	double LocationX = 0.0, LocationY = 0.0, LocationZ = 0.0;
	double RotYaw = 0.0, RotPitch = 0.0, RotRoll = 0.0;
	Params->TryGetNumberField(TEXT("location_x"), LocationX);
	Params->TryGetNumberField(TEXT("location_y"), LocationY);
	Params->TryGetNumberField(TEXT("location_z"), LocationZ);
	Params->TryGetNumberField(TEXT("rotation_yaw"), RotYaw);
	Params->TryGetNumberField(TEXT("rotation_pitch"), RotPitch);
	Params->TryGetNumberField(TEXT("rotation_roll"), RotRoll);

	const FVector Location(LocationX, LocationY, LocationZ);
	const FRotator Rotation(RotPitch, RotYaw, RotRoll);

	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
	}

	FActorSpawnParameters SpawnParams;
	AActor* NewActor = nullptr;

	// Blueprint path (starts with "/")
	if (ClassName.StartsWith(TEXT("/")))
	{
		UObject* Asset = UEditorAssetLibrary::LoadAsset(ClassName);
		UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
		if (Blueprint && Blueprint->GeneratedClass)
		{
			NewActor = World->SpawnActor<AActor>(
				Blueprint->GeneratedClass, Location, Rotation, SpawnParams);
		}
	}

	// Native UClass — try common module paths
	if (!NewActor)
	{
		static const TCHAR* Modules[] =
		{
			TEXT("/Script/Engine."),
			TEXT("/Script/Runtime/Engine/Classes."),
		};
		UClass* Class = nullptr;
		for (const TCHAR* Prefix : Modules)
		{
			Class = FindObject<UClass>(nullptr, *(FString(Prefix) + ClassName));
			if (Class)
			{
				break;
			}
		}
		if (Class && Class->IsChildOf(AActor::StaticClass()))
		{
			NewActor = World->SpawnActor<AActor>(Class, Location, Rotation, SpawnParams);
		}
	}

	if (!NewActor)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to spawn — class not found or invalid: %s"), *ClassName));
	}

	return FEpicUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetActorProperties(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_label' parameter"));
	}

	FString Filter;
	Params->TryGetStringField(TEXT("filter"), Filter);
	const FString FilterLower = Filter.ToLower();

	bool bIncludeComponents = false;
	Params->TryGetBoolField(TEXT("include_components"), bIncludeComponents);

	bool bFlat = false;
	Params->TryGetBoolField(TEXT("flat"), bFlat);

	bool bIncludeMetadata = false;
	Params->TryGetBoolField(TEXT("include_metadata"), bIncludeMetadata);

	bool bExpandArrays = true;
	Params->TryGetBoolField(TEXT("expand_arrays"), bExpandArrays);

	int32 MaxDepth = 0;
	double MaxDepthD = 0.0;
	if (Params->TryGetNumberField(TEXT("max_depth"), MaxDepthD)) { MaxDepth = static_cast<int32>(MaxDepthD); }
	else { MaxDepth = 3; }

	int32 ArrayElementLimit = 16;
	double ArrLimD = 0.0;
	if (Params->TryGetNumberField(TEXT("array_element_limit"), ArrLimD)) { ArrayElementLimit = static_cast<int32>(ArrLimD); }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
	}

	AActor* Target = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A && (A->GetActorLabel() == ActorLabel || A->GetName() == ActorLabel))
		{
			Target = A;
			break;
		}
	}

	if (!Target)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Actor not found in level: '%s'"), *ActorLabel));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor_name"), Target->GetName());
	Result->SetStringField(TEXT("actor_label"), Target->GetActorLabel());
	Result->SetStringField(TEXT("class"), Target->GetClass()->GetPathName());

	if (bFlat)
	{
		// Flat dotted-path mode — path-aware filtering, depth-limited, AI-friendly.
		FString Category;
		Params->TryGetStringField(TEXT("category"), Category);

		bool bIncludeInherited = true;
		Params->TryGetBoolField(TEXT("include_inherited"), bIncludeInherited);

		int32 MaxEntries = 200;
		double MaxEntriesD = 0.0;
		if (Params->TryGetNumberField(TEXT("max_entries"), MaxEntriesD)) { MaxEntries = static_cast<int32>(MaxEntriesD); }

		int32 Cursor = 0;
		double CursorD = 0.0;
		if (Params->TryGetNumberField(TEXT("cursor"), CursorD)) { Cursor = static_cast<int32>(CursorD); }

		FPropertyWalkOptions Opts;
		Opts.FilterLower = FilterLower;
		Opts.CategoryLower = Category.ToLower();
		Opts.MaxDepth = MaxDepth;
		Opts.MaxEntries = MaxEntries;
		Opts.Cursor = Cursor;
		Opts.bIncludeMetadata = bIncludeMetadata;
		Opts.bExpandArrays = bExpandArrays;
		Opts.ArrayElementLimit = ArrayElementLimit;
		Opts.bIncludeInherited = bIncludeInherited;

		Result->SetObjectField(TEXT("properties"),
			FEpicUnrealMCPPropertyUtils::SerializePropertiesFlat(Target, Opts));

		if (bIncludeComponents)
		{
			TArray<UActorComponent*> Components;
			Target->GetComponents(Components);
			TSharedPtr<FJsonObject> CompMap = MakeShared<FJsonObject>();
			for (UActorComponent* Comp : Components)
			{
				if (!Comp) { continue; }
				CompMap->SetObjectField(Comp->GetName(),
					FEpicUnrealMCPPropertyUtils::SerializePropertiesFlat(Comp, Opts));
			}
			Result->SetObjectField(TEXT("components"), CompMap);
		}
	}
	else
	{
		// Nested mode — top-level FProperty names filtered, structs returned as nested JSON.
		Result->SetObjectField(TEXT("properties"),
			FEpicUnrealMCPPropertyUtils::SerializeAllProperties(Target, FilterLower, true));

		if (bIncludeComponents)
		{
			TArray<UActorComponent*> Components;
			Target->GetComponents(Components);
			TSharedPtr<FJsonObject> CompMap = MakeShared<FJsonObject>();
			for (UActorComponent* Comp : Components)
			{
				if (!Comp) { continue; }
				const FString CompKey = Comp->GetName() + TEXT(" (") + Comp->GetClass()->GetName() + TEXT(")");
				CompMap->SetObjectField(CompKey,
					FEpicUnrealMCPPropertyUtils::SerializeAllProperties(Comp, FilterLower, true));
			}
			Result->SetObjectField(TEXT("components"), CompMap);
		}
	}

	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSetActorProperty(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_label' parameter"));
	}

	FString PropertyPath;
	if (!Params->TryGetStringField(TEXT("property_path"), PropertyPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_path' parameter"));
	}

	const TSharedPtr<FJsonValue> Value = Params->TryGetField(TEXT("property_value"));
	if (!Value.IsValid())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_value' parameter"));
	}

	FString ComponentName;
	Params->TryGetStringField(TEXT("component_name"), ComponentName);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
	}

	AActor* TargetActor = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A && (A->GetActorLabel() == ActorLabel || A->GetName() == ActorLabel))
		{
			TargetActor = A;
			break;
		}
	}

	if (!TargetActor)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Actor not found in level: '%s'"), *ActorLabel));
	}

	UObject* TargetObject = TargetActor;
	UActorComponent* TargetComponent = nullptr;
	if (!ComponentName.IsEmpty())
	{
		TArray<UActorComponent*> Components;
		TargetActor->GetComponents(Components);
		for (UActorComponent* Comp : Components)
		{
			if (Comp && (Comp->GetName() == ComponentName || Comp->GetClass()->GetName() == ComponentName))
			{
				TargetComponent = Comp;
				TargetObject = Comp;
				break;
			}
		}
		if (!TargetComponent)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Component '%s' not found on actor '%s'"), *ComponentName, *ActorLabel));
		}
	}

	TargetActor->Modify();
	if (TargetComponent) { TargetComponent->Modify(); }

	FProperty* TopLevelProp = nullptr;
	FString WriteError;
	const bool bOk = FEpicUnrealMCPPropertyUtils::SetPropertyAtPath(
		TargetObject, PropertyPath, Value, WriteError, &TopLevelProp);

	if (!bOk)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to set '%s' on '%s': %s"), *PropertyPath, *ActorLabel, *WriteError));
	}

	if (TopLevelProp)
	{
		FPropertyChangedEvent ChangeEvent(TopLevelProp, EPropertyChangeType::ValueSet);

		// AActor::PostEditChangeProperty() unconditionally calls RerunConstructionScripts(),
		// which can null-deref inside a fragile Blueprint UserConstructionScript and hard-crash
		// the editor (SIGSEGV at ActorConstruction.cpp:596). The value is already written by
		// SetPropertyAtPath above and the package is marked dirty below, so it persists regardless
		// of the notify. Default to a crash-safe notify; only run the full actor notify (with the
		// construction-script rerun) when the caller explicitly opts in via "rerun_construction_script".
		bool bRerunConstruction = false;
		Params->TryGetBoolField(TEXT("rerun_construction_script"), bRerunConstruction);

		if (TargetComponent)
		{
			// Component property: notify the component so its render/state updates live.
			// Does not force the owning actor's construction script to rerun.
			TargetComponent->PostEditChangeProperty(ChangeEvent);
		}
		else if (bRerunConstruction)
		{
			// Opt-in full actor notify (reruns the construction script). Use only when the target
			// Blueprint's construction script is known-safe and a live editor rebuild is needed.
			TargetObject->PostEditChangeProperty(ChangeEvent);
		}
		// else: actor property in crash-safe mode — value is written + package dirtied, no rerun.
	}

	if (TargetActor->GetLevel())
	{
		TargetActor->GetLevel()->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor_label"), TargetActor->GetActorLabel());
	Result->SetStringField(TEXT("actor_name"), TargetActor->GetName());
	Result->SetStringField(TEXT("property_path"), PropertyPath);
	if (TargetComponent) { Result->SetStringField(TEXT("component"), TargetComponent->GetName()); }
	if (TopLevelProp) { Result->SetStringField(TEXT("top_level_property"), TopLevelProp->GetName()); }
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetActorPropertyMetadata(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ActorLabel;
	if (!Params->TryGetStringField(TEXT("actor_label"), ActorLabel))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor_label' parameter"));
	}

	FString PropertyPath;
	Params->TryGetStringField(TEXT("property_path"), PropertyPath);

	FString Filter;
	Params->TryGetStringField(TEXT("filter"), Filter);
	const FString FilterLower = Filter.ToLower();

	FString Category;
	Params->TryGetStringField(TEXT("category"), Category);

	int32 MaxDepth = 1;
	double DepthD = 0.0;
	if (Params->TryGetNumberField(TEXT("depth"), DepthD)) { MaxDepth = static_cast<int32>(DepthD); }

	bool bExpandEnums = true;
	Params->TryGetBoolField(TEXT("expand_enums"), bExpandEnums);

	bool bIncludeInherited = true;
	Params->TryGetBoolField(TEXT("include_inherited"), bIncludeInherited);

	bool bDescendIntoObjects = false;
	Params->TryGetBoolField(TEXT("descend_into_objects"), bDescendIntoObjects);

	int32 MaxEntries = 50;
	double MaxEntriesD = 0.0;
	if (Params->TryGetNumberField(TEXT("max_entries"), MaxEntriesD)) { MaxEntries = static_cast<int32>(MaxEntriesD); }

	int32 Cursor = 0;
	double CursorD = 0.0;
	if (Params->TryGetNumberField(TEXT("cursor"), CursorD)) { Cursor = static_cast<int32>(CursorD); }

	FString ComponentName;
	Params->TryGetStringField(TEXT("component_name"), ComponentName);

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
	}

	AActor* Target = nullptr;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (A && (A->GetActorLabel() == ActorLabel || A->GetName() == ActorLabel))
		{
			Target = A;
			break;
		}
	}
	if (!Target)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Actor not found: '%s'"), *ActorLabel));
	}

	UObject* TargetObject = Target;
	if (!ComponentName.IsEmpty())
	{
		TArray<UActorComponent*> Components;
		Target->GetComponents(Components);
		UActorComponent* Found = nullptr;
		for (UActorComponent* C : Components)
		{
			if (C && (C->GetName() == ComponentName || C->GetClass()->GetName() == ComponentName))
			{
				Found = C;
				break;
			}
		}
		if (!Found)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				FString::Printf(TEXT("Component '%s' not found"), *ComponentName));
		}
		TargetObject = Found;
	}

	FPropertyWalkOptions Opts;
	Opts.FilterLower = FilterLower;
	Opts.CategoryLower = Category.ToLower();
	Opts.MaxDepth = MaxDepth;
	Opts.MaxEntries = MaxEntries;
	Opts.Cursor = Cursor;
	Opts.bExpandEnum = bExpandEnums;
	Opts.bIncludeInherited = bIncludeInherited;
	Opts.bDescendIntoObjects = bDescendIntoObjects;
	Opts.bIncludeMetadata = true;

	TSharedPtr<FJsonObject> Tree = FEpicUnrealMCPPropertyUtils::GetPropertyMetadataTree(
		TargetObject, PropertyPath, Opts);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("actor_name"), Target->GetName());
	Result->SetStringField(TEXT("actor_label"), Target->GetActorLabel());
	Result->SetStringField(TEXT("class"), Target->GetClass()->GetPathName());
	if (!ComponentName.IsEmpty()) { Result->SetStringField(TEXT("component"), ComponentName); }
	if (!PropertyPath.IsEmpty()) { Result->SetStringField(TEXT("property_path"), PropertyPath); }
	Result->SetObjectField(TEXT("metadata"), Tree);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSpawnActorByClass(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ClassPath;
	if (!Params->TryGetStringField(TEXT("class_path"), ClassPath) || ClassPath.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'class_path' parameter"));
	}

	FString DesiredName;
	Params->TryGetStringField(TEXT("name"), DesiredName);

	FVector Location = FVector::ZeroVector;
	FRotator Rotation = FRotator::ZeroRotator;
	FVector Scale = FVector::OneVector;
	if (Params->HasField(TEXT("location"))) { Location = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location")); }
	if (Params->HasField(TEXT("rotation"))) { Rotation = FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")); }
	if (Params->HasField(TEXT("scale"))) { Scale = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("scale")); }

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
	}

	UClass* Class = nullptr;

	// 1. Full class path (contains '.' and starts with '/'): direct find/load.
	if (ClassPath.Contains(TEXT(".")))
	{
		Class = FindObject<UClass>(nullptr, *ClassPath);
		if (!Class) { Class = LoadObject<UClass>(nullptr, *ClassPath); }
	}

	// 2. Blueprint asset path (e.g. "/Game/Blueprints/BP_Foo"): load asset, get GeneratedClass.
	if (!Class && ClassPath.StartsWith(TEXT("/")))
	{
		FString BPPath = ClassPath;
		if (!BPPath.Contains(TEXT(".")))
		{
			FString PathOnly, AssetName;
			BPPath.Split(TEXT("/"), &PathOnly, &AssetName, ESearchCase::IgnoreCase, ESearchDir::FromEnd);
			if (!AssetName.IsEmpty()) { BPPath = ClassPath + TEXT(".") + AssetName; }
		}
		UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *BPPath);
		if (!Asset) { Asset = UEditorAssetLibrary::LoadAsset(ClassPath); }
		if (UBlueprint* BP = Cast<UBlueprint>(Asset))
		{
			Class = BP->GeneratedClass;
		}
		if (!Class)
		{
			// Maybe ClassPath is the GeneratedClass itself (ends with _C)
			Class = LoadObject<UClass>(nullptr, *(ClassPath + TEXT("_C")));
		}
	}

	// 3. Short name fallback — iterate UClass to find by short name.
	if (!Class)
	{
		Class = FEpicUnrealMCPPropertyUtils::ResolveAnyClass(ClassPath);
	}

	if (!Class)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Could not resolve class '%s'"), *ClassPath));
	}
	if (!Class->IsChildOf(AActor::StaticClass()))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Class '%s' is not an AActor subclass"), *Class->GetPathName()));
	}
	if (Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Class '%s' is abstract or deprecated"), *Class->GetPathName()));
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	if (!DesiredName.IsEmpty()) { SpawnParams.Name = FName(*DesiredName); }
	SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;

	AActor* NewActor = nullptr;
	if (UEditorActorSubsystem* EAS = GEditor->GetEditorSubsystem<UEditorActorSubsystem>())
	{
		// Editor subsystem path — registers with transaction system.
		NewActor = EAS->SpawnActorFromClass(Class, Location, Rotation, /*bTransient*/ false);
	}
	if (!NewActor)
	{
		NewActor = World->SpawnActor<AActor>(Class, Location, Rotation, SpawnParams);
	}
	if (!NewActor)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("SpawnActor returned null for class '%s'"), *Class->GetPathName()));
	}

	if (!Scale.Equals(FVector::OneVector))
	{
		NewActor->SetActorScale3D(Scale);
	}
	if (!DesiredName.IsEmpty())
	{
		NewActor->SetActorLabel(DesiredName);
	}

	if (ULevel* Level = NewActor->GetLevel())
	{
		Level->MarkPackageDirty();
	}

	TSharedPtr<FJsonObject> Result = FEpicUnrealMCPCommonUtils::ActorToJsonObject(NewActor, true);
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("class_path"), Class->GetPathName());
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleFindActors(
	const TSharedPtr<FJsonObject>& Params)
{
	FString NamePattern, LabelPattern, ClassFilter, TagFilter;
	Params->TryGetStringField(TEXT("name_pattern"), NamePattern);
	Params->TryGetStringField(TEXT("label_pattern"), LabelPattern);
	Params->TryGetStringField(TEXT("class_filter"), ClassFilter);
	Params->TryGetStringField(TEXT("tag"), TagFilter);

	int32 MaxResults = 100;
	double MaxResultsD = 0.0;
	if (Params->TryGetNumberField(TEXT("max_results"), MaxResultsD)) { MaxResults = static_cast<int32>(MaxResultsD); }

	bool bExactClass = false;
	Params->TryGetBoolField(TEXT("exact_class"), bExactClass);

	bool bIncludeTransform = false;
	Params->TryGetBoolField(TEXT("include_transform"), bIncludeTransform);

	const FString NameLower = NamePattern.ToLower();
	const FString LabelLower = LabelPattern.ToLower();
	const FString ClassLower = ClassFilter.ToLower();

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to get editor world"));
	}

	// Resolve class filter as a UClass if it looks like a real class (so we can do IsChildOf).
	UClass* ClassFilterClass = nullptr;
	if (!ClassFilter.IsEmpty())
	{
		ClassFilterClass = FEpicUnrealMCPPropertyUtils::ResolveAnyClass(ClassFilter);
	}

	TArray<TSharedPtr<FJsonValue>> ActorArr;
	int32 TotalScanned = 0;
	int32 TotalMatched = 0;

	for (TActorIterator<AActor> It(World); It; ++It)
	{
		AActor* A = *It;
		if (!A) { continue; }
		++TotalScanned;

		if (!NameLower.IsEmpty() && !A->GetName().ToLower().Contains(NameLower)) { continue; }
		if (!LabelLower.IsEmpty() && !A->GetActorLabel().ToLower().Contains(LabelLower)) { continue; }

		if (!ClassFilter.IsEmpty())
		{
			if (ClassFilterClass)
			{
				if (bExactClass)
				{
					if (A->GetClass() != ClassFilterClass) { continue; }
				}
				else
				{
					if (!A->IsA(ClassFilterClass)) { continue; }
				}
			}
			else
			{
				const FString ClassNameLower = A->GetClass()->GetName().ToLower();
				const FString ClassPathLower = A->GetClass()->GetPathName().ToLower();
				if (!ClassNameLower.Contains(ClassLower) && !ClassPathLower.Contains(ClassLower)) { continue; }
			}
		}

		if (!TagFilter.IsEmpty() && !A->Tags.Contains(FName(*TagFilter))) { continue; }

		++TotalMatched;
		if (ActorArr.Num() >= MaxResults && MaxResults > 0) { continue; }

		TSharedPtr<FJsonObject> Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("name"), A->GetName());
		Entry->SetStringField(TEXT("label"), A->GetActorLabel());
		Entry->SetStringField(TEXT("class"), A->GetClass()->GetPathName());
		Entry->SetStringField(TEXT("class_short"), A->GetClass()->GetName());
		if (bIncludeTransform)
		{
			const FVector L = A->GetActorLocation();
			const FRotator R = A->GetActorRotation();
			const FVector S = A->GetActorScale3D();
			TArray<TSharedPtr<FJsonValue>> Loc = { MakeShared<FJsonValueNumber>(L.X), MakeShared<FJsonValueNumber>(L.Y), MakeShared<FJsonValueNumber>(L.Z) };
			TArray<TSharedPtr<FJsonValue>> Rot = { MakeShared<FJsonValueNumber>(R.Pitch), MakeShared<FJsonValueNumber>(R.Yaw), MakeShared<FJsonValueNumber>(R.Roll) };
			TArray<TSharedPtr<FJsonValue>> Scl = { MakeShared<FJsonValueNumber>(S.X), MakeShared<FJsonValueNumber>(S.Y), MakeShared<FJsonValueNumber>(S.Z) };
			Entry->SetArrayField(TEXT("location"), Loc);
			Entry->SetArrayField(TEXT("rotation"), Rot);
			Entry->SetArrayField(TEXT("scale"), Scl);
		}
		if (A->Tags.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> Tags;
			for (const FName& T : A->Tags) { Tags.Add(MakeShared<FJsonValueString>(T.ToString())); }
			Entry->SetArrayField(TEXT("tags"), Tags);
		}
		ActorArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetNumberField(TEXT("total_scanned"), TotalScanned);
	Result->SetNumberField(TEXT("total_matched"), TotalMatched);
	Result->SetNumberField(TEXT("returned"), ActorArr.Num());
	Result->SetBoolField(TEXT("truncated"), TotalMatched > ActorArr.Num());
	Result->SetArrayField(TEXT("actors"), ActorArr);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleTakeScreenshot(
	const TSharedPtr<FJsonObject>& Params)
{
	FString OutputPath;
	if (!Params->TryGetStringField(TEXT("file_path"), OutputPath) || OutputPath.IsEmpty())
	{
		OutputPath = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("MCP_Screenshot.png");
	}

	// "viewport" = level viewport only, "window" = full editor window
	FString Mode = TEXT("viewport");
	Params->TryGetStringField(TEXT("mode"), Mode);

	IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);

	TArray<FColor> Pixels;
	int32 Width = 0;
	int32 Height = 0;

	if (Mode == TEXT("window"))
	{
		// Find the active editor window
		TSharedPtr<SWindow> Window = FSlateApplication::Get().GetActiveTopLevelWindow();

		// Fall back to the first visible window if editor lost focus
		if (!Window.IsValid())
		{
			TArray<TSharedRef<SWindow>> AllWindows;
			FSlateApplication::Get().GetAllVisibleWindowsOrdered(AllWindows);
			if (AllWindows.Num() > 0)
			{
				Window = AllWindows[0];
			}
		}

		if (!Window.IsValid())
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No editor window found"));
		}

		// Use Slate TakeScreenshot — cross-platform, captures the full editor window
		// including BP graphs, material editors, data tables, and all Slate UI
		FIntVector Size;
		bool bCaptureSuccess = FSlateApplication::Get().TakeScreenshot(
			Window.ToSharedRef(), Pixels, Size);

		if (!bCaptureSuccess || Pixels.Num() == 0)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("Failed to capture editor window"));
		}

		Width = Size.X;
		Height = Size.Y;
	}
	else
	{
		// Capture the active level viewport
		FLevelEditorModule& LevelEditor =
			FModuleManager::GetModuleChecked<FLevelEditorModule>("LevelEditor");
		TSharedPtr<IAssetViewport> ActiveViewport = LevelEditor.GetFirstActiveViewport();
		if (!ActiveViewport.IsValid())
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No active level viewport found"));
		}

		FViewport* Viewport = ActiveViewport->GetActiveViewport();
		if (!Viewport)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("Failed to get viewport render target"));
		}

		// Force a fresh frame before reading pixels: scene mutations made via
		// python/property writes since the last draw otherwise come back as a
		// STALE image. Viewport->Draw() renders synchronously (a blocking command
		// never ticks, so an invalidate-for-next-tick alone isn't enough).
		if (GEditor)
		{
			GEditor->RedrawLevelEditingViewports(/*bInvalidateHitProxies=*/true);
		}
		Viewport->Draw();
		FlushRenderingCommands();

		const FIntPoint Size = Viewport->GetSizeXY();
		if (Size.X <= 0 || Size.Y <= 0)
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("Viewport has invalid size (may be minimized or not yet rendered)"));
		}

		if (!Viewport->ReadPixels(Pixels))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
				TEXT("Failed to read pixels from viewport"));
		}

		Width = Size.X;
		Height = Size.Y;
	}

	// Both viewport and Slate may return alpha = 0
	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}

	// Optional downscale: full viewport-size PNGs are an image-token cost for
	// MCP clients reading the capture; max_width shrinks proportionally
	// before encoding (e.g. 1280 is plenty for verify loops).
	double MaxWidthD = 0.0;
	if (Params->TryGetNumberField(TEXT("max_width"), MaxWidthD) &&
		MaxWidthD >= 64.0 && Width > static_cast<int32>(MaxWidthD))
	{
		const int32 DstWidth = static_cast<int32>(MaxWidthD);
		const int32 DstHeight = FMath::Max(
			1, static_cast<int32>(static_cast<int64>(Height) * DstWidth / Width));
		TArray<FColor> Resized;
		FImageUtils::ImageResize(Width, Height, Pixels, DstWidth, DstHeight,
			Resized, /*bLinearSpace=*/false, /*bForceOpaqueOutput=*/true);
		Pixels = MoveTemp(Resized);
		Width = DstWidth;
		Height = DstHeight;
	}

	IImageWrapperModule& ImageWrapperModule =
		FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> PngWriter =
		ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!PngWriter.IsValid() ||
		!PngWriter->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor),
			Width, Height, ERGBFormat::BGRA, 8))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Failed to compress image to PNG"));
	}

	const TArray64<uint8> PNGData = PngWriter->GetCompressed();
	if (!FFileHelper::SaveArrayToFile(PNGData, *OutputPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Failed to save screenshot to: %s"), *OutputPath));
	}

	// Absolute path so callers can read the file directly
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("file_path"), FPaths::ConvertRelativePathToFull(OutputPath));
	Result->SetNumberField(TEXT("width"), Width);
	Result->SetNumberField(TEXT("height"), Height);
	return Result;
}

// ---------------------------------------------------------------------------
// PIE control
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleStartPIE(
	const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
	}
	if (GEditor->PlayWorld)
	{
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("already_running"), true);
		Result->SetStringField(TEXT("message"), TEXT("A PIE session is already running"));
		return Result;
	}

	FRequestPlaySessionParams SessionParams;
	// Simulate-in-editor keeps the editor camera instead of possessing a pawn.
	bool bSimulate = false;
	Params->TryGetBoolField(TEXT("simulate"), bSimulate);
	if (bSimulate)
	{
		SessionParams.WorldType = EPlaySessionWorldType::SimulateInEditor;
	}
	GEditor->RequestPlaySession(SessionParams);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("requested"), true);
	Result->SetStringField(TEXT("message"),
		TEXT("PIE session requested — starts on the next editor tick; poll get_pie_status"));
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleStopPIE(
	const TSharedPtr<FJsonObject>& Params)
{
	if (!GEditor)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("GEditor unavailable"));
	}
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	if (!GEditor->PlayWorld)
	{
		Result->SetBoolField(TEXT("success"), true);
		Result->SetBoolField(TEXT("was_running"), false);
		Result->SetStringField(TEXT("message"), TEXT("No PIE session was running"));
		return Result;
	}
	GEditor->RequestEndPlayMap();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("was_running"), true);
	Result->SetStringField(TEXT("message"),
		TEXT("PIE stop requested — ends on the next editor tick; poll get_pie_status"));
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleGetPIEStatus(
	const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	const bool bRunning = GEditor && GEditor->PlayWorld != nullptr;
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("pie_active"), bRunning);
	if (bRunning)
	{
		Result->SetStringField(TEXT("play_world"), GEditor->PlayWorld->GetName());
		Result->SetBoolField(TEXT("simulating"), GEditor->bIsSimulatingInEditor);
	}
	return Result;
}

// ---------------------------------------------------------------------------
// Viewport control
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleSetViewportCamera(
	const TSharedPtr<FJsonObject>& Params)
{
	FLevelEditorViewportClient* Client = nullptr;
	for (FLevelEditorViewportClient* Candidate : GEditor->GetLevelViewportClients())
	{
		if (Candidate && Candidate->IsPerspective())
		{
			Client = Candidate;
			break;
		}
	}
	if (!Client)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No perspective level viewport found"));
	}

	const FVector Location = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("location"));
	Client->SetViewLocation(Location);

	if (Params->HasField(TEXT("look_at")))
	{
		const FVector LookAt = FEpicUnrealMCPCommonUtils::GetVectorFromJson(Params, TEXT("look_at"));
		Client->SetViewRotation((LookAt - Location).Rotation());
	}
	else if (Params->HasField(TEXT("rotation")))
	{
		Client->SetViewRotation(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(Params, TEXT("rotation")));
	}

	Client->Invalidate();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("location"), Client->GetViewLocation().ToString());
	Result->SetStringField(TEXT("rotation"), Client->GetViewRotation().ToString());
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleFrameActor(
	const TSharedPtr<FJsonObject>& Params)
{
	FString ActorRef;
	if (!Params->TryGetStringField(TEXT("actor"), ActorRef) &&
		!Params->TryGetStringField(TEXT("actor_label"), ActorRef) &&
		!Params->TryGetStringField(TEXT("name"), ActorRef))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'actor' parameter"));
	}

	AActor* Actor = FEpicUnrealMCPCommonUtils::ResolveActor(ActorRef);
	if (!Actor)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Actor not found (by label, name, or path): %s"), *ActorRef));
	}

	// Same as pressing F in the viewport — frames the actor's bounds.
	GEditor->MoveViewportCamerasToActor(*Actor, /*bActiveViewportOnly=*/false);

	// Degenerate bounds mean the camera framed effectively nothing (an
	// empty mesh component, an unconfigured actor) — say so instead of
	// letting the follow-up screenshot show unexplained skybox.
	FVector Origin, Extent;
	Actor->GetActorBounds(/*bOnlyCollidingComponents=*/false, Origin, Extent,
		/*bIncludeFromChildActors=*/true);
	const double BoundsRadius = Extent.Length();

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("framed"), Actor->GetActorLabel());
	Result->SetNumberField(TEXT("bounds_radius"), BoundsRadius);
	if (BoundsRadius < 10.0)
	{
		Result->SetStringField(TEXT("warning"), FString::Printf(
			TEXT("Actor bounds radius is only %.1fcm — the viewport may be "
			     "framing empty space. Is the actor's mesh/component set?"),
			BoundsRadius));
	}
	return Result;
}

// ---------------------------------------------------------------------------
// Console
// ---------------------------------------------------------------------------

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleExecuteConsoleCommand(
	const TSharedPtr<FJsonObject>& Params)
{
	FString Command;
	if (!Params->TryGetStringField(TEXT("command"), Command) || Command.IsEmpty())
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'command' parameter"));
	}

	UWorld* World = FEpicUnrealMCPCommonUtils::GetEditorWorld();
	const bool bHandled = GEngine && GEngine->Exec(World, *Command);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("handled"), bHandled);
	Result->SetStringField(TEXT("command"), Command);
	if (!bHandled)
	{
		Result->SetStringField(TEXT("note"),
			TEXT("Engine reported the command as unhandled — it may still have been consumed by a cvar sink"));
	}
	return Result;
}

// ---------------------------------------------------------------------------
// Shot capture — camera stills + contact sheets
// ---------------------------------------------------------------------------

namespace
{
	// The perspective viewport client we both AIM and CAPTURE (must be the same
	// one — GetLevelViewportClients()[persp] and GetFirstActiveViewport() can be
	// different viewports, which made every capture show the untouched view).
	FLevelEditorViewportClient* MCP_GetCaptureClient()
	{
		if (!GEditor) return nullptr;
		FViewport* Active = GEditor->GetActiveViewport();
		FLevelEditorViewportClient* Fallback = nullptr;
		for (FLevelEditorViewportClient* C : GEditor->GetLevelViewportClients())
		{
			if (C && C->IsPerspective() && C->Viewport)
			{
				if (C->Viewport == Active) return C;   // the one actually on screen
				if (!Fallback) Fallback = C;
			}
		}
		return Fallback;
	}

	// Render THIS client's viewport synchronously and read its pixels.
	// WarmupFrames > 1 lets TSR/Lumen accumulate on a freshly-aimed view — a
	// single Draw() of a just-moved camera is under-converged (dark/banded).
	bool MCP_CaptureClient(FLevelEditorViewportClient* Client, int32 WarmupFrames,
		TArray<FColor>& OutPixels, int32& OutW, int32& OutH, FString& OutErr)
	{
		if (!Client || !Client->Viewport) { OutErr = TEXT("Client has no viewport"); return false; }
		FViewport* Viewport = Client->Viewport;

		// Draw() renders SYNCHRONOUSLY (RedrawLevelEditingViewports only marks a
		// redraw for a future tick, which never happens inside a blocking command).
		const int32 Frames = FMath::Clamp(WarmupFrames, 1, 128);
		for (int32 i = 0; i < Frames; ++i)
		{
			Client->Invalidate();
			Viewport->Draw();
		}
		FlushRenderingCommands();

		const FIntPoint Size = Viewport->GetSizeXY();
		if (Size.X <= 0 || Size.Y <= 0) { OutErr = TEXT("Viewport has invalid size"); return false; }
		if (!Viewport->ReadPixels(OutPixels)) { OutErr = TEXT("Failed to read pixels"); return false; }
		OutW = Size.X; OutH = Size.Y;
		for (FColor& Px : OutPixels) { Px.A = 255; }
		return true;
	}

	bool MCP_SavePixelsPng(const TArray<FColor>& Pixels, int32 W, int32 H, const FString& Path, FString& OutErr)
	{
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		IImageWrapperModule& Mod =
			FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
		TSharedPtr<IImageWrapper> Png = Mod.CreateImageWrapper(EImageFormat::PNG);
		if (!Png.IsValid() ||
			!Png->SetRaw(Pixels.GetData(), (int64)Pixels.Num() * sizeof(FColor), W, H, ERGBFormat::BGRA, 8))
		{ OutErr = TEXT("Failed to encode PNG"); return false; }
		if (!FFileHelper::SaveArrayToFile(Png->GetCompressed(), *Path))
		{ OutErr = FString::Printf(TEXT("Failed to save: %s"), *Path); return false; }
		return true;
	}

	// Point the viewport at a view spec: {camera:<ref>} OR {location, look_at|rotation, fov}.
	// Returns a human label for the view. Empty return = failure (OutErr set).
	FString MCP_AimViewport(FLevelEditorViewportClient* Client,
		const TSharedPtr<FJsonObject>& View, FString& OutErr)
	{
		FString CamRef;
		if (View->TryGetStringField(TEXT("camera"), CamRef) && !CamRef.IsEmpty())
		{
			AActor* CamActor = FEpicUnrealMCPCommonUtils::ResolveActor(CamRef);
			if (!CamActor) { OutErr = FString::Printf(TEXT("Camera not found: %s"), *CamRef); return FString(); }
			UCameraComponent* Cam = CamActor->FindComponentByClass<UCameraComponent>();
			if (!Cam) { OutErr = FString::Printf(TEXT("No CameraComponent on %s"), *CamRef); return FString(); }
			FMinimalViewInfo VI;
			Cam->GetCameraView(0.0f, VI);
			Client->SetViewLocation(VI.Location);
			Client->SetViewRotation(VI.Rotation);
			Client->ViewFOV = VI.FOV;
			Client->FOVAngle = VI.FOV;
			return CamActor->GetActorLabel();
		}

		if (!View->HasField(TEXT("location")))
		{ OutErr = TEXT("View needs 'camera' or 'location'"); return FString(); }
		const FVector Loc = FEpicUnrealMCPCommonUtils::GetVectorFromJson(View, TEXT("location"));
		Client->SetViewLocation(Loc);
		if (View->HasField(TEXT("look_at")))
		{
			const FVector LookAt = FEpicUnrealMCPCommonUtils::GetVectorFromJson(View, TEXT("look_at"));
			Client->SetViewRotation((LookAt - Loc).Rotation());
		}
		else if (View->HasField(TEXT("rotation")))
		{
			Client->SetViewRotation(FEpicUnrealMCPCommonUtils::GetRotatorFromJson(View, TEXT("rotation")));
		}
		double Fov = 0.0;
		if (View->TryGetNumberField(TEXT("fov"), Fov) && Fov > 1.0)
		{
			Client->ViewFOV = (float)Fov;
			Client->FOVAngle = (float)Fov;
		}
		FString Label;
		View->TryGetStringField(TEXT("label"), Label);
		return Label.IsEmpty() ? TEXT("view") : Label;
	}
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleCaptureFromCamera(
	const TSharedPtr<FJsonObject>& Params)
{
	FString CamRef;
	if (!Params->TryGetStringField(TEXT("camera"), CamRef) &&
		!Params->TryGetStringField(TEXT("camera_label"), CamRef) &&
		!Params->TryGetStringField(TEXT("name"), CamRef))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'camera' parameter"));
	}

	FLevelEditorViewportClient* Client = MCP_GetCaptureClient();
	if (!Client) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No perspective level viewport"));

	TSharedPtr<FJsonObject> View = MakeShared<FJsonObject>();
	View->SetStringField(TEXT("camera"), CamRef);
	FString Err;
	const FString Label = MCP_AimViewport(Client, View, Err);
	if (Label.IsEmpty()) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

	// Clean shot: game view (no gizmos/grid), restored afterwards.
	bool bGameView = true;
	Params->TryGetBoolField(TEXT("game_view"), bGameView);
	const bool bWasGameView = Client->IsInGameView();
	Client->SetGameView(bGameView);
	Client->Invalidate();

	TArray<FColor> Pixels; int32 W = 0, H = 0;
	double WarmupD = 16.0;
	Params->TryGetNumberField(TEXT("warmup_frames"), WarmupD);
	const bool bOk = MCP_CaptureClient(Client, (int32)WarmupD, Pixels, W, H, Err);
	Client->SetGameView(bWasGameView);
	if (!bOk) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

	double MaxWidthD = 0.0;
	if (Params->TryGetNumberField(TEXT("max_width"), MaxWidthD) && MaxWidthD >= 64.0 && W > (int32)MaxWidthD)
	{
		const int32 DstW = (int32)MaxWidthD;
		const int32 DstH = FMath::Max(1, (int32)((int64)H * DstW / W));
		TArray<FColor> Resized;
		FImageUtils::ImageResize(W, H, Pixels, DstW, DstH, Resized, false, true);
		Pixels = MoveTemp(Resized); W = DstW; H = DstH;
	}

	FString OutPath;
	if (!Params->TryGetStringField(TEXT("file_path"), OutPath) || OutPath.IsEmpty())
	{
		OutPath = FPaths::ProjectSavedDir() / TEXT("Screenshots") /
			FString::Printf(TEXT("Cam_%s.png"), *Label);
	}
	if (!MCP_SavePixelsPng(Pixels, W, H, OutPath, Err))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("camera"), Label);
	Result->SetStringField(TEXT("file_path"), FPaths::ConvertRelativePathToFull(OutPath));
	Result->SetNumberField(TEXT("width"), W);
	Result->SetNumberField(TEXT("height"), H);
	return Result;
}

TSharedPtr<FJsonObject> FEpicUnrealMCPEditorCommands::HandleContactSheet(
	const TSharedPtr<FJsonObject>& Params)
{
	const TArray<TSharedPtr<FJsonValue>>* Views = nullptr;
	if (!Params->TryGetArrayField(TEXT("views"), Views) || Views->Num() == 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			TEXT("Missing non-empty 'views' array (each: {camera:<ref>} or {location,look_at|rotation,fov,label})"));
	}

	FLevelEditorViewportClient* Client = MCP_GetCaptureClient();
	if (!Client) return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No perspective level viewport"));

	int32 CellW = 480;
	{ double d; if (Params->TryGetNumberField(TEXT("cell_width"), d)) CellW = FMath::Clamp((int32)d, 64, 1920); }

	bool bGameView = true;
	Params->TryGetBoolField(TEXT("game_view"), bGameView);
	const bool bWasGameView = Client->IsInGameView();
	Client->SetGameView(bGameView);

	double WarmupD = 16.0;
	Params->TryGetNumberField(TEXT("warmup_frames"), WarmupD);
	const int32 WarmupFrames = (int32)WarmupD;

	TArray<TArray<FColor>> Cells;
	TArray<TSharedPtr<FJsonValue>> Labels;
	int32 CellH = 0;
	FString FirstErr;

	for (const TSharedPtr<FJsonValue>& V : *Views)
	{
		const TSharedPtr<FJsonObject> View = V->AsObject();
		if (!View.IsValid()) continue;

		FString Err;
		const FString Label = MCP_AimViewport(Client, View, Err);
		if (Label.IsEmpty()) { if (FirstErr.IsEmpty()) FirstErr = Err; continue; }
		Client->Invalidate();

		TArray<FColor> Px; int32 W = 0, H = 0;
		if (!MCP_CaptureClient(Client, WarmupFrames, Px, W, H, Err)) { if (FirstErr.IsEmpty()) FirstErr = Err; continue; }

		if (CellH == 0) CellH = FMath::Max(1, (int32)((int64)CellW * H / W));
		TArray<FColor> Cell;
		FImageUtils::ImageResize(W, H, Px, CellW, CellH, Cell, false, true);
		Cells.Add(MoveTemp(Cell));
		Labels.Add(MakeShared<FJsonValueString>(Label));
	}

	Client->SetGameView(bWasGameView);

	if (Cells.Num() == 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("No views captured. First error: %s"),
				FirstErr.IsEmpty() ? TEXT("(none)") : *FirstErr));
	}

	const int32 N = Cells.Num();
	int32 Cols = FMath::CeilToInt(FMath::Sqrt((float)N));
	{ double d; if (Params->TryGetNumberField(TEXT("columns"), d) && d >= 1.0) Cols = (int32)d; }
	Cols = FMath::Clamp(Cols, 1, N);
	const int32 Rows = FMath::DivideAndRoundUp(N, Cols);

	const int32 GridW = Cols * CellW;
	const int32 GridH = Rows * CellH;
	TArray<FColor> Grid;
	Grid.Init(FColor(12, 12, 14, 255), GridW * GridH);

	for (int32 i = 0; i < N; ++i)
	{
		const int32 r = i / Cols;
		const int32 c = i % Cols;
		const TArray<FColor>& Cell = Cells[i];
		for (int32 y = 0; y < CellH; ++y)
		{
			const int32 SrcOffset = y * CellW;
			const int32 DstOffset = (r * CellH + y) * GridW + c * CellW;
			FMemory::Memcpy(&Grid[DstOffset], &Cell[SrcOffset], CellW * sizeof(FColor));
		}
	}

	FString OutPath;
	if (!Params->TryGetStringField(TEXT("file_path"), OutPath) || OutPath.IsEmpty())
	{
		OutPath = FPaths::ProjectSavedDir() / TEXT("Screenshots") / TEXT("ContactSheet.png");
	}
	FString Err;
	if (!MCP_SavePixelsPng(Grid, GridW, GridH, OutPath, Err))
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(Err);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("file_path"), FPaths::ConvertRelativePathToFull(OutPath));
	Result->SetNumberField(TEXT("width"), GridW);
	Result->SetNumberField(TEXT("height"), GridH);
	Result->SetNumberField(TEXT("columns"), Cols);
	Result->SetNumberField(TEXT("rows"), Rows);
	Result->SetNumberField(TEXT("cell_width"), CellW);
	Result->SetNumberField(TEXT("cell_height"), CellH);
	Result->SetArrayField(TEXT("cells_row_major"), Labels);
	return Result;
}
