#include "Commands/BlueprintGraph/GraphTracer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_Event.h"
#include "K2Node_CustomEvent.h"
#include "EditorAssetLibrary.h"

namespace
{
	UBlueprint* LoadBlueprintShared(const FString& BlueprintName)
	{
		FString BlueprintPath = BlueprintName;
		if (!BlueprintPath.StartsWith(TEXT("/")))
		{
			BlueprintPath = TEXT("/Game/Blueprints/") + BlueprintPath;
		}
		if (!BlueprintPath.Contains(TEXT(".")))
		{
			BlueprintPath += TEXT(".") + FPaths::GetBaseFilename(BlueprintPath);
		}

		UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);
		if (!Blueprint && UEditorAssetLibrary::DoesAssetExist(BlueprintPath))
		{
			Blueprint = Cast<UBlueprint>(UEditorAssetLibrary::LoadAsset(BlueprintPath));
		}
		return Blueprint;
	}

	void CollectAllGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs)
	{
		OutGraphs.Append(Blueprint->UbergraphPages);
		OutGraphs.Append(Blueprint->FunctionGraphs);
		OutGraphs.Append(Blueprint->MacroGraphs);
	}

	bool EventNameMatches(const UK2Node_Event* EventNode, const FString& WantedName)
	{
		if (EventNode->EventReference.GetMemberName().ToString().Equals(WantedName, ESearchCase::IgnoreCase))
		{
			return true;
		}
		if (const UK2Node_CustomEvent* Custom = Cast<UK2Node_CustomEvent>(EventNode))
		{
			if (Custom->CustomFunctionName.ToString().Equals(WantedName, ESearchCase::IgnoreCase))
			{
				return true;
			}
		}
		// Friendly alias: "BeginPlay" should find "ReceiveBeginPlay"
		const FString Prefixed = TEXT("Receive") + WantedName;
		return EventNode->EventReference.GetMemberName().ToString().Equals(Prefixed, ESearchCase::IgnoreCase);
	}
}

UEdGraphNode* FGraphTracer::FindStartNode(UBlueprint* Blueprint, const FString& NodeId, const FString& EventName, const FString& FunctionName, FString& OutError)
{
	TArray<UEdGraph*> Graphs;
	if (!FunctionName.IsEmpty())
	{
		for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
		{
			if (FuncGraph && FuncGraph->GetFName().ToString().Equals(FunctionName, ESearchCase::IgnoreCase))
			{
				Graphs.Add(FuncGraph);
				break;
			}
		}
		if (Graphs.Num() == 0)
		{
			OutError = FString::Printf(TEXT("Function graph not found: %s"), *FunctionName);
			return nullptr;
		}
	}
	else
	{
		CollectAllGraphs(Blueprint, Graphs);
	}

	if (!NodeId.IsEmpty())
	{
		for (UEdGraph* Graph : Graphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (Node && (Node->NodeGuid.ToString().Equals(NodeId, ESearchCase::IgnoreCase) ||
					Node->GetName().Equals(NodeId, ESearchCase::IgnoreCase)))
				{
					return Node;
				}
			}
		}
		OutError = FString::Printf(TEXT("Node not found: %s"), *NodeId);
		return nullptr;
	}

	if (!EventName.IsEmpty())
	{
		for (UEdGraph* Graph : Graphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
				{
					if (EventNameMatches(EventNode, EventName))
					{
						return EventNode;
					}
				}
			}
		}
		OutError = FString::Printf(TEXT("Event node not found: %s"), *EventName);
		return nullptr;
	}

	OutError = TEXT("Provide either 'start_node_id' or 'event_name'");
	return nullptr;
}

TSharedPtr<FJsonObject> FGraphTracer::DescribeNodeBrief(UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
	Obj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
	return Obj;
}

