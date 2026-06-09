// Header for creating utility nodes (Print, CallFunction, Select, SpawnActor, ComponentBoundEvent, GetSubsystem)

#pragma once

#include "CoreMinimal.h"
#include "EdGraph/EdGraph.h"

class UK2Node;

/**
 * Creator for Unreal Blueprint utility nodes
 */
class FUtilityNodeCreator
{
public:
	static UK2Node* CreatePrintNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
	static UK2Node* CreateCallFunctionNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
	static UK2Node* CreateSelectNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
	static UK2Node* CreateSpawnActorNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
	static UK2Node* CreateComponentBoundEventNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
	static UK2Node* CreateGetSubsystemNode(UEdGraph* Graph, const TSharedPtr<class FJsonObject>& Params);
};
