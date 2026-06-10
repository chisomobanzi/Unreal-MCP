#include "Commands/BlueprintGraph/Nodes/EventBindingNodes.h"
#include "Commands/BlueprintGraph/BPConnector.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_AddDelegate.h"
#include "K2Node_RemoveDelegate.h"
#include "K2Node_CallDelegate.h"
#include "K2Node_CustomEvent.h"
#include "K2Node_CallParentFunction.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EditorAssetLibrary.h"
#include "UObject/UObjectIterator.h"

namespace
{
	UBlueprint* LoadBlueprintForBinding(const FString& BlueprintName)
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

	UEdGraph* ResolveTargetGraph(UBlueprint* Blueprint, const FString& FunctionName)
	{
		if (!FunctionName.IsEmpty())
		{
			for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
			{
				if (FuncGraph && FuncGraph->GetFName().ToString() == FunctionName)
				{
					return FuncGraph;
				}
			}
			for (UEdGraph* FuncGraph : Blueprint->FunctionGraphs)
			{
				if (FuncGraph && FuncGraph->GetFName().ToString().Contains(FunctionName))
				{
					return FuncGraph;
				}
			}
			return nullptr;
		}
		return Blueprint->UbergraphPages.Num() > 0 ? Blueprint->UbergraphPages[0] : nullptr;
	}

	void ReadPosition(const TSharedPtr<FJsonObject>& Params, double& OutX, double& OutY)
	{
		OutX = 0.0;
		OutY = 0.0;
		Params->TryGetNumberField(TEXT("pos_x"), OutX);
		Params->TryGetNumberField(TEXT("pos_y"), OutY);
	}

	void FinalizeNode(UEdGraph* Graph, UK2Node* Node, double PosX, double PosY)
	{
		Node->NodePosX = static_cast<int32>(PosX);
		Node->NodePosY = static_cast<int32>(PosY);
		Graph->AddNode(Node, true);
		Node->CreateNewGuid();
		Node->PostPlacedNewNode();
		Node->AllocateDefaultPins();
	}

	void NotifyAndCompile(UBlueprint* Blueprint, UEdGraph* Graph)
	{
		Graph->NotifyGraphChanged();
		FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
	}

	void DescribeNode(const TSharedPtr<FJsonObject>& Out, const FString& Key, UK2Node* Node)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("node_id"), Node->NodeGuid.ToString());
		Obj->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::ListView).ToString());
		Obj->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		Obj->SetNumberField(TEXT("pos_y"), Node->NodePosY);
		Out->SetObjectField(Key, Obj);
	}
}

TSharedPtr<FJsonObject> FEventBindingNodes::MakeError(const FString& Message)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetBoolField(TEXT("success"), false);
	Response->SetStringField(TEXT("error"), Message);
	return Response;
}

UClass* FEventBindingNodes::ResolveClass(const FString& ClassName, UBlueprint* ContextBlueprint)
{
	if (ClassName.IsEmpty() || ClassName.Equals(TEXT("self"), ESearchCase::IgnoreCase))
	{
		return ContextBlueprint ? ContextBlueprint->GeneratedClass : nullptr;
	}

	// Blueprint asset path
	if (ClassName.StartsWith(TEXT("/")))
	{
		FString Path = ClassName;
		if (!Path.Contains(TEXT(".")))
		{
			Path += TEXT(".") + FPaths::GetBaseFilename(Path);
		}
		if (UBlueprint* BP = LoadObject<UBlueprint>(nullptr, *Path))
		{
			return BP->GeneratedClass;
		}
		// Maybe a direct class path like /Script/Module.ClassName
		if (UClass* Cls = LoadObject<UClass>(nullptr, *ClassName))
		{
			return Cls;
		}
		return nullptr;
	}

	// C++ class name, with or without the U/A prefix
	if (UClass* Cls = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::None))
	{
		return Cls;
	}
	if (ClassName.Len() > 1 && (ClassName[0] == TEXT('U') || ClassName[0] == TEXT('A')))
	{
		if (UClass* Cls = FindFirstObject<UClass>(*ClassName.Mid(1), EFindFirstObjectOptions::None))
		{
			return Cls;
		}
	}
	// Blueprint generated-class name ("WBP_Foo_C")
	if (!ClassName.EndsWith(TEXT("_C")))
	{
		if (UClass* Cls = FindFirstObject<UClass>(*(ClassName + TEXT("_C")), EFindFirstObjectOptions::None))
		{
			return Cls;
		}
	}
	return nullptr;
}

