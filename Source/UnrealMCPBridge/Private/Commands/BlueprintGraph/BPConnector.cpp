#include "Commands/BlueprintGraph/BPConnector.h"
#include "Commands/EpicUnrealMCPCommonUtils.h"
#include "Engine/Blueprint.h"
#include "K2Node.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"

namespace
{
	// Snapshot of a pin's links as "NodeGuid:PinName" strings, used to report
	// which connections the schema broke while making a new one.
	void SnapshotLinks(const UEdGraphPin* Pin, TMap<FString, FString>& Out)
	{
		for (const UEdGraphPin* Linked : Pin->LinkedTo)
		{
			if (Linked && Linked->GetOwningNodeUnchecked())
			{
				const FString Key = Linked->GetOwningNode()->NodeGuid.ToString() + TEXT(":") + Linked->PinName.ToString();
				Out.Add(Key, Linked->GetOwningNode()->GetClass()->GetName());
			}
		}
	}

	TSharedPtr<FJsonValueObject> MakeBrokenLinkEntry(const FString& FromNodeId, const FString& FromPin, const FString& Key)
	{
		FString NodeId, PinName;
		Key.Split(TEXT(":"), &NodeId, &PinName);
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("from_node"), FromNodeId);
		Obj->SetStringField(TEXT("from_pin"), FromPin);
		Obj->SetStringField(TEXT("to_node"), NodeId);
		Obj->SetStringField(TEXT("to_pin"), PinName);
		return MakeShared<FJsonValueObject>(Obj);
	}
}

TSharedPtr<FJsonObject> FBPConnector::ConnectNodes(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
    FString SourceNodeId = Params->GetStringField(TEXT("source_node_id"));
    FString SourcePinName = Params->GetStringField(TEXT("source_pin_name"));
    FString TargetNodeId = Params->GetStringField(TEXT("target_node_id"));
    FString TargetPinName = Params->GetStringField(TEXT("target_pin_name"));

    FString FunctionName;
    Params->TryGetStringField(TEXT("function_name"), FunctionName);

    UBlueprint* Blueprint = LoadBlueprintByName(BlueprintName);
    if (!Blueprint)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Blueprint not found");
        return Result;
    }

    UEdGraph* Graph = ResolveGraph(Blueprint, FunctionName, Result);
    if (!Graph)
    {
        return Result; // ResolveGraph filled in the error
    }

    // Find nodes
    UK2Node* SourceNode = FindNodeById(Graph, SourceNodeId);
    UK2Node* TargetNode = FindNodeById(Graph, TargetNodeId);

    if (!SourceNode || !TargetNode)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Node not found: %s"),
            !SourceNode ? *SourceNodeId : *TargetNodeId));
        return Result;
    }

    UEdGraphPin* SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_Output);
    if (!SourcePin)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Output pin '%s' not found on source node"), *SourcePinName));
        Result->SetArrayField("available_pins", DescribePins(SourceNode));
        return Result;
    }

    UEdGraphPin* TargetPin = FindPinByName(TargetNode, TargetPinName, EGPD_Input);
    if (!TargetPin)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Input pin '%s' not found on target node"), *TargetPinName));
        Result->SetArrayField("available_pins", DescribePins(TargetNode));
        return Result;
    }

    // Route through the schema so connection rules apply: type checks give a
    // real reason on failure, and single-link pins (exec outputs, data inputs)
    // get their existing link broken instead of silently double-linking into a
    // graph that no longer compiles.
    const UEdGraphSchema* Schema = Graph->GetSchema();
    const FPinConnectionResponse Response = Schema->CanCreateConnection(SourcePin, TargetPin);

    if (Response.Response == CONNECT_RESPONSE_DISALLOW)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Connection not allowed: %s"), *Response.Message.ToString()));
        return Result;
    }

    TMap<FString, FString> SourceLinksBefore, TargetLinksBefore;
    SnapshotLinks(SourcePin, SourceLinksBefore);
    SnapshotLinks(TargetPin, TargetLinksBefore);

    if (!Schema->TryCreateConnection(SourcePin, TargetPin))
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Schema rejected the connection: %s"), *Response.Message.ToString()));
        return Result;
    }

    // Report any links the schema broke to make room for the new one
    TArray<TSharedPtr<FJsonValue>> BrokenLinks;
    {
        TMap<FString, FString> SourceLinksAfter, TargetLinksAfter;
        SnapshotLinks(SourcePin, SourceLinksAfter);
        SnapshotLinks(TargetPin, TargetLinksAfter);

        for (const auto& Entry : SourceLinksBefore)
        {
            if (!SourceLinksAfter.Contains(Entry.Key))
            {
                BrokenLinks.Add(MakeBrokenLinkEntry(SourceNode->NodeGuid.ToString(), SourcePin->PinName.ToString(), Entry.Key));
            }
        }
        for (const auto& Entry : TargetLinksBefore)
        {
            if (!TargetLinksAfter.Contains(Entry.Key))
            {
                BrokenLinks.Add(MakeBrokenLinkEntry(TargetNode->NodeGuid.ToString(), TargetPin->PinName.ToString(), Entry.Key));
            }
        }
    }

    // Recompile
    Blueprint->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    Result->SetBoolField("success", true);

    TSharedPtr<FJsonObject> ConnectionInfo = MakeShared<FJsonObject>();
    ConnectionInfo->SetStringField("source_node", SourceNodeId);
    ConnectionInfo->SetStringField("source_pin", SourcePinName);
    ConnectionInfo->SetStringField("target_node", TargetNodeId);
    ConnectionInfo->SetStringField("target_pin", TargetPinName);
    ConnectionInfo->SetStringField("connection_type", SourcePin->PinType.PinCategory.ToString());

    Result->SetObjectField("connection", ConnectionInfo);
    if (BrokenLinks.Num() > 0)
    {
        Result->SetArrayField("broken_links", BrokenLinks);
    }

    return Result;
}

