#include "NiagaraAutoFlowToolset_Attributes.h"

#include "NiagaraDataSetCompiledData.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraAutoFlowToolset_Attributes)

FNiagaraAutoFlowAttributeSet UNiagaraAutoFlowToolset_Attributes::GetParticleAttributeSet(UNiagaraSystem* System, FName EmitterName)
{
	if (!ValidateSystem(System) || FindEmitterData(*System, EmitterName) == nullptr)
	{
		return FNiagaraAutoFlowAttributeSet();
	}
	return BuildAttributeSet(*System, FindEmitterIndex(*System, EmitterName));
}

FNiagaraAutoFlowAttributeFlow UNiagaraAutoFlowToolset_Attributes::GetAttributeFlow(UNiagaraSystem* System, FName EmitterName)
{
	if (!ValidateSystem(System) || FindEmitterData(*System, EmitterName) == nullptr)
	{
		return FNiagaraAutoFlowAttributeFlow();
	}
	return BuildAttributeFlow(*System, FindEmitterIndex(*System, EmitterName));
}

FNiagaraAutoFlowAttributeSet UNiagaraAutoFlowToolset_Attributes::BuildAttributeSet(const UNiagaraSystem& System, int32 HandleIndex)
{
	const FNiagaraEmitterHandle& Handle = System.GetEmitterHandles()[HandleIndex];
	const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	check(EmitterData);

	FNiagaraAutoFlowAttributeSet Set;
	Set.EmitterName = Handle.GetName().ToString();
	Set.SimTarget = EnumToString(EmitterData->SimTarget);
	Set.bTrimAttributes = System.ShouldTrimAttributes();
	Set.bRequiresPersistentIDs = EmitterData->bRequiresPersistentIDs;
	Set.AllocationMode = EnumToString(EmitterData->AllocationMode);
	Set.PreAllocationCount = EmitterData->AllocationMode == EParticleAllocationMode::AutomaticEstimate ? 0 : EmitterData->PreAllocationCount;

	const TArray<TSharedRef<const FNiagaraEmitterCompiledData>>& CompiledData = System.GetEmitterCompiledData();
	if (!CompiledData.IsValidIndex(HandleIndex))
	{
		return Set;
	}

	const FNiagaraDataSetCompiledData& DataSet = CompiledData[HandleIndex]->DataSetCompiledData;
	Set.bCompiledDataValid = DataSet.Variables.Num() > 0;
	for (int32 VarIndex = 0; VarIndex < DataSet.Variables.Num(); ++VarIndex)
	{
		const FNiagaraVariableBase& Variable = DataSet.Variables[VarIndex];
		FNiagaraAutoFlowAttributeInfo& Info = Set.Attributes.AddDefaulted_GetRef();
		Info.Name = NormalizeAttributeName(Variable.GetName().ToString());
		Info.TypeName = Variable.GetType().GetName();

		if (DataSet.VariableLayouts.IsValidIndex(VarIndex))
		{
			const FNiagaraVariableLayoutInfo& Layout = DataSet.VariableLayouts[VarIndex];
			Info.FloatComponents = static_cast<int32>(Layout.GetNumFloatComponents());
			Info.Int32Components = static_cast<int32>(Layout.GetNumInt32Components());
			Info.HalfComponents = static_cast<int32>(Layout.GetNumHalfComponents());
			Info.Bytes = (Info.FloatComponents + Info.Int32Components) * 4 + Info.HalfComponents * 2;
		}
	}

	Set.TotalFloatComponents = static_cast<int32>(DataSet.TotalFloatComponents);
	Set.TotalInt32Components = static_cast<int32>(DataSet.TotalInt32Components);
	Set.TotalHalfComponents = static_cast<int32>(DataSet.TotalHalfComponents);
	Set.BytesPerParticle = (Set.TotalFloatComponents + Set.TotalInt32Components) * 4 + Set.TotalHalfComponents * 2;
	Set.EstimatedBytesAtPreAllocation = static_cast<int64>(Set.BytesPerParticle) * Set.PreAllocationCount * 2;
	return Set;
}

