#include "Commands/BlueprintGraph/Nodes/UtilityNodes.h"
#include "Commands/BlueprintGraph/Nodes/NodeCreatorUtils.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Select.h"
#include "K2Node_SpawnActorFromClass.h"
#include "K2Node_ComponentBoundEvent.h"
#include "K2Node_GetSubsystem.h"
#include "EdGraphSchema_K2.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "EditorAssetLibrary.h"
#include "Json.h"

UK2Node* FUtilityNodeCreator::CreatePrintNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_CallFunction* PrintNode = NewObject<UK2Node_CallFunction>(Graph);
	if (!PrintNode)
	{
		return nullptr;
	}

	UFunction* PrintFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(
		GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, PrintString)
	);

	if (!PrintFunc)
	{
		return nullptr;
	}

	// Set function reference BEFORE initialization
	PrintNode->SetFromFunction(PrintFunc);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	PrintNode->NodePosX = static_cast<int32>(PosX);
	PrintNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(PrintNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(PrintNode, Graph);

	// Set message if provided AFTER initialization
	FString Message;
	if (Params->TryGetStringField(TEXT("message"), Message))
	{
		UEdGraphPin* InStringPin = PrintNode->FindPin(TEXT("InString"));
		if (InStringPin)
		{
			InStringPin->DefaultValue = Message;
		}
	}

	return PrintNode;
}

UK2Node* FUtilityNodeCreator::CreateCallFunctionNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	// Get target function name
	FString TargetFunction;
	if (!Params->TryGetStringField(TEXT("target_function"), TargetFunction))
	{
		return nullptr;
	}

	UK2Node_CallFunction* CallNode = NewObject<UK2Node_CallFunction>(Graph);
	if (!CallNode)
	{
		return nullptr;
	}

	// Find the function to call
	UFunction* TargetFunc = nullptr;
	FString ClassName;
	// Accept both "target_class" and "target_blueprint" parameter names
	if (!Params->TryGetStringField(TEXT("target_class"), ClassName))
	{
		Params->TryGetStringField(TEXT("target_blueprint"), ClassName);
	}

	if (!ClassName.IsEmpty())
	{
		UClass* TargetClass = nullptr;

		// Try StaticFindObject with full path first (e.g. "/Script/UMG.TextBlock")
		TargetClass = Cast<UClass>(StaticFindObject(UClass::StaticClass(), nullptr, *ClassName));

		// Fallback: try FindFirstObject for short names (e.g. "TextBlock", "UTextBlock")
		if (!TargetClass)
		{
			FString CleanName = ClassName;
			if (CleanName.StartsWith(TEXT("U")) || CleanName.StartsWith(TEXT("A")))
			{
				CleanName = CleanName.RightChop(1);
			}
			TargetClass = FindFirstObject<UClass>(*CleanName, EFindFirstObjectOptions::NativeFirst);
		}

		// Fallback: try with U prefix
		if (!TargetClass)
		{
			TargetClass = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
		}

		if (TargetClass)
		{
			TargetFunc = TargetClass->FindFunctionByName(FName(*TargetFunction));
		}
	}
	else
	{
		// Try common Unreal classes
		TargetFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(FName(*TargetFunction));
	}

	// Fallback: try loading ClassName as a Blueprint and using its GeneratedClass
	if (!TargetFunc && !ClassName.IsEmpty())
	{
		FString BPPath = ClassName;
		if (!BPPath.Contains(TEXT(".")))
		{
			BPPath += TEXT(".") + FPaths::GetBaseFilename(BPPath);
		}
		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *BPPath))
		{
			if (BP->GeneratedClass)
			{
				TargetFunc = BP->GeneratedClass->FindFunctionByName(FName(*TargetFunction));
			}
		}
	}

	if (!TargetFunc)
	{
		return nullptr;
	}

	// Set function reference BEFORE initialization
	CallNode->SetFromFunction(TargetFunc);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	CallNode->NodePosX = static_cast<int32>(PosX);
	CallNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(CallNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(CallNode, Graph);

	return CallNode;
}

UK2Node* FUtilityNodeCreator::CreateSelectNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_Select* SelectNode = NewObject<UK2Node_Select>(Graph);
	if (!SelectNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SelectNode->NodePosX = static_cast<int32>(PosX);
	SelectNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SelectNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SelectNode, Graph);

	return SelectNode;
}

