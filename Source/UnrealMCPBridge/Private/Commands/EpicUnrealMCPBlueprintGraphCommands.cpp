#include "Commands/EpicUnrealMCPBlueprintGraphCommands.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Commands/BlueprintGraph/NodeManager.h"
#include "Commands/BlueprintGraph/BPConnector.h"
#include "Commands/BlueprintGraph/BPVariables.h"
#include "Commands/BlueprintGraph/EventManager.h"
#include "Commands/BlueprintGraph/GraphTracer.h"
#include "Commands/BlueprintGraph/NodeDeleter.h"
#include "Commands/BlueprintGraph/NodePropertyManager.h"
#include "Commands/BlueprintGraph/Function/FunctionManager.h"
#include "Commands/BlueprintGraph/Function/FunctionIO.h"
#include "Commands/BlueprintGraph/Nodes/EventBindingNodes.h"

// ensure_anim_slot — anim-graph surgery is unreachable from Python, so it
// lives here (the same reason the widget-tree commands exist).
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_Root.h"
#include "AnimGraphNode_Slot.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"

FEpicUnrealMCPBlueprintGraphCommands::FEpicUnrealMCPBlueprintGraphCommands()
{
}

FEpicUnrealMCPBlueprintGraphCommands::~FEpicUnrealMCPBlueprintGraphCommands()
{
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleCommand(
	const FString& CommandType,
	const TSharedPtr<FJsonObject>& Params)
{
	if (CommandType == TEXT("add_blueprint_node"))
	{
		return HandleAddBlueprintNode(Params);
	}
	else if (CommandType == TEXT("ensure_anim_slot"))
	{
		return HandleEnsureAnimSlot(Params);
	}
	else if (CommandType == TEXT("connect_nodes"))
	{
		return HandleConnectNodes(Params);
	}
	else if (CommandType == TEXT("disconnect_nodes"))
	{
		return FBPConnector::DisconnectNodes(Params);
	}
	else if (CommandType == TEXT("add_delegate_node"))
	{
		return FEventBindingNodes::AddDelegateNode(Params);
	}
	else if (CommandType == TEXT("add_parent_call_node"))
	{
		return FEventBindingNodes::AddParentCallNode(Params);
	}
	else if (CommandType == TEXT("add_custom_event_node"))
	{
		return FEventBindingNodes::AddCustomEventNode(Params);
	}
	else if (CommandType == TEXT("trace_graph"))
	{
		return FGraphTracer::TraceGraph(Params);
	}
	else if (CommandType == TEXT("create_variable"))
	{
		return HandleCreateVariable(Params);
	}
	else if (CommandType == TEXT("set_blueprint_variable_properties"))
	{
		return HandleSetVariableProperties(Params);
	}
	else if (CommandType == TEXT("add_event_node"))
	{
		return HandleAddEventNode(Params);
	}
	else if (CommandType == TEXT("delete_node"))
	{
		return HandleDeleteNode(Params);
	}
	else if (CommandType == TEXT("set_node_property"))
	{
		return HandleSetNodeProperty(Params);
	}
	else if (CommandType == TEXT("create_function"))
	{
		return HandleCreateFunction(Params);
	}
	else if (CommandType == TEXT("add_function_input"))
	{
		return HandleAddFunctionInput(Params);
	}
	else if (CommandType == TEXT("add_function_output"))
	{
		return HandleAddFunctionOutput(Params);
	}
	else if (CommandType == TEXT("delete_function"))
	{
		return HandleDeleteFunction(Params);
	}
	else if (CommandType == TEXT("rename_function"))
	{
		return HandleRenameFunction(Params);
	}

	return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
		FString::Printf(TEXT("Unknown blueprint graph command: %s"), *CommandType));
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleEnsureAnimSlot(
	const TSharedPtr<FJsonObject>& Params)
{
	// Insert a Slot node (default "DefaultSlot") between the AnimGraph's final
	// pose source and its Root, so montages have somewhere to blend. Idempotent:
	// an existing slot of that name reports success without touching the graph.
	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path'"));
	}
	FString SlotName = TEXT("DefaultSlot");
	Params->TryGetStringField(TEXT("slot_name"), SlotName);

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (!AnimBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Not an AnimBlueprint: %s"), *BlueprintPath));
	}

	// The main anim graph is the function graph literally named AnimGraph.
	UEdGraph* AnimGraph = nullptr;
	for (UEdGraph* Graph : AnimBP->FunctionGraphs)
	{
		if (Graph && Graph->GetFName() == TEXT("AnimGraph"))
		{
			AnimGraph = Graph;
			break;
		}
	}
	if (!AnimGraph)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("No AnimGraph found"));
	}

	UAnimGraphNode_Root* Root = nullptr;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (UAnimGraphNode_Slot* Existing = Cast<UAnimGraphNode_Slot>(Node))
		{
			if (Existing->Node.SlotName == FName(*SlotName))
			{
				TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetBoolField(TEXT("already_present"), true);
				return FEpicUnrealMCPCommonUtils::CreateSuccessResponse(Result);
			}
		}
		if (!Root)
		{
			Root = Cast<UAnimGraphNode_Root>(Node);
		}
	}
	if (!Root)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("AnimGraph has no Root node"));
	}

	UEdGraphPin* RootPoseIn = nullptr;
	for (UEdGraphPin* Pin : Root->Pins)
	{
		if (Pin->Direction == EGPD_Input)
		{
			RootPoseIn = Pin;
			break;
		}
	}
	if (!RootPoseIn || RootPoseIn->LinkedTo.Num() == 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Root pose input is unlinked"));
	}
	UEdGraphPin* Source = RootPoseIn->LinkedTo[0];

	UAnimGraphNode_Slot* SlotNode = NewObject<UAnimGraphNode_Slot>(AnimGraph);
	SlotNode->Node.SlotName = FName(*SlotName);
	SlotNode->CreateNewGuid();
	AnimGraph->AddNode(SlotNode, /*bFromUI*/ false, /*bSelectNewNode*/ false);
	SlotNode->PostPlacedNewNode();
	SlotNode->AllocateDefaultPins();
	SlotNode->NodePosX = Root->NodePosX - 220;
	SlotNode->NodePosY = Root->NodePosY;

	UEdGraphPin* SlotIn = nullptr;
	UEdGraphPin* SlotOut = nullptr;
	for (UEdGraphPin* Pin : SlotNode->Pins)
	{
		if (Pin->Direction == EGPD_Input && !SlotIn) { SlotIn = Pin; }
		if (Pin->Direction == EGPD_Output && !SlotOut) { SlotOut = Pin; }
	}
	if (!SlotIn || !SlotOut)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Slot node pins missing"));
	}

	RootPoseIn->BreakAllPinLinks();
	SlotIn->MakeLinkTo(Source);
	SlotOut->MakeLinkTo(RootPoseIn);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	FKismetEditorUtilities::CompileBlueprint(AnimBP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("inserted"), true);
	Result->SetStringField(TEXT("slot_name"), SlotName);
	Result->SetBoolField(TEXT("compiled_ok"),
		AnimBP->Status != EBlueprintStatus::BS_Error);
	return FEpicUnrealMCPCommonUtils::CreateSuccessResponse(Result);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddBlueprintNode(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString NodeType;
	if (!Params->TryGetStringField(TEXT("node_type"), NodeType))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_type' parameter"));
	}

	return FBlueprintNodeManager::AddNode(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleConnectNodes(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString SourceNodeId;
	if (!Params->TryGetStringField(TEXT("source_node_id"), SourceNodeId))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_node_id' parameter"));
	}

	FString SourcePinName;
	if (!Params->TryGetStringField(TEXT("source_pin_name"), SourcePinName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'source_pin_name' parameter"));
	}

	FString TargetNodeId;
	if (!Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_node_id' parameter"));
	}

	FString TargetPinName;
	if (!Params->TryGetStringField(TEXT("target_pin_name"), TargetPinName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'target_pin_name' parameter"));
	}

	return FBPConnector::ConnectNodes(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleCreateVariable(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
	}

	FString VariableType;
	if (!Params->TryGetStringField(TEXT("variable_type"), VariableType))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_type' parameter"));
	}

	return FBPVariables::CreateVariable(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleSetVariableProperties(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString VariableName;
	if (!Params->TryGetStringField(TEXT("variable_name"), VariableName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'variable_name' parameter"));
	}

	return FBPVariables::SetVariableProperties(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddEventNode(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString EventName;
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'event_name' parameter"));
	}

	return FEventManager::AddEventNode(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteNode(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString NodeID;
	if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
	}

	return FNodeDeleter::DeleteNode(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleSetNodeProperty(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString NodeID;
	if (!Params->TryGetStringField(TEXT("node_id"), NodeID))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'node_id' parameter"));
	}

	// Legacy mode requires property_name; semantic mode uses action instead
	bool bHasAction = Params->HasField(TEXT("action"));
	if (!bHasAction)
	{
		FString PropertyName;
		if (!Params->TryGetStringField(TEXT("property_name"), PropertyName))
		{
			return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'property_name' parameter"));
		}
	}

	return FNodePropertyManager::SetNodeProperty(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleCreateFunction(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	}

	return FFunctionManager::CreateFunction(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionInput(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	}

	FString ParamName;
	if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'param_name' parameter"));
	}

	return FFunctionIO::AddFunctionInput(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleAddFunctionOutput(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	}

	FString ParamName;
	if (!Params->TryGetStringField(TEXT("param_name"), ParamName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'param_name' parameter"));
	}

	return FFunctionIO::AddFunctionOutput(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleDeleteFunction(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString FunctionName;
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'function_name' parameter"));
	}

	return FFunctionManager::DeleteFunction(Params);
}

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleRenameFunction(
	const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_name' parameter"));
	}

	FString OldFunctionName;
	if (!Params->TryGetStringField(TEXT("old_function_name"), OldFunctionName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'old_function_name' parameter"));
	}

	FString NewFunctionName;
	if (!Params->TryGetStringField(TEXT("new_function_name"), NewFunctionName))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'new_function_name' parameter"));
	}

	return FFunctionManager::RenameFunction(Params);
}
