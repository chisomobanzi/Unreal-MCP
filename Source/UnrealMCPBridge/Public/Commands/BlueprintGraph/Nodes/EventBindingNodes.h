// Delegate binding, parent function call, and parameterized custom event nodes
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
class UEdGraph;
class UClass;
class FMulticastDelegateProperty;
struct FEdGraphPinType;

/**
 * Creates the Blueprint node families the base NodeManager can't:
 *   - Multicast delegate nodes: Bind (Add), Unbind (Remove), Call, and
 *     Assign (Add + matching custom event, pre-connected)
 *   - Parent function calls (UK2Node_CallParentFunction)
 *   - Custom events with user-defined parameters
 *
 * Each entry point is a self-contained command handler returning the standard
 * {success, ...} JSON envelope.
 */
class UNREALMCPBRIDGE_API FEventBindingNodes
{
public:
	/**
	 * add_delegate_node — place a delegate node in the event graph.
	 * Params:
	 *   - blueprint_name (string)
	 *   - delegate_name (string): multicast delegate property name (e.g. "OnTextMessage")
	 *   - owner_class (string, optional): class that owns the delegate property.
	 *     "self" (default) = this Blueprint / its parents; otherwise a C++ class
	 *     name ("TutorAudioStreamComponent") or a /Game/... Blueprint path.
	 *   - mode (string): "Assign" (default) | "Add" | "Remove" | "Call"
	 *   - event_name (string, optional): name for the bound custom event in
	 *     Assign mode (default: "<DelegateName>_Event")
	 *   - function_name (string, optional): place in this function graph instead
	 *     of the event graph (Assign mode always uses the event graph)
	 *   - pos_x / pos_y (number, optional)
	 */
	static TSharedPtr<FJsonObject> AddDelegateNode(const TSharedPtr<FJsonObject>& Params);

	/**
	 * add_parent_call_node — place a "Call to Parent Function" node.
	 * Params: blueprint_name, function_name (parent function), graph_function_name
	 * (optional: which graph to place in; defaults to the event graph), pos_x/pos_y.
	 */
	static TSharedPtr<FJsonObject> AddParentCallNode(const TSharedPtr<FJsonObject>& Params);

	/**
	 * add_custom_event_node — place a custom event, optionally with parameters.
	 * Params:
	 *   - blueprint_name, event_name, pos_x/pos_y
	 *   - parameters (array, optional): [{"name": "...", "type": "...", "is_array": bool}]
	 *     Supported types: bool, byte, int, int64, float, string, name, text,
	 *     vector, rotator, transform, linearcolor, object:<ClassNameOrPath>,
	 *     class:<ClassNameOrPath>, struct:<StructPath>, enum:<EnumPath>
	 */
	static TSharedPtr<FJsonObject> AddCustomEventNode(const TSharedPtr<FJsonObject>& Params);

	/** Resolves a class from "self", a C++ name, or a /Game Blueprint path. */
	static UClass* ResolveClass(const FString& ClassName, UBlueprint* ContextBlueprint);

	/** Builds a pin type from a type string; returns false with OutError set on failure. */
	static bool ParsePinType(const FString& TypeString, bool bIsArray, FEdGraphPinType& OutType, FString& OutError);

private:
	static TSharedPtr<FJsonObject> MakeError(const FString& Message);
	static FMulticastDelegateProperty* FindDelegateProperty(UClass* OwnerClass, const FString& DelegateName, FString& OutAvailable);
};
