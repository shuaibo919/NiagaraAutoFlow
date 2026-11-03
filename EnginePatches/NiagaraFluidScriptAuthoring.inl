// NiagaraEditor-only implementation: graph nodes stay inside their owning DLL.
#include "AssetRegistry/AssetRegistryModule.h"
#include "NiagaraEmitter.h"
#include "NiagaraSimulationStageBase.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

namespace AutoFlowFluidGraph
{
    static void Report(FString& Error, const FString& Message) { Error = Message; }

    static const TCHAR* GeneratedKey = TEXT("AutoflowFluidModule");

    static FNiagaraTypeDefinition ResolveType(const FString& Type)
    {
        if (Type == TEXT("float")) return FNiagaraTypeDefinition::GetFloatDef();
        if (Type == TEXT("float2")) return FNiagaraTypeDefinition::GetVec2Def();
        if (Type == TEXT("float3")) return FNiagaraTypeDefinition::GetVec3Def();
        if (Type == TEXT("float4")) return FNiagaraTypeDefinition::GetVec4Def();
        if (Type == TEXT("position")) return FNiagaraTypeDefinition::GetPositionDef();
        if (Type == TEXT("int")) return FNiagaraTypeDefinition::GetIntDef();
        if (Type == TEXT("bool")) return FNiagaraTypeDefinition::GetBoolDef();
        if (Type.StartsWith(TEXT("class:")))
        {
            UClass* Class = LoadObject<UClass>(nullptr, *Type.Mid(6));
            if (Class && Class->IsChildOf<UNiagaraDataInterface>()) return FNiagaraTypeDefinition(Class);
        }
        return FNiagaraTypeDefinition();
    }

    static UNiagaraNodeOutput* AddOutput(UNiagaraGraph& Graph, ENiagaraScriptUsage Usage, FGuid UsageId)
    {
        FGraphNodeCreator<UNiagaraNodeInput> InCreator(Graph);
        UNiagaraNodeInput* Input = InCreator.CreateNode();
        Input->Input = FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn"));
        Input->Usage = ENiagaraInputNodeUsage::Parameter;
        InCreator.Finalize();
        Input->NodePosX = -300;
        FGraphNodeCreator<UNiagaraNodeOutput> OutCreator(Graph);
        UNiagaraNodeOutput* Output = OutCreator.CreateNode();
        Output->SetUsage(Usage);
        Output->SetUsageId(UsageId);
        Output->Outputs.Add(FNiagaraVariable(FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("Output")));
        OutCreator.Finalize();
        Output->NodePosX = 300;
        const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(Graph.GetSchema());
        Schema->TryCreateConnection(Input->GetOutputPin(0), Output->GetInputPin(0));
        return Output;
    }