TSharedPtr<FJsonObject> FGraphTracer::TraceNode(UEdGraphNode* Node, FTraceContext& Ctx, int32 Depth)
{
	TSharedPtr<FJsonObject> Obj = DescribeNodeBrief(Node);

	if (Ctx.Visited.Contains(Node))
	{
		// Stop here: the node was already expanded elsewhere in the trace
		Obj->SetBoolField(TEXT("already_visited"), true);
		return Obj;
	}
	Ctx.Visited.Add(Node);
	Ctx.NodesVisited++;

	if (Depth >= Ctx.MaxDepth)
	{
		Obj->SetBoolField(TEXT("max_depth_reached"), true);
		return Obj;
	}
	if (Ctx.NodesVisited > 500)
	{
		Obj->SetBoolField(TEXT("node_budget_exhausted"), true);
		return Obj;
	}

	// Data inputs: literals and one-level-back sources, so a trace shows what
	// each call consumes without a second query
	if (Ctx.bIncludeData)
	{
		TSharedPtr<FJsonObject> DataInputs = MakeShared<FJsonObject>();
		bool bAny = false;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Input || Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec || Pin->bHidden)
			{
				continue;
			}
			bAny = true;
			if (Pin->LinkedTo.Num() > 0 && Pin->LinkedTo[0] && Pin->LinkedTo[0]->GetOwningNodeUnchecked())
			{
				UEdGraphNode* Source = Pin->LinkedTo[0]->GetOwningNode();
				DataInputs->SetStringField(Pin->PinName.ToString(),
					FString::Printf(TEXT("<- %s [%s]"), *Source->GetNodeTitle(ENodeTitleType::ListView).ToString(), *Source->NodeGuid.ToString()));
			}
			else
			{
				const FString Literal = !Pin->DefaultValue.IsEmpty() ? Pin->DefaultValue
					: (Pin->DefaultObject ? Pin->DefaultObject->GetName() : Pin->DefaultTextValue.ToString());
				DataInputs->SetStringField(Pin->PinName.ToString(), Literal.IsEmpty() ? TEXT("(default)") : Literal);
			}
		}
		if (bAny)
		{
			Obj->SetObjectField(TEXT("data_inputs"), DataInputs);
		}
	}

	// Follow exec pins
	const EEdGraphPinDirection FollowDir = Ctx.bForward ? EGPD_Output : EGPD_Input;
	TSharedPtr<FJsonObject> ExecLinks = MakeShared<FJsonObject>();
	bool bAnyExec = false;

	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->Direction != FollowDir || Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
		{
			continue;
		}

		TArray<TSharedPtr<FJsonValue>> NextNodes;
		for (UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (Linked && Linked->GetOwningNodeUnchecked())
			{
				NextNodes.Add(MakeShared<FJsonValueObject>(TraceNode(Linked->GetOwningNode(), Ctx, Depth + 1)));
			}
		}

		if (NextNodes.Num() > 0)
		{
			bAnyExec = true;
			ExecLinks->SetArrayField(Pin->PinName.ToString(), NextNodes);
		}
	}

	if (bAnyExec)
	{
		Obj->SetObjectField(Ctx.bForward ? TEXT("next") : TEXT("previous"), ExecLinks);
	}
	else
	{
		Obj->SetBoolField(Ctx.bForward ? TEXT("end_of_path") : TEXT("start_of_path"), true);
	}

	return Obj;
}

TSharedPtr<FJsonObject> FGraphTracer::TraceGraph(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	FString BlueprintName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Missing 'blueprint_name' parameter"));
		return Result;
	}

	FString NodeId, EventName, FunctionName, Direction;
	Params->TryGetStringField(TEXT("start_node_id"), NodeId);
	Params->TryGetStringField(TEXT("event_name"), EventName);
	Params->TryGetStringField(TEXT("function_name"), FunctionName);
	Params->TryGetStringField(TEXT("direction"), Direction);

	FTraceContext Ctx;
	Ctx.bForward = !Direction.Equals(TEXT("backward"), ESearchCase::IgnoreCase);
	Ctx.bIncludeData = true;
	Params->TryGetBoolField(TEXT("include_data"), Ctx.bIncludeData);

	double MaxDepth = 50.0;
	Params->TryGetNumberField(TEXT("max_depth"), MaxDepth);
	Ctx.MaxDepth = FMath::Clamp(static_cast<int32>(MaxDepth), 1, 200);

	UBlueprint* Blueprint = LoadBlueprintShared(BlueprintName);
	if (!Blueprint)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
		return Result;
	}

	FString FindError;
	UEdGraphNode* StartNode = FindStartNode(Blueprint, NodeId, EventName, FunctionName, FindError);
	if (!StartNode)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), FindError);
		return Result;
	}

	TSharedPtr<FJsonObject> Trace = TraceNode(StartNode, Ctx, 0);

	// Flat title list for the unambiguous straight-line prefix — answers the
	// common "what's the sequence?" question without parsing the tree
	TArray<TSharedPtr<FJsonValue>> Linear;
	{
		const FString NextKey = Ctx.bForward ? TEXT("next") : TEXT("previous");
		TSharedPtr<FJsonObject> Cursor = Trace;
		int32 Guard = 0;
		while (Cursor.IsValid() && Guard++ < Ctx.MaxDepth)
		{
			Linear.Add(MakeShared<FJsonValueString>(
				Cursor->GetStringField(TEXT("title")) + TEXT(" [") + Cursor->GetStringField(TEXT("node_id")) + TEXT("]")));

			const TSharedPtr<FJsonObject>* Links;
			if (!Cursor->TryGetObjectField(NextKey, Links))
			{
				break;
			}
			// Straight-line means exactly one exec pin with exactly one target
			TArray<FString> Keys;
			(*Links)->Values.GetKeys(Keys);
			if (Keys.Num() != 1)
			{
				break;
			}
			const TArray<TSharedPtr<FJsonValue>>* Targets;
			if (!(*Links)->TryGetArrayField(Keys[0], Targets) || Targets->Num() != 1)
			{
				break;
			}
			const TSharedPtr<FJsonObject>* NextObj;
			if (!(*Targets)[0]->TryGetObject(NextObj))
			{
				break;
			}
			Cursor = *NextObj;
		}
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("direction"), Ctx.bForward ? TEXT("forward") : TEXT("backward"));
	Result->SetNumberField(TEXT("nodes_visited"), Ctx.NodesVisited);
	Result->SetArrayField(TEXT("linear_sequence"), Linear);
	Result->SetObjectField(TEXT("trace"), Trace);
	return Result;
}