TSharedPtr<FJsonObject> FBPConnector::DisconnectNodes(const TSharedPtr<FJsonObject>& Params)
{
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

    FString BlueprintName = Params->GetStringField(TEXT("blueprint_name"));
    FString SourceNodeId = Params->GetStringField(TEXT("source_node_id"));
    FString SourcePinName = Params->GetStringField(TEXT("source_pin_name"));

    FString TargetNodeId, TargetPinName, FunctionName;
    Params->TryGetStringField(TEXT("target_node_id"), TargetNodeId);
    Params->TryGetStringField(TEXT("target_pin_name"), TargetPinName);
    Params->TryGetStringField(TEXT("function_name"), FunctionName);

    UBlueprint* Blueprint = LoadBlueprintByName(BlueprintName);
    if (!Blueprint)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", "Blueprint not found");
        return Result;
    }

    UEdGraph* Graph = ResolveGraph(Blueprint, FunctionName, Result);
    if (!Graph)
    {
        return Result;
    }

    UK2Node* SourceNode = FindNodeById(Graph, SourceNodeId);
    if (!SourceNode)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Node not found: %s"), *SourceNodeId));
        return Result;
    }

    // Accept the pin in either direction so callers can name whichever end they know
    UEdGraphPin* SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_Output);
    if (!SourcePin)
    {
        SourcePin = FindPinByName(SourceNode, SourcePinName, EGPD_Input);
    }
    if (!SourcePin)
    {
        Result->SetBoolField("success", false);
        Result->SetStringField("error", FString::Printf(TEXT("Pin '%s' not found on node"), *SourcePinName));
        Result->SetArrayField("available_pins", DescribePins(SourceNode));
        return Result;
    }

    const UEdGraphSchema* Schema = Graph->GetSchema();
    int32 NumBroken = 0;

    if (!TargetNodeId.IsEmpty())
    {
        // Break only the specific link to the named target pin
        UK2Node* TargetNode = FindNodeById(Graph, TargetNodeId);
        if (!TargetNode)
        {
            Result->SetBoolField("success", false);
            Result->SetStringField("error", FString::Printf(TEXT("Target node not found: %s"), *TargetNodeId));
            return Result;
        }

        const EEdGraphPinDirection WantedDir = (SourcePin->Direction == EGPD_Output) ? EGPD_Input : EGPD_Output;
        UEdGraphPin* TargetPin = FindPinByName(TargetNode, TargetPinName, WantedDir);
        if (!TargetPin)
        {
            Result->SetBoolField("success", false);
            Result->SetStringField("error", FString::Printf(TEXT("Pin '%s' not found on target node"), *TargetPinName));
            Result->SetArrayField("available_pins", DescribePins(TargetNode));
            return Result;
        }

        if (!SourcePin->LinkedTo.Contains(TargetPin))
        {
            Result->SetBoolField("success", false);
            Result->SetStringField("error", TEXT("Those pins are not connected"));
            return Result;
        }

        Schema->BreakSinglePinLink(SourcePin, TargetPin);
        NumBroken = 1;
    }
    else
    {
        // Break everything on the pin
        NumBroken = SourcePin->LinkedTo.Num();
        if (NumBroken == 0)
        {
            Result->SetBoolField("success", true);
            Result->SetNumberField("links_broken", 0);
            Result->SetStringField("note", TEXT("Pin had no connections"));
            return Result;
        }
        Schema->BreakPinLinks(*SourcePin, /*bSendsNodeNotification*/ true);
    }

    Blueprint->MarkPackageDirty();
    FKismetEditorUtilities::CompileBlueprint(Blueprint);

    Result->SetBoolField("success", true);
    Result->SetNumberField("links_broken", NumBroken);
    return Result;
}

