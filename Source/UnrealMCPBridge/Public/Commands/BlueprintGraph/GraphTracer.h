// Traces execution paths through Blueprint graphs
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UK2Node;

/**
 * Answers "what runs after X?" / "what leads to X?" without dumping the whole
 * graph: follows exec pins from a start node (or event) and returns the
 * ordered chain, branching where the graph branches.
 */
class UNREALMCPBRIDGE_API FGraphTracer
{
public:
	/**
	 * trace_graph command.
	 * Params:
	 *   - blueprint_name (string)
	 *   - start_node_id (string) OR event_name (string): where to start.
	 *     event_name matches both overridden events (ReceiveBeginPlay) and
	 *     custom events. Friendly aliases like "BeginPlay" work too.
	 *   - function_name (string, optional): search this function graph instead
	 *     of the event graph (start_node_id is searched across all graphs)
	 *   - direction (string, optional): "forward" (default) follows exec
	 *     outputs; "backward" follows exec inputs to find callers
	 *   - max_depth (number, optional, default 50)
	 *   - include_data (bool, optional, default true): list each node's data
	 *     inputs (literal values or the node feeding them, one level deep)
	 * Returns: { trace: <nested node tree>, linear_sequence: [titles...] }
	 * linear_sequence covers the unambiguous straight-line prefix of the path.
	 */
	static TSharedPtr<FJsonObject> TraceGraph(const TSharedPtr<FJsonObject>& Params);

private:
	struct FTraceContext
	{
		bool bForward = true;
		bool bIncludeData = true;
		int32 MaxDepth = 50;
		int32 NodesVisited = 0;
		TSet<UEdGraphNode*> Visited;
	};

	static TSharedPtr<FJsonObject> TraceNode(UEdGraphNode* Node, FTraceContext& Ctx, int32 Depth);
	static TSharedPtr<FJsonObject> DescribeNodeBrief(UEdGraphNode* Node);
	static UEdGraphNode* FindStartNode(UBlueprint* Blueprint, const FString& NodeId, const FString& EventName, const FString& FunctionName, FString& OutError);
};