    static UNiagaraScript* CreateModule(const TSharedPtr<FJsonObject>& Definition, FString& OutError)
    {
        const FString Path = Definition->GetStringField(TEXT("asset"));
        if (!Path.StartsWith(TEXT("/Game/Autoflow/Fluid/Modules/")))
        {
            Report(OutError, TEXT("Fluid modules must be below /Game/Autoflow/Fluid/Modules."));
            return nullptr;
        }
        UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *(Path + TEXT(".") + FPackageName::GetLongPackageAssetName(Path)));
        if (Script)
        {
            if (FString(Script->GetPackage()->GetMetaData().GetValue(Script, GeneratedKey)).IsEmpty())
            {
                Report(OutError, TEXT("Refusing to replace a module not created by AutoFlow fluid authoring."));
                return nullptr;
            }
            FNiagaraScriptAuthoringUtilities::UpdateDataInterfaceModuleHlsl(Script, Definition->GetStringField(TEXT("hlsl")), OutError);
            return Script;
        }
        UPackage* Package = CreatePackage(*Path);
        Script = NewObject<UNiagaraScript>(Package, *FPackageName::GetLongPackageAssetName(Path), RF_Public | RF_Standalone | RF_Transactional);
        Script->SetUsage(ENiagaraScriptUsage::Module);
        UNiagaraScriptSource* Source = NewObject<UNiagaraScriptSource>(Script, NAME_None, RF_Transactional);
        UNiagaraGraph* Graph = NewObject<UNiagaraGraph>(Source, NAME_None, RF_Transactional);
        Source->NodeGraph = Graph;
        Script->SetLatestSource(Source);
        UNiagaraNodeOutput* Output = AddOutput(*Graph, ENiagaraScriptUsage::Module, FGuid());
        UNiagaraNodeInput* Map = CastChecked<UNiagaraNodeInput>(Output->GetInputPin(0)->LinkedTo[0]->GetOwningNode());
        const UEdGraphSchema_Niagara* Schema = CastChecked<UEdGraphSchema_Niagara>(Graph->GetSchema());
        Output->GetInputPin(0)->BreakAllPinLinks();
        FGraphNodeCreator<UNiagaraNodeCustomHlsl> CustomCreator(*Graph);
        UNiagaraNodeCustomHlsl* Custom = CustomCreator.CreateNode();
        CustomCreator.Finalize();
        UEdGraphPin* MapIn = Custom->RequestNewTypedPin(EGPD_Input, FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapIn"));
        UEdGraphPin* MapOut = Custom->RequestNewTypedPin(EGPD_Output, FNiagaraTypeDefinition::GetParameterMapDef(), TEXT("MapOut"));
        Schema->TryCreateConnection(Map->GetOutputPin(0), MapIn);
        Schema->TryCreateConnection(MapOut, Output->GetInputPin(0));
        FGraphNodeCreator<UNiagaraNodeParameterMapGet> GetCreator(*Graph);
        UNiagaraNodeParameterMapGet* Get = GetCreator.CreateNode();
        GetCreator.Finalize();
        Schema->TryCreateConnection(Map->GetOutputPin(0), Get->GetInputPin(0));
        for (const TSharedPtr<FJsonValue>& Item : Definition->GetArrayField(TEXT("inputs")))
        {
            const TSharedPtr<FJsonObject> Pin = Item->AsObject();
            const FString Name = Pin->GetStringField(TEXT("name"));
            const FString Variable = Pin->GetStringField(TEXT("variable"));
            const FNiagaraTypeDefinition Type = ResolveType(Pin->GetStringField(TEXT("type")));
            if (!Type.IsValid())
            {
                Report(OutError, TEXT("Unsupported fluid-module input type: ") + Pin->GetStringField(TEXT("type")));
                return nullptr;
            }
            Graph->AddParameter(FNiagaraVariable(Type, *Variable), FNiagaraVariableMetaData(), false, false);
            UEdGraphPin* ValuePin = Get->RequestNewTypedPin(EGPD_Output, Type, *Variable);
            UEdGraphPin* ArgumentPin = Custom->RequestNewTypedPin(EGPD_Input, Type, *Name);
            if (!Schema->TryCreateConnection(ValuePin, ArgumentPin))
            {
                Report(OutError, TEXT("Could not connect fluid-module input: ") + Name);
                return nullptr;
            }
        }
        Custom->SetCustomHlsl(Definition->GetStringField(TEXT("hlsl")));
        FVersionedNiagaraScriptData* ScriptData = Script->GetLatestScriptData();
        ScriptData->ModuleUsageBitmask = 1 << static_cast<uint8>(ENiagaraScriptUsage::ParticleSimulationStageScript);
        ScriptData->LibraryVisibility = ENiagaraScriptLibraryVisibility::Library;
        ScriptData->Category = FText::FromString(TEXT("Autoflow / Fluid"));
        ScriptData->Description = FText::FromString(Definition->GetStringField(TEXT("description")));
        Package->GetMetaData().SetValue(Script, GeneratedKey, TEXT("1"));
        Graph->MarkGraphRequiresSynchronization(TEXT("Autoflow Grid2D module"));
        Script->MarkPackageDirty();
        Script->PostEditChange();
        FAssetRegistryModule::AssetCreated(Script);
        return Script;
    }

}

UNiagaraScript* FNiagaraScriptAuthoringUtilities::CreateDataInterfaceModuleScript(const FString& DefinitionJson, FString& OutError)
{
    TSharedPtr<FJsonObject> Definition;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(DefinitionJson), Definition) || !Definition)
    { OutError = TEXT("Invalid data-interface module JSON."); return nullptr; }
    return AutoFlowFluidGraph::CreateModule(Definition, OutError);
}

bool FNiagaraScriptAuthoringUtilities::AddDataInterfaceSimulationStage(UNiagaraEmitter* Emitter, const FGuid& Version,
    UNiagaraScript* Module, FName StageName, const FNiagaraVariable& IterationInterface, FString& OutError)
{
    FVersionedNiagaraEmitterData* Data = Emitter ? Emitter->GetEmitterData(Version) : nullptr;
    UNiagaraScriptSource* Source = Data ? Cast<UNiagaraScriptSource>(Data->GraphSource) : nullptr;
    if (!Source || !Source->NodeGraph || !Module)
    { OutError = TEXT("Expected an editable emitter graph and module."); return false; }
    UNiagaraSimulationStageGeneric* Stage = NewObject<UNiagaraSimulationStageGeneric>(Emitter, NAME_None, RF_Transactional);
    Stage->SimulationStageName = StageName;
    Stage->IterationSource = ENiagaraIterationSource::DataInterface;
    Stage->DataInterface.BoundVariable = IterationInterface;
    Stage->NumIterations.SetDefaultParameter(FNiagaraTypeDefinition::GetIntDef(), int32(1));
    Stage->ExecuteBehavior = ENiagaraSimStageExecuteBehavior::Always;
    Stage->Script = NewObject<UNiagaraScript>(Stage, NAME_None, RF_Transactional);
    Stage->Script->SetUsage(ENiagaraScriptUsage::ParticleSimulationStageScript);
    Stage->Script->SetUsageId(FGuid::NewGuid());
    Stage->Script->SetLatestSource(Source);
    UNiagaraNodeOutput* Output = AutoFlowFluidGraph::AddOutput(*Source->NodeGraph,
        ENiagaraScriptUsage::ParticleSimulationStageScript, Stage->Script->GetUsageId());
    if (!FNiagaraStackGraphUtilities::AddScriptModuleToStack(Module, *Output))
    { OutError = TEXT("Could not insert the data-interface simulation-stage module."); return false; }
    Emitter->AddSimulationStage(Stage, Version);
    Source->NodeGraph->MarkGraphRequiresSynchronization(TEXT("Autoflow Grid2D simulation stage"));
    return true;
}