FNiagaraAutoFlowAttributeFlow UNiagaraAutoFlowToolset_Attributes::BuildAttributeFlow(const UNiagaraSystem& System, int32 HandleIndex)
{
	const FNiagaraEmitterHandle& Handle = System.GetEmitterHandles()[HandleIndex];
	const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData();
	check(EmitterData);

	FNiagaraAutoFlowAttributeFlow Flow;
	Flow.EmitterName = Handle.GetName().ToString();

	TMap<FString, int32> EntryIndices;
	auto FindOrAddEntry = [&Flow, &EntryIndices](const FString& Name, const FString& TypeName) -> FNiagaraAutoFlowAttributeFlowEntry&
	{
		if (const int32* Existing = EntryIndices.Find(Name))
		{
			return Flow.Attributes[*Existing];
		}
		EntryIndices.Add(Name, Flow.Attributes.Num());
		FNiagaraAutoFlowAttributeFlowEntry& Entry = Flow.Attributes.AddDefaulted_GetRef();
		Entry.Name = Name;
		Entry.TypeName = TypeName;
		return Entry;
	};

	const FNiagaraAutoFlowAttributeSet Set = BuildAttributeSet(System, HandleIndex);
	for (const FNiagaraAutoFlowAttributeInfo& Info : Set.Attributes)
	{
		FindOrAddEntry(Info.Name, Info.TypeName).bInDataSet = true;
	}
	if (!Set.bCompiledDataValid)
	{
		Flow.Notes.Add(TEXT("No compiled data layout; compile the system first. bInDataSet and RenderedButMissing are unreliable."));
	}

#if WITH_EDITORONLY_DATA
	TArray<FNiagaraAutoFlowScriptSlot> Slots;
	GatherScriptSlots(System, Slots);
	for (const FNiagaraAutoFlowScriptSlot& Slot : Slots)
	{
		if (Slot.EmitterName != Flow.EmitterName)
		{
			continue;
		}
		for (const FNiagaraVariableBase& Written : Slot.Script->GetVMExecutableData().AttributesWritten)
		{
			FNiagaraAutoFlowAttributeFlowEntry& Entry = FindOrAddEntry(NormalizeAttributeName(Written.GetName().ToString()), Written.GetType().GetName());
			Entry.WrittenBy.AddUnique(Slot.ScriptUsage);
		}
	}
#endif

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
	{
		const UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];
		if (Renderer == nullptr || !Renderer->GetIsEnabled())
		{
			continue;
		}

		const FString Label = FString::Printf(TEXT("%s[%d]"), *Renderer->GetClass()->GetName(), RendererIndex);
		for (const FNiagaraVariableAttributeBinding* Binding : Renderer->GetAttributeBindings())
		{
			if (Binding == nullptr || !Binding->IsParticleBinding())
			{
				continue;
			}
			const FNiagaraVariableBase BoundVariable = Binding->GetDataSetBindableVariable();
			FNiagaraAutoFlowAttributeFlowEntry& Entry = FindOrAddEntry(NormalizeAttributeName(BoundVariable.GetName().ToString()), BoundVariable.GetType().GetName());
			Entry.BoundByRenderers.AddUnique(Label);
		}
	}

	for (const FNiagaraAutoFlowAttributeFlowEntry& Entry : Flow.Attributes)
	{
		if (Entry.WrittenBy.Num() > 0 && Entry.BoundByRenderers.Num() == 0)
		{
			Flow.WrittenButNotRendered.Add(Entry.Name);
		}
		if (Set.bCompiledDataValid && !Entry.bInDataSet && Entry.BoundByRenderers.Num() > 0)
		{
			Flow.RenderedButMissing.Add(Entry.Name);
		}
	}

	Flow.Notes.Add(TEXT("WrittenBy lists explicit graph writes only. Attributes such as Age, Lifetime or NormalizedAge are often read by later modules; verify with SearchScriptSource before removing."));
	return Flow;
}