UBlueprint* FBPConnector::LoadBlueprintByName(const FString& BlueprintName)
{
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

    UBlueprint* Blueprint = LoadObject<UBlueprint>(nullptr, *BlueprintPath);

    if (!Blueprint)
    {
        FString AssetPath = BlueprintPath;
        if (UEditorAssetLibrary::DoesAssetExist(AssetPath))
        {
            UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
            Blueprint = Cast<UBlueprint>(Asset);
        }
    }

    return Blueprint;
}

UEdGraph* FBPConnector::ResolveGraph(UBlueprint* Blueprint, const FString& FunctionName, TSharedPtr<FJsonObject>& ErrorResult)
{
    UEdGraph* Graph = nullptr;

    if (!FunctionName.IsEmpty())
    {
        for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
        {
            if (FuncGraph && (FuncGraph->GetFName().ToString() == FunctionName ||
                              (FuncGraph->GetOuter() && FuncGraph->GetOuter()->GetFName().ToString() == FunctionName)))
            {
                Graph = FuncGraph;
                break;
            }
        }

        if (!Graph)
        {
            for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
            {
                if (FuncGraph && FuncGraph->GetFName().ToString().Contains(FunctionName))
                {
                    Graph = FuncGraph;
                    break;
                }
            }
        }

        if (!Graph)
        {
            ErrorResult->SetBoolField("success", false);
            ErrorResult->SetStringField("error", FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
            return nullptr;
        }
    }
    else
    {
        if (Blueprint->UbergraphPages.Num() == 0)
        {
            ErrorResult->SetBoolField("success", false);
            ErrorResult->SetStringField("error", "Blueprint has no event graph");
            return nullptr;
        }
        Graph = Blueprint->UbergraphPages[0];
    }

    if (!Graph)
    {
        ErrorResult->SetBoolField("success", false);
        ErrorResult->SetStringField("error", "Graph not found");
    }
    return Graph;
}

TArray<TSharedPtr<FJsonValue>> FBPConnector::DescribePins(UK2Node* Node)
{
    TArray<TSharedPtr<FJsonValue>> Pins;
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (!Pin || Pin->bHidden)
        {
            continue;
        }
        TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
        PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
        PinObj->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Output ? TEXT("output") : TEXT("input"));
        PinObj->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
        PinObj->SetNumberField(TEXT("num_links"), Pin->LinkedTo.Num());
        Pins.Add(MakeShared<FJsonValueObject>(PinObj));
    }
    return Pins;
}

UK2Node* FBPConnector::FindNodeById(UEdGraph* Graph, const FString& NodeId)
{
    if (!Graph)
    {
        return nullptr;
    }

    for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (!Node)
        {
            continue;
        }

        // Try matching by NodeGuid first
        if (Node->NodeGuid.ToString().Equals(NodeId, ESearchCase::IgnoreCase))
        {
            UK2Node* K2Node = Cast<UK2Node>(Node);
            return K2Node;  // Return even if nullptr (caller will handle)
        }

        // Try matching by GetName()
        if (Node->GetName().Equals(NodeId, ESearchCase::IgnoreCase))
        {
            UK2Node* K2Node = Cast<UK2Node>(Node);
            return K2Node;  // Return even if nullptr (caller will handle)
        }
    }

    return nullptr;
}

UEdGraphPin* FBPConnector::FindPinByName(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction)
{
    // Exact match first
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->PinName.ToString() == PinName && Pin->Direction == Direction)
        {
            return Pin;
        }
    }
    // Case-insensitive fallback ("Exec"/"Then" vs "execute"/"then"), plus the
    // friendly display name agents see in read_blueprint_content output
    for (UEdGraphPin* Pin : Node->Pins)
    {
        if (Pin->Direction != Direction)
        {
            continue;
        }
        if (Pin->PinName.ToString().Equals(PinName, ESearchCase::IgnoreCase) ||
            Pin->PinFriendlyName.ToString().Equals(PinName, ESearchCase::IgnoreCase))
        {
            return Pin;
        }
    }
    // Common alias: callers say "Exec" for the unnamed exec input ("execute")
    if (Direction == EGPD_Input && (PinName.Equals(TEXT("Exec"), ESearchCase::IgnoreCase) || PinName.Equals(TEXT("In"), ESearchCase::IgnoreCase)))
    {
        for (UEdGraphPin* Pin : Node->Pins)
        {
            if (Pin->Direction == EGPD_Input && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
            {
                return Pin;
            }
        }
    }
    return nullptr;
}

bool FBPConnector::ArePinsCompatible(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin)
{
    if (SourcePin->Direction != EGPD_Output || TargetPin->Direction != EGPD_Input)
    {
        return false;
    }

    return SourcePin->PinType.PinCategory == TargetPin->PinType.PinCategory;
}
