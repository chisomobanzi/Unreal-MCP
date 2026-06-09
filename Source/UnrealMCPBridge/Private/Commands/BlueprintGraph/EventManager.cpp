#include "Commands/BlueprintGraph/EventManager.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "K2Node_Event.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"

namespace
{
	// Friendly names accepted in place of the Receive*-style internal names
	const TCHAR* ResolveEventAlias(const FString& EventName)
	{
		static const TMap<FString, const TCHAR*> Aliases = {
			{ TEXT("BeginPlay"),          TEXT("ReceiveBeginPlay") },
			{ TEXT("EndPlay"),            TEXT("ReceiveEndPlay") },
			{ TEXT("Tick"),               TEXT("ReceiveTick") },
			{ TEXT("ActorBeginOverlap"),  TEXT("ReceiveActorBeginOverlap") },
			{ TEXT("ActorEndOverlap"),    TEXT("ReceiveActorEndOverlap") },
			{ TEXT("Hit"),                TEXT("ReceiveHit") },
			{ TEXT("Destroyed"),          TEXT("ReceiveDestroyed") },
			{ TEXT("AnyDamage"),          TEXT("ReceiveAnyDamage") },
		};
		const TCHAR* const* Found = Aliases.Find(EventName);
		return Found ? *Found : nullptr;
	}
}

TSharedPtr<FJsonObject> FEventManager::AddEventNode(const TSharedPtr<FJsonObject>& Params)
{
	// Validate parameters
	if (!Params.IsValid())
	{
		return CreateErrorResponse(TEXT("Invalid parameters"));
	}

	// Get required parameters
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		return CreateErrorResponse(TEXT("Missing 'event_name' parameter"));
	}

	// Get optional position parameters
	FVector2D Position(0.0f, 0.0f);
	double PosX = 0.0, PosY = 0.0;
	if (Params->TryGetNumberField(TEXT("pos_x"), PosX))
	{
		Position.X = static_cast<float>(PosX);
	}
	if (Params->TryGetNumberField(TEXT("pos_y"), PosY))
	{
		Position.Y = static_cast<float>(PosY);
	}

	// Load the Blueprint
	UBlueprint* Blueprint = LoadBlueprint(BlueprintName);
	if (!Blueprint)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	// Get the event graph (events can only exist in the event graph)
	if (Blueprint->UbergraphPages.Num() == 0)
	{
		return CreateErrorResponse(TEXT("Blueprint has no event graph"));
	}

	UEdGraph* Graph = Blueprint->UbergraphPages[0];
	if (!Graph)
	{
		return CreateErrorResponse(TEXT("Failed to get Blueprint event graph"));
	}

	// Create the event node
	UK2Node_Event* EventNode = CreateEventNode(Graph, EventName, Position);
	if (!EventNode)
	{
		return CreateErrorResponse(FString::Printf(TEXT("Failed to create event node: %s"), *EventName));
	}

	// Notify changes
	Graph->NotifyGraphChanged();
	FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);

	return CreateSuccessResponse(EventNode);
}

