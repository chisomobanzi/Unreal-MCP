#pragma once

#include "CoreMinimal.h"
#include "Json.h"

class AActor;
class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
class UK2Node_Event;
class UK2Node_CallFunction;
class UK2Node_VariableGet;
class UK2Node_VariableSet;
class UK2Node_InputAction;
class UK2Node_Self;
class UFunction;

class UNREALMCPBRIDGE_API FEpicUnrealMCPCommonUtils
{
public:
	// JSON
	static TSharedPtr<FJsonObject> CreateErrorResponse(const FString& Message);
	static TSharedPtr<FJsonObject> CreateSuccessResponse(const TSharedPtr<FJsonObject>& Data = nullptr);
	static void GetIntArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<int32>& OutArray);
	static void GetFloatArrayFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName, TArray<float>& OutArray);
	static FVector2D GetVector2DFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);
	static FVector GetVectorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);
	static FRotator GetRotatorFromJson(const TSharedPtr<FJsonObject>& JsonObject, const FString& FieldName);

	// Actor
	static TSharedPtr<FJsonValue> ActorToJson(AActor* Actor);
	static TSharedPtr<FJsonObject> ActorToJsonObject(AActor* Actor, bool bDetailed = false);

	// The EDITOR world context. Never GWorld: during PIE, GWorld points at the
	// play world, so commands using it silently operate on the wrong world.
	static UWorld* GetEditorWorld();

	// Resolve a level actor by display label, internal object name, or path
	// name, on the editor world. Every actor-taking command should use this
	// so label-vs-name behavior is consistent across the tool surface.
	static AActor* ResolveActor(const FString& ActorRef);

	// Blueprint
	static UBlueprint* FindBlueprint(const FString& BlueprintName);
	static UBlueprint* FindBlueprintByName(const FString& BlueprintName);
	static UEdGraph* FindOrCreateEventGraph(UBlueprint* Blueprint);

	// Blueprint nodes
	static UK2Node_Event* CreateEventNode(UEdGraph* Graph, const FString& EventName, const FVector2D& Position);
	static UK2Node_CallFunction* CreateFunctionCallNode(UEdGraph* Graph, UFunction* Function, const FVector2D& Position);
	static UK2Node_VariableGet* CreateVariableGetNode(
		UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position);
	static UK2Node_VariableSet* CreateVariableSetNode(
		UEdGraph* Graph, UBlueprint* Blueprint, const FString& VariableName, const FVector2D& Position);
	static UK2Node_InputAction* CreateInputActionNode(UEdGraph* Graph, const FString& ActionName, const FVector2D& Position);
	static UK2Node_Self* CreateSelfReferenceNode(UEdGraph* Graph, const FVector2D& Position);
	static bool ConnectGraphNodes(
		UEdGraph* Graph, UEdGraphNode* SourceNode, const FString& SourcePinName,
		UEdGraphNode* TargetNode, const FString& TargetPinName);
	static UEdGraphPin* FindPin(UEdGraphNode* Node, const FString& PinName, EEdGraphPinDirection Direction = EGPD_MAX);
	static UK2Node_Event* FindExistingEventNode(UEdGraph* Graph, const FString& EventName);

	// Property
	static bool SetObjectProperty(
		UObject* Object, const FString& PropertyName,
		const TSharedPtr<FJsonValue>& Value, FString& OutErrorMessage);
};
