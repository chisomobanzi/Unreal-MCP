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
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_Inertialization.h"
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
	else if (CommandType == TEXT("ensure_gesture_layer"))
	{
		return HandleEnsureGestureLayer(Params);
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

TSharedPtr<FJsonObject> FEpicUnrealMCPBlueprintGraphCommands::HandleEnsureGestureLayer(
	const TSharedPtr<FJsonObject>& Params)
{
	// Insert an upper-body gesture layer downstream of an existing base Slot:
	//   base Slot ──┬─────────────────────────► LayeredBoneBlend.BasePose
	//               └─► Slot(upper_slot) ─────► LayeredBoneBlend.BlendPoses_0
	//   LayeredBoneBlend ─► Inertialization ─► (whatever the base Slot fed)
	// The blend is masked from mask_bone up, so upper-body montages layer over
	// the base pose without touching the legs; the Inertialization node serves
	// every montage that requests inertial blending. Idempotent on upper_slot.
	FString BlueprintPath;
	if (!Params->TryGetStringField(TEXT("blueprint_path"), BlueprintPath))
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Missing 'blueprint_path'"));
	}
	FString BaseSlot = TEXT("DefaultSlot");
	FString UpperSlot = TEXT("UpperBodySlot");
	FString MaskBone = TEXT("J_Bip_C_Spine");
	Params->TryGetStringField(TEXT("base_slot"), BaseSlot);
	Params->TryGetStringField(TEXT("upper_slot"), UpperSlot);
	Params->TryGetStringField(TEXT("mask_bone"), MaskBone);
	int32 BlendDepth = 0;
	if (Params->HasTypedField<EJson::Number>(TEXT("blend_depth")))
	{
		BlendDepth = static_cast<int32>(Params->GetNumberField(TEXT("blend_depth")));
	}

	UAnimBlueprint* AnimBP = LoadObject<UAnimBlueprint>(nullptr, *BlueprintPath);
	if (!AnimBP)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Not an AnimBlueprint: %s"), *BlueprintPath));
	}

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

	UAnimGraphNode_Slot* BaseSlotNode = nullptr;
	for (UEdGraphNode* Node : AnimGraph->Nodes)
	{
		if (UAnimGraphNode_Slot* SlotNode = Cast<UAnimGraphNode_Slot>(Node))
		{
			if (SlotNode->Node.SlotName == FName(*UpperSlot))
			{
				TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
				Result->SetBoolField(TEXT("already_present"), true);
				return FEpicUnrealMCPCommonUtils::CreateSuccessResponse(Result);
			}
			if (SlotNode->Node.SlotName == FName(*BaseSlot))
			{
				BaseSlotNode = SlotNode;
			}
		}
	}
	if (!BaseSlotNode)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(
			FString::Printf(TEXT("Base slot '%s' not found — run ensure_anim_slot first"), *BaseSlot));
	}

	UEdGraphPin* BaseOut = nullptr;
	for (UEdGraphPin* Pin : BaseSlotNode->Pins)
	{
		if (Pin->Direction == EGPD_Output) { BaseOut = Pin; break; }
	}
	if (!BaseOut || BaseOut->LinkedTo.Num() == 0)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Base slot output is unlinked"));
	}
	UEdGraphPin* Downstream = BaseOut->LinkedTo[0];

	// Upper-body slot node.
	UAnimGraphNode_Slot* UpperSlotNode = NewObject<UAnimGraphNode_Slot>(AnimGraph);
	UpperSlotNode->Node.SlotName = FName(*UpperSlot);
	UpperSlotNode->CreateNewGuid();
	AnimGraph->AddNode(UpperSlotNode, false, false);
	UpperSlotNode->PostPlacedNewNode();
	UpperSlotNode->AllocateDefaultPins();
	UpperSlotNode->NodePosX = BaseSlotNode->NodePosX + 200;
	UpperSlotNode->NodePosY = BaseSlotNode->NodePosY + 160;

	// Layered blend per bone, one layer masked at MaskBone.
	UAnimGraphNode_LayeredBoneBlend* BlendNode = NewObject<UAnimGraphNode_LayeredBoneBlend>(AnimGraph);
	BlendNode->CreateNewGuid();
	AnimGraph->AddNode(BlendNode, false, false);
	BlendNode->PostPlacedNewNode();
	BlendNode->Node.AddPose();
	if (BlendNode->Node.LayerSetup.Num() == 0)
	{
		BlendNode->Node.LayerSetup.AddDefaulted();
	}
	FBranchFilter Branch;
	Branch.BoneName = FName(*MaskBone);
	Branch.BlendDepth = BlendDepth;
	BlendNode->Node.LayerSetup[0].BranchFilters.Reset();
	BlendNode->Node.LayerSetup[0].BranchFilters.Add(Branch);
	if (BlendNode->Node.BlendWeights.Num() > 0)
	{
		BlendNode->Node.BlendWeights[0] = 1.0f;
	}
	BlendNode->Node.bMeshSpaceRotationBlend = true;
	BlendNode->ReconstructNode();
	BlendNode->NodePosX = BaseSlotNode->NodePosX + 420;
	BlendNode->NodePosY = BaseSlotNode->NodePosY + 60;

	// Inertialization after the blend, before the original downstream chain.
	UAnimGraphNode_Inertialization* InertNode = NewObject<UAnimGraphNode_Inertialization>(AnimGraph);
	InertNode->CreateNewGuid();
	AnimGraph->AddNode(InertNode, false, false);
	InertNode->PostPlacedNewNode();
	InertNode->AllocateDefaultPins();
	InertNode->NodePosX = BaseSlotNode->NodePosX + 640;
	InertNode->NodePosY = BaseSlotNode->NodePosY + 60;

	auto FindPin = [](UEdGraphNode* Node, EEdGraphPinDirection Dir, const TCHAR* NameContains) -> UEdGraphPin*
	{
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == Dir &&
				(!NameContains || Pin->PinName.ToString().Contains(NameContains)))
			{
				return Pin;
			}
		}
		return nullptr;
	};

	UEdGraphPin* UpperIn = FindPin(UpperSlotNode, EGPD_Input, nullptr);
	UEdGraphPin* UpperOut = FindPin(UpperSlotNode, EGPD_Output, nullptr);
	UEdGraphPin* BlendBase = FindPin(BlendNode, EGPD_Input, TEXT("BasePose"));
	UEdGraphPin* BlendPose0 = FindPin(BlendNode, EGPD_Input, TEXT("BlendPoses"));
	UEdGraphPin* BlendOut = FindPin(BlendNode, EGPD_Output, nullptr);
	UEdGraphPin* InertIn = FindPin(InertNode, EGPD_Input, nullptr);
	UEdGraphPin* InertOut = FindPin(InertNode, EGPD_Output, nullptr);
	if (!UpperIn || !UpperOut || !BlendBase || !BlendPose0 || !BlendOut || !InertIn || !InertOut)
	{
		return FEpicUnrealMCPCommonUtils::CreateErrorResponse(TEXT("Expected pins missing on new nodes"));
	}

	BaseOut->BreakAllPinLinks();
	BaseOut->MakeLinkTo(BlendBase);
	BaseOut->MakeLinkTo(UpperIn);
	UpperOut->MakeLinkTo(BlendPose0);
	BlendOut->MakeLinkTo(InertIn);
	InertOut->MakeLinkTo(Downstream);

	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(AnimBP);
	FKismetEditorUtilities::CompileBlueprint(AnimBP);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("inserted"), true);
	Result->SetStringField(TEXT("upper_slot"), UpperSlot);
	Result->SetStringField(TEXT("mask_bone"), MaskBone);
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
