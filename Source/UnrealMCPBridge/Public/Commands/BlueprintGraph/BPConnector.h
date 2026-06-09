// Connects two Blueprint nodes via their pins
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

// Forward declarations
class UBlueprint;
class UK2Node;
class UEdGraph;
class UEdGraphPin;
enum EEdGraphPinDirection : int;

/**
 * Utility class for connecting Blueprint nodes
 */
class UNREALMCPBRIDGE_API FBPConnector
{
public:
    /**
     * Connects two Blueprint nodes via their pins.
     * Goes through the graph schema, so single-link pins (exec outputs, data
     * inputs) get their existing link auto-broken instead of double-linking;
     * any broken links are reported in the response under "broken_links".
     * @param Params JSON containing blueprint_name, source_node_id, source_pin_name, target_node_id, target_pin_name
     * @return JSON with success and connection details
     */
    static TSharedPtr<FJsonObject> ConnectNodes(const TSharedPtr<FJsonObject>& Params);

    /**
     * Breaks links on a pin. With target_node_id/target_pin_name, breaks just
     * that link; otherwise breaks every link on the named pin.
     * @param Params JSON containing blueprint_name, source_node_id, source_pin_name,
     *               optional target_node_id, target_pin_name, function_name
     * @return JSON with success and links_broken count
     */
    static TSharedPtr<FJsonObject> DisconnectNodes(const TSharedPtr<FJsonObject>& Params);

private:
    /**
     * Finds a node by its ID in the graph
     */
    static UK2Node* FindNodeById(UEdGraph* Graph, const FString& NodeId);

    /**
     * Finds a pin by its name in a node (exact, then case-insensitive /
     * friendly-name, then exec aliases)
     */
    static UEdGraphPin* FindPinByName(UK2Node* Node, const FString& PinName, EEdGraphPinDirection Direction);

    /**
     * Checks compatibility between two pins
     */
    static bool ArePinsCompatible(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin);

    /** Loads a Blueprint by name or path (shared lookup rules) */
    static UBlueprint* LoadBlueprintByName(const FString& BlueprintName);

    /** Resolves the function graph by name, or the event graph when empty; fills ErrorResult on failure */
    static UEdGraph* ResolveGraph(UBlueprint* Blueprint, const FString& FunctionName, TSharedPtr<FJsonObject>& ErrorResult);

    /** JSON description of a node's visible pins, for pin-not-found errors */
    static TArray<TSharedPtr<FJsonValue>> DescribePins(UK2Node* Node);
};