UK2Node* FUtilityNodeCreator::CreateSpawnActorNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	UK2Node_SpawnActorFromClass* SpawnActorNode = NewObject<UK2Node_SpawnActorFromClass>(Graph);
	if (!SpawnActorNode)
	{
		return nullptr;
	}

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SpawnActorNode->NodePosX = static_cast<int32>(PosX);
	SpawnActorNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SpawnActorNode, true, false);
	FNodeCreatorUtils::InitializeK2Node(SpawnActorNode, Graph);

	return SpawnActorNode;
}

UK2Node* FUtilityNodeCreator::CreateComponentBoundEventNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	// Accept component_name or variable_name (fallback for older tool schemas)
	FString ComponentName;
	if (!Params->TryGetStringField(TEXT("component_name"), ComponentName))
	{
		if (!Params->TryGetStringField(TEXT("variable_name"), ComponentName))
		{
			return nullptr;
		}
	}

	// Accept delegate_name or event_type (fallback for older tool schemas)
	FString DelegateName;
	if (!Params->TryGetStringField(TEXT("delegate_name"), DelegateName))
	{
		if (!Params->TryGetStringField(TEXT("event_type"), DelegateName))
		{
			return nullptr;
		}
	}

	UBlueprint* Blueprint = FBlueprintEditorUtils::FindBlueprintForGraph(Graph);
	if (!Blueprint || !Blueprint->GeneratedClass)
	{
		return nullptr;
	}

	// Find the component property on the BP's generated class
	FObjectProperty* ComponentProp = FindFProperty<FObjectProperty>(
		Blueprint->GeneratedClass, FName(*ComponentName));
	if (!ComponentProp)
	{
		return nullptr;
	}

	// Find the multicast delegate property on the component's class
	UClass* ComponentClass = ComponentProp->PropertyClass;
	if (!ComponentClass)
	{
		return nullptr;
	}

	FMulticastDelegateProperty* DelegateProp = FindFProperty<FMulticastDelegateProperty>(
		ComponentClass, FName(*DelegateName));
	if (!DelegateProp)
	{
		return nullptr;
	}

	UK2Node_ComponentBoundEvent* EventNode = NewObject<UK2Node_ComponentBoundEvent>(Graph);
	if (!EventNode)
	{
		return nullptr;
	}

	EventNode->InitializeComponentBoundEventParams(ComponentProp, DelegateProp);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	EventNode->NodePosX = static_cast<int32>(PosX);
	EventNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(EventNode, true, false);
	EventNode->AllocateDefaultPins();

	return EventNode;
}

UK2Node* FUtilityNodeCreator::CreateGetSubsystemNode(UEdGraph* Graph, const TSharedPtr<FJsonObject>& Params)
{
	if (!Graph || !Params.IsValid())
	{
		return nullptr;
	}

	// Accept subsystem_class or target_blueprint (fallback for older tool schemas)
	FString SubsystemClassName;
	if (!Params->TryGetStringField(TEXT("subsystem_class"), SubsystemClassName))
	{
		if (!Params->TryGetStringField(TEXT("target_blueprint"), SubsystemClassName))
		{
			return nullptr;
		}
	}

	// Resolve the subsystem class
	UClass* SubsystemClass = Cast<UClass>(
		StaticFindObject(UClass::StaticClass(), nullptr, *SubsystemClassName));

	if (!SubsystemClass)
	{
		FString CleanName = SubsystemClassName;
		if (CleanName.StartsWith(TEXT("U")) || CleanName.StartsWith(TEXT("A")))
		{
			CleanName = CleanName.RightChop(1);
		}
		SubsystemClass = FindFirstObject<UClass>(*CleanName, EFindFirstObjectOptions::NativeFirst);
	}

	if (!SubsystemClass)
	{
		SubsystemClass = FindFirstObject<UClass>(*SubsystemClassName, EFindFirstObjectOptions::NativeFirst);
	}

	if (!SubsystemClass)
	{
		return nullptr;
	}

	UK2Node_GetSubsystem* SubsystemNode = NewObject<UK2Node_GetSubsystem>(Graph);
	if (!SubsystemNode)
	{
		return nullptr;
	}

	SubsystemNode->Initialize(SubsystemClass);

	double PosX, PosY;
	FNodeCreatorUtils::ExtractNodePosition(Params, PosX, PosY);
	SubsystemNode->NodePosX = static_cast<int32>(PosX);
	SubsystemNode->NodePosY = static_cast<int32>(PosY);

	Graph->AddNode(SubsystemNode, true, false);
	SubsystemNode->AllocateDefaultPins();

	return SubsystemNode;
}