UK2Node_Event* FEventManager::CreateEventNode(UEdGraph* Graph, const FString& EventName, const FVector2D& Position)
{
	if (!Graph)
	{
		return nullptr;
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint)
	{
		return nullptr;
	}

	// Check for existing event node to avoid duplicates (also under the
	// internal name when the caller used a friendly alias)
	UK2Node_Event* ExistingNode = FindExistingEventNode(Graph, EventName);
	if (!ExistingNode)
	{
		if (const TCHAR* Alias = ResolveEventAlias(EventName))
		{
			ExistingNode = FindExistingEventNode(Graph, Alias);
		}
	}
	if (ExistingNode)
	{
		UE_LOG(LogTemp, Display, TEXT("F18: Using existing event node '%s' (ID: %s)"),
			*EventName, *ExistingNode->NodeGuid.ToString());
		return ExistingNode;
	}

	// Create new event node
	UK2Node_Event* EventNode = nullptr;
	UClass* BlueprintClass = Blueprint->GeneratedClass;
	if (!BlueprintClass)
	{
		BlueprintClass = Blueprint->SkeletonGeneratedClass;
	}

	if (!BlueprintClass)
	{
		UE_LOG(LogTemp, Error, TEXT("F18: Blueprint has no generated class"));
		return nullptr;
	}

	// Resolve the event function: direct name, then friendly alias
	// (BeginPlay -> ReceiveBeginPlay etc.). FindFunctionByName walks the
	// super-class chain and implemented interfaces, so parent C++ events,
	// parent-BP events, and interface events all resolve here.
	FName EventFName(*EventName);
	UFunction* EventFunction = BlueprintClass->FindFunctionByName(EventFName);
	if (!EventFunction)
	{
		if (const TCHAR* Alias = ResolveEventAlias(EventName))
		{
			EventFName = FName(Alias);
			EventFunction = BlueprintClass->FindFunctionByName(EventFName);
		}
	}

	if (!EventFunction)
	{
		UE_LOG(LogTemp, Error, TEXT("F18: Failed to find function for event name: %s"), *EventName);
		return nullptr;
	}

	// Only BlueprintImplementableEvent / BlueprintNativeEvent functions can be
	// placed as event nodes. Anything else (a plain BlueprintCallable, a BP
	// function) belongs to CallFunction / custom-event flows instead.
	if (!EventFunction->HasAnyFunctionFlags(FUNC_BlueprintEvent))
	{
		UE_LOG(LogTemp, Error, TEXT("F18: '%s' exists but is not a BlueprintImplementableEvent/NativeEvent — it cannot be placed as an event node"), *EventName);
		return nullptr;
	}

	// An override node must reference the class that DECLARES the event, with
	// bOverrideFunction set — referencing the Blueprint's own generated class
	// without the override flag produces a node that won't compile.
	UClass* DeclaringClass = EventFunction->GetOwnerClass();
	check(DeclaringClass);
	if (DeclaringClass->ClassGeneratedBy == Blueprint)
	{
		// Declared by this Blueprint itself (e.g. an existing custom event) —
		// placing an override of yourself is invalid.
		UE_LOG(LogTemp, Error, TEXT("F18: '%s' is declared by this Blueprint itself; use a custom event node instead"), *EventName);
		return nullptr;
	}
	DeclaringClass = DeclaringClass->GetAuthoritativeClass();

	EventNode = NewObject<UK2Node_Event>(Graph);
	EventNode->EventReference.SetExternalMember(EventFName, DeclaringClass);
	EventNode->bOverrideFunction = true;
	EventNode->NodePosX = static_cast<int32>(Position.X);
	EventNode->NodePosY = static_cast<int32>(Position.Y);
	Graph->AddNode(EventNode, true);
	EventNode->PostPlacedNewNode();
	EventNode->AllocateDefaultPins();

	UE_LOG(LogTemp, Display, TEXT("F18: Created event override node '%s' from %s (ID: %s)"),
		*EventFName.ToString(), *DeclaringClass->GetName(), *EventNode->NodeGuid.ToString());

	return EventNode;
}

UK2Node_Event* FEventManager::FindExistingEventNode(UEdGraph* Graph, const FString& EventName)
{
	if (!Graph)
	{
		return nullptr;
	}

	for (UEdGraphNode* Node : Graph->Nodes)
	{
		UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node);
		if (EventNode && EventNode->EventReference.GetMemberName() == FName(*EventName))
		{
			return EventNode;
		}
	}

	return nullptr;
}

UBlueprint* FEventManager::LoadBlueprint(const FString& BlueprintName)
{
	// Try direct path first
	FString BlueprintPath = BlueprintName;

	// If no path prefix, assume /Game/Blueprints/
	if (!BlueprintPath.StartsWith(TEXT("/")))
	{
		BlueprintPath = TEXT("/Game/Blueprints/") + BlueprintPath;
	}

	// Add .Blueprint suffix if not present
	if (!BlueprintPath.Contains(TEXT(".")))
	{
		BlueprintPath += TEXT(".") + FPaths::GetBaseFilename(BlueprintPath);
	}

	// Try to load the Blueprint
	UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BlueprintPath);

	// If not found, try with UEditorAssetLibrary
	if (!BP)
	{
		FString AssetPath = BlueprintPath;
		if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
		{
			UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
			BP = Cast<UBlueprint>(Asset);
		}
	}

	return BP;
}

TSharedPtr<FJsonObject> FEventManager::CreateSuccessResponse(const UK2Node_Event* EventNode)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), true);
	Response->SetStringField(TEXT("node_id"), EventNode->NodeGuid.ToString());
	Response->SetStringField(TEXT("event_name"), EventNode->EventReference.GetMemberName().ToString());
	Response->SetNumberField(TEXT("pos_x"), EventNode->NodePosX);
	Response->SetNumberField(TEXT("pos_y"), EventNode->NodePosY);
	return Response;
}

TSharedPtr<FJsonObject> FEventManager::CreateErrorResponse(const FString& ErrorMessage)
{
	TSharedPtr<FJsonObject> Response = MakeShareable(new FJsonObject);
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), ErrorMessage);
	return Response;
}