bool FNiagaraScriptAuthoringUtilities::UpdateDataInterfaceModuleHlsl(UNiagaraScript* Module, const FString& Hlsl, FString& OutError)
{
    using namespace AutoFlowFluidGraph;
    if (!Module || FString(Module->GetPackage()->GetMetaData().GetValue(Module, GeneratedKey)).IsEmpty() || Hlsl.IsEmpty())
    {
        Report(OutError, TEXT("Expected an AutoFlow-created fluid module and a nonempty HLSL body."));
        return false;
    }
    UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Module->GetLatestSource());
    if (!Source || !Source->NodeGraph) return false;
    for (UEdGraphNode* Node : Source->NodeGraph->Nodes)
    {
        if (UNiagaraNodeCustomHlsl* Custom = Cast<UNiagaraNodeCustomHlsl>(Node))
        {
            Custom->Modify();
            Custom->SetCustomHlsl(Hlsl);
            Source->NodeGraph->MarkGraphRequiresSynchronization(TEXT("Autoflow fluid HLSL update"));
            Module->MarkPackageDirty();
            Module->PostEditChange();
            return true;
        }
    }
    return false;
}

bool FNiagaraScriptAuthoringUtilities::AddDataInterfaceModuleInputs(UNiagaraScript* Module, const FString& InputsJson, FString& OutError)
{
    using namespace AutoFlowFluidGraph;
    if (!IsInGameThread() || !Module || FString(Module->GetPackage()->GetMetaData().GetValue(Module, GeneratedKey)).IsEmpty())
    { OutError = TEXT("Expected an AutoFlow-generated fluid module on the game thread."); return false; }
    TArray<TSharedPtr<FJsonValue>> Pins;
    if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(InputsJson), Pins) || Pins.Num() > 32)
    { OutError = TEXT("Expected up to 32 typed User input definitions."); return false; }
    UNiagaraScriptSource* Source = Cast<UNiagaraScriptSource>(Module->GetLatestSource());
    UNiagaraGraph* Graph = Source ? Source->NodeGraph : nullptr;
    UNiagaraNodeCustomHlsl* Custom = nullptr;
    UNiagaraNodeParameterMapGet* Get = nullptr;
    if (Graph) for (UEdGraphNode* Node : Graph->Nodes)
    {
        if (auto* Found = Cast<UNiagaraNodeCustomHlsl>(Node)) Custom = Found;
        if (auto* Found = Cast<UNiagaraNodeParameterMapGet>(Node)) Get = Found;
    }
    if (!Custom || !Get) { OutError = TEXT("Generated fluid graph is missing its input nodes."); return false; }
    for (const auto& Value : Pins)
    {
        const auto Pin = Value->AsObject();
        FString Name, Variable, Type;
        if (!Pin || !Pin->TryGetStringField(TEXT("name"), Name) || !Pin->TryGetStringField(TEXT("variable"), Variable)
            || !Pin->TryGetStringField(TEXT("type"), Type) || !NiagaraScriptAuthoring::Identifier(Name)
            || !Variable.StartsWith(TEXT("User.")) || !NiagaraScriptAuthoring::Identifier(Variable.Mid(5))
            || !(Type == TEXT("float") || Type == TEXT("float2") || Type == TEXT("float3") || Type == TEXT("int")))
        { OutError = TEXT("Inputs must be named scalar/vector User parameters."); return false; }
    }
    Graph->Modify(); Get->Modify(); Custom->Modify(); Module->Modify();
    const auto* Schema = CastChecked<UEdGraphSchema_Niagara>(Graph->GetSchema());
    for (const auto& Value : Pins)
    {
        const auto Pin = Value->AsObject();
        const FName Name(*Pin->GetStringField(TEXT("name")));
        if (Custom->FindPin(Name, EGPD_Input)) continue;
        const FName Variable(*Pin->GetStringField(TEXT("variable")));
        const auto Type = ResolveType(Pin->GetStringField(TEXT("type")));
        Graph->AddParameter(FNiagaraVariable(Type, Variable), FNiagaraVariableMetaData(), false, false);
        UEdGraphPin* Read = Get->FindPin(Variable, EGPD_Output);
        if (!Read) Read = Get->RequestNewTypedPin(EGPD_Output, Type, Variable);
        UEdGraphPin* Argument = Custom->RequestNewTypedPin(EGPD_Input, Type, Name);
        if (!Schema->TryCreateConnection(Read, Argument))
        { OutError = TEXT("Could not connect the new User input."); return false; }
    }
    Graph->MarkGraphRequiresSynchronization(TEXT("Autoflow fluid User inputs"));
    Module->MarkPackageDirty();
    Module->PostEditChange();
    return true;
}