FMulticastDelegateProperty* FEventBindingNodes::FindDelegateProperty(UClass* OwnerClass, const FString& DelegateName, FString& OutAvailable)
{
	FMulticastDelegateProperty* Found = nullptr;
	TArray<FString> Available;

	for (TFieldIterator<FMulticastDelegateProperty> It(OwnerClass); It; ++It)
	{
		FMulticastDelegateProperty* Prop = *It;
		Available.Add(Prop->GetName());
		if (!Found && Prop->GetName().Equals(DelegateName, ESearchCase::IgnoreCase))
		{
			Found = Prop;
		}
	}

	if (!Found)
	{
		OutAvailable = FString::Join(Available, TEXT(", "));
	}
	return Found;
}

bool FEventBindingNodes::ParsePinType(const FString& TypeString, bool bIsArray, FEdGraphPinType& OutType, FString& OutError)
{
	OutType = FEdGraphPinType();
	FString Type = TypeString.TrimStartAndEnd();
	FString SubPath;

	// "object:ClassName" / "struct:/Script/Module.StructName" style qualifiers
	FString Kind = Type;
	if (Type.Contains(TEXT(":")))
	{
		Type.Split(TEXT(":"), &Kind, &SubPath);
		Kind = Kind.TrimStartAndEnd();
		SubPath = SubPath.TrimStartAndEnd();
	}

	if (Kind.Equals(TEXT("bool"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Boolean;
	}
	else if (Kind.Equals(TEXT("byte"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;
	}
	else if (Kind.Equals(TEXT("int"), ESearchCase::IgnoreCase) || Kind.Equals(TEXT("int32"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Int;
	}
	else if (Kind.Equals(TEXT("int64"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Int64;
	}
	else if (Kind.Equals(TEXT("float"), ESearchCase::IgnoreCase) || Kind.Equals(TEXT("double"), ESearchCase::IgnoreCase) || Kind.Equals(TEXT("real"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Real;
		OutType.PinSubCategory = UEdGraphSchema_K2::PC_Double;
	}
	else if (Kind.Equals(TEXT("string"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_String;
	}
	else if (Kind.Equals(TEXT("name"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Name;
	}
	else if (Kind.Equals(TEXT("text"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Text;
	}
	else if (Kind.Equals(TEXT("vector"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutType.PinSubCategoryObject = TBaseStructure<FVector>::Get();
	}
	else if (Kind.Equals(TEXT("rotator"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutType.PinSubCategoryObject = TBaseStructure<FRotator>::Get();
	}
	else if (Kind.Equals(TEXT("transform"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutType.PinSubCategoryObject = TBaseStructure<FTransform>::Get();
	}
	else if (Kind.Equals(TEXT("linearcolor"), ESearchCase::IgnoreCase) || Kind.Equals(TEXT("color"), ESearchCase::IgnoreCase))
	{
		OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutType.PinSubCategoryObject = TBaseStructure<FLinearColor>::Get();
	}
	else if (Kind.Equals(TEXT("object"), ESearchCase::IgnoreCase) || Kind.Equals(TEXT("class"), ESearchCase::IgnoreCase))
	{
		UClass* Cls = UObject::StaticClass();
		if (!SubPath.IsEmpty())
		{
			Cls = ResolveClass(SubPath, nullptr);
			if (!Cls)
			{
				OutError = FString::Printf(TEXT("Could not resolve class '%s'"), *SubPath);
				return false;
			}
		}
		OutType.PinCategory = Kind.Equals(TEXT("class"), ESearchCase::IgnoreCase) ? UEdGraphSchema_K2::PC_Class : UEdGraphSchema_K2::PC_Object;
		OutType.PinSubCategoryObject = Cls;
	}
	else if (Kind.Equals(TEXT("struct"), ESearchCase::IgnoreCase))
	{
		UScriptStruct* Struct = SubPath.IsEmpty() ? nullptr
			: (LoadObject<UScriptStruct>(nullptr, *SubPath)
				? LoadObject<UScriptStruct>(nullptr, *SubPath)
				: FindFirstObject<UScriptStruct>(*SubPath, EFindFirstObjectOptions::None));
		if (!Struct)
		{
			OutError = FString::Printf(TEXT("Could not resolve struct '%s' (use a /Script/... path or exact struct name)"), *SubPath);
			return false;
		}
		OutType.PinCategory = UEdGraphSchema_K2::PC_Struct;
		OutType.PinSubCategoryObject = Struct;
	}
	else if (Kind.Equals(TEXT("enum"), ESearchCase::IgnoreCase))
	{
		UEnum* Enum = SubPath.IsEmpty() ? nullptr
			: (LoadObject<UEnum>(nullptr, *SubPath)
				? LoadObject<UEnum>(nullptr, *SubPath)
				: FindFirstObject<UEnum>(*SubPath, EFindFirstObjectOptions::None));
		if (!Enum)
		{
			OutError = FString::Printf(TEXT("Could not resolve enum '%s'"), *SubPath);
			return false;
		}
		OutType.PinCategory = UEdGraphSchema_K2::PC_Byte;
		OutType.PinSubCategoryObject = Enum;
	}
	else
	{
		OutError = FString::Printf(TEXT("Unknown type '%s'. Supported: bool, byte, int, int64, float, string, name, text, vector, rotator, transform, linearcolor, object:<Class>, class:<Class>, struct:<Path>, enum:<Path>"), *TypeString);
		return false;
	}

	if (bIsArray)
	{
		OutType.ContainerType = EPinContainerType::Array;
	}
	return true;
}

TSharedPtr<FJsonObject> FEventBindingNodes::AddDelegateNode(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, DelegateName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return MakeError(TEXT("Missing 'blueprint_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("delegate_name"), DelegateName))
	{
		return MakeError(TEXT("Missing 'delegate_name' parameter"));
	}

	FString Mode = TEXT("Assign");
	Params->TryGetStringField(TEXT("mode"), Mode);

	FString OwnerClassName = TEXT("self");
	Params->TryGetStringField(TEXT("owner_class"), OwnerClassName);

	FString FunctionName;
	Params->TryGetStringField(TEXT("function_name"), FunctionName);

	UBlueprint* Blueprint = LoadBlueprintForBinding(BlueprintName);
	if (!Blueprint)
	{
		return MakeError(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	UClass* OwnerClass = ResolveClass(OwnerClassName, Blueprint);
	if (!OwnerClass)
	{
		return MakeError(FString::Printf(TEXT("Could not resolve owner class: %s"), *OwnerClassName));
	}

	FString AvailableDelegates;
	FMulticastDelegateProperty* DelegateProp = FindDelegateProperty(OwnerClass, DelegateName, AvailableDelegates);
	if (!DelegateProp)
	{
		return MakeError(FString::Printf(TEXT("Delegate '%s' not found on %s. Multicast delegates on that class: [%s]"),
			*DelegateName, *OwnerClass->GetName(), *AvailableDelegates));
	}

	// Assign mode wires a custom event, which can only live in the event graph
	const bool bAssign = Mode.Equals(TEXT("Assign"), ESearchCase::IgnoreCase);
	UEdGraph* Graph = ResolveTargetGraph(Blueprint, bAssign ? FString() : FunctionName);
	if (!Graph)
	{
		return MakeError(FunctionName.IsEmpty() ? TEXT("Blueprint has no event graph") : FString::Printf(TEXT("Function graph not found: %s"), *FunctionName));
	}

	double PosX, PosY;
	ReadPosition(Params, PosX, PosY);

	const bool bSelfContext = Blueprint->GeneratedClass && Blueprint->GeneratedClass->IsChildOf(DelegateProp->GetOwnerClass());

	UK2Node_BaseMCDelegate* DelegateNode = nullptr;
	if (bAssign || Mode.Equals(TEXT("Add"), ESearchCase::IgnoreCase))
	{
		DelegateNode = NewObject<UK2Node_AddDelegate>(Graph);
	}
	else if (Mode.Equals(TEXT("Remove"), ESearchCase::IgnoreCase))
	{
		DelegateNode = NewObject<UK2Node_RemoveDelegate>(Graph);
	}
	else if (Mode.Equals(TEXT("Call"), ESearchCase::IgnoreCase))
	{
		DelegateNode = NewObject<UK2Node_CallDelegate>(Graph);
	}
	else
	{
		return MakeError(FString::Printf(TEXT("Unknown mode '%s' (use Assign, Add, Remove, or Call)"), *Mode));
	}

	DelegateNode->SetFromProperty(DelegateProp, bSelfContext, DelegateProp->GetOwnerClass());
	FinalizeNode(Graph, DelegateNode, PosX, PosY);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("mode"), Mode);
	Result->SetStringField(TEXT("delegate"), DelegateProp->GetName());
	Result->SetStringField(TEXT("owner_class"), DelegateProp->GetOwnerClass()->GetName());
	Result->SetBoolField(TEXT("self_context"), bSelfContext);
	DescribeNode(Result, TEXT("delegate_node"), DelegateNode);
	if (!bSelfContext)
	{
		Result->SetStringField(TEXT("note"), TEXT("Delegate is not on self: connect an instance to the node's target pin"));
	}

	if (bAssign)
	{
		// Build the matching custom event from the delegate signature and wire
		// it to the Bind node's delegate pin — the same thing the editor's
		// "Assign" action does.
		FString EventName = DelegateProp->GetName() + TEXT("_Event");
		Params->TryGetStringField(TEXT("event_name"), EventName);

		const UFunction* Signature = DelegateProp->SignatureFunction;
		UK2Node_CustomEvent* EventNode = UK2Node_CustomEvent::CreateFromFunction(
			FVector2D(PosX + 420.0, PosY + 60.0), Graph, EventName, Signature, /*bSelectNewNode*/ false);

		if (!EventNode)
		{
			NotifyAndCompile(Blueprint, Graph);
			Result->SetStringField(TEXT("warning"), TEXT("Bind node created, but the custom event could not be created"));
			return Result;
		}

		UEdGraphPin* DelegatePin = DelegateNode->GetDelegatePin();
		UEdGraphPin* EventDelegateOut = EventNode->FindPin(UK2Node_Event::DelegateOutputName);

		bool bConnected = false;
		if (DelegatePin && EventDelegateOut)
		{
			bConnected = Graph->GetSchema()->TryCreateConnection(EventDelegateOut, DelegatePin);
		}

		DescribeNode(Result, TEXT("event_node"), EventNode);
		Result->SetStringField(TEXT("event_name"), EventNode->CustomFunctionName.ToString());
		Result->SetBoolField(TEXT("event_connected"), bConnected);
	}

	NotifyAndCompile(Blueprint, Graph);
	return Result;
}

TSharedPtr<FJsonObject> FEventBindingNodes::AddParentCallNode(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, FunctionName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return MakeError(TEXT("Missing 'blueprint_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("function_name"), FunctionName))
	{
		return MakeError(TEXT("Missing 'function_name' parameter"));
	}

	FString GraphFunctionName;
	Params->TryGetStringField(TEXT("graph_function_name"), GraphFunctionName);

	UBlueprint* Blueprint = LoadBlueprintForBinding(BlueprintName);
	if (!Blueprint)
	{
		return MakeError(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	UClass* ParentClass = Blueprint->ParentClass;
	if (!ParentClass)
	{
		return MakeError(TEXT("Blueprint has no parent class"));
	}

	UFunction* ParentFunction = ParentClass->FindFunctionByName(FName(*FunctionName));
	if (!ParentFunction)
	{
		return MakeError(FString::Printf(TEXT("Function '%s' not found on parent class %s"), *FunctionName, *ParentClass->GetName()));
	}

	UEdGraph* Graph = ResolveTargetGraph(Blueprint, GraphFunctionName);
	if (!Graph)
	{
		return MakeError(GraphFunctionName.IsEmpty() ? TEXT("Blueprint has no event graph") : FString::Printf(TEXT("Function graph not found: %s"), *GraphFunctionName));
	}

	double PosX, PosY;
	ReadPosition(Params, PosX, PosY);

	UK2Node_CallParentFunction* CallNode = NewObject<UK2Node_CallParentFunction>(Graph);
	CallNode->SetFromFunction(ParentFunction);
	FinalizeNode(Graph, CallNode, PosX, PosY);

	NotifyAndCompile(Blueprint, Graph);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("function"), FunctionName);
	Result->SetStringField(TEXT("parent_class"), ParentClass->GetName());
	DescribeNode(Result, TEXT("node"), CallNode);
	return Result;
}

TSharedPtr<FJsonObject> FEventBindingNodes::AddCustomEventNode(const TSharedPtr<FJsonObject>& Params)
{
	FString BlueprintName, EventName;
	if (!Params->TryGetStringField(TEXT("blueprint_name"), BlueprintName))
	{
		return MakeError(TEXT("Missing 'blueprint_name' parameter"));
	}
	if (!Params->TryGetStringField(TEXT("event_name"), EventName))
	{
		return MakeError(TEXT("Missing 'event_name' parameter"));
	}

	UBlueprint* Blueprint = LoadBlueprintForBinding(BlueprintName);
	if (!Blueprint)
	{
		return MakeError(FString::Printf(TEXT("Blueprint not found: %s"), *BlueprintName));
	}

	UEdGraph* Graph = ResolveTargetGraph(Blueprint, FString());
	if (!Graph)
	{
		return MakeError(TEXT("Blueprint has no event graph"));
	}

	// Refuse a name that already exists as an event in this graph
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (UK2Node_CustomEvent* Existing = Cast<UK2Node_CustomEvent>(Node))
		{
			if (Existing->CustomFunctionName == FName(*EventName))
			{
				return MakeError(FString::Printf(TEXT("A custom event named '%s' already exists (node %s)"), *EventName, *Existing->NodeGuid.ToString()));
			}
		}
	}

	double PosX, PosY;
	ReadPosition(Params, PosX, PosY);

	UK2Node_CustomEvent* EventNode = NewObject<UK2Node_CustomEvent>(Graph);
	EventNode->CustomFunctionName = FName(*EventName);
	FinalizeNode(Graph, EventNode, PosX, PosY);

	// Add user-defined parameter pins
	TArray<TSharedPtr<FJsonValue>> AddedParams;
	const TArray<TSharedPtr<FJsonValue>>* ParametersArray = nullptr;
	if (Params->TryGetArrayField(TEXT("parameters"), ParametersArray))
	{
		for (const TSharedPtr<FJsonValue>& Entry : *ParametersArray)
		{
			const TSharedPtr<FJsonObject>* ParamObj;
			if (!Entry->TryGetObject(ParamObj))
			{
				continue;
			}

			FString ParamName, ParamType;
			if (!(*ParamObj)->TryGetStringField(TEXT("name"), ParamName) ||
				!(*ParamObj)->TryGetStringField(TEXT("type"), ParamType))
			{
				return MakeError(TEXT("Each parameter needs 'name' and 'type' fields"));
			}

			bool bIsArray = false;
			(*ParamObj)->TryGetBoolField(TEXT("is_array"), bIsArray);

			FEdGraphPinType PinType;
			FString TypeError;
			if (!ParsePinType(ParamType, bIsArray, PinType, TypeError))
			{
				return MakeError(FString::Printf(TEXT("Parameter '%s': %s"), *ParamName, *TypeError));
			}

			// Custom events expose parameters as output data pins
			UEdGraphPin* NewPin = EventNode->CreateUserDefinedPin(FName(*ParamName), PinType, EGPD_Output);
			if (!NewPin)
			{
				return MakeError(FString::Printf(TEXT("Failed to create parameter pin '%s'"), *ParamName));
			}

			TSharedPtr<FJsonObject> Added = MakeShared<FJsonObject>();
			Added->SetStringField(TEXT("name"), ParamName);
			Added->SetStringField(TEXT("type"), ParamType);
			AddedParams.Add(MakeShared<FJsonValueObject>(Added));
		}
	}

	NotifyAndCompile(Blueprint, Graph);

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("event_name"), EventName);
	DescribeNode(Result, TEXT("node"), EventNode);
	Result->SetArrayField(TEXT("parameters"), AddedParams);
	return Result;
}
