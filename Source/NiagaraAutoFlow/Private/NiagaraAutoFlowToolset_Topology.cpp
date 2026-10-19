#include "NiagaraAutoFlowToolset_Topology.h"

#include "Materials/MaterialInterface.h"
#include "MaterialShared.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraScript.h"
#include "NiagaraSimulationStageBase.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraSystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraAutoFlowToolset_Topology)

TArray<FNiagaraAutoFlowSimStageInfo> UNiagaraAutoFlowToolset_Topology::GetSimulationStages(UNiagaraSystem* System, FName EmitterName)
{
	if (!ValidateSystem(System))
	{
		return {};
	}
	const FVersionedNiagaraEmitterData* EmitterData = FindEmitterData(*System, EmitterName);
	return EmitterData ? BuildSimulationStages(*EmitterData) : TArray<FNiagaraAutoFlowSimStageInfo>();
}

TArray<FNiagaraAutoFlowEventHandlerInfo> UNiagaraAutoFlowToolset_Topology::GetEventHandlers(UNiagaraSystem* System, FName EmitterName)
{
	TArray<FNiagaraAutoFlowEventHandlerInfo> Result;
	if (!ValidateSystem(System))
	{
		return Result;
	}
	const FVersionedNiagaraEmitterData* EmitterData = FindEmitterData(*System, EmitterName);
	if (EmitterData == nullptr)
	{
		return Result;
	}

	const TArray<FNiagaraEventScriptProperties>& Handlers = EmitterData->GetEventHandlers();
	for (int32 Index = 0; Index < Handlers.Num(); ++Index)
	{
		const FNiagaraEventScriptProperties& Props = Handlers[Index];
		FNiagaraAutoFlowEventHandlerInfo& Info = Result.AddDefaulted_GetRef();
		Info.Index = Index;
		Info.SourceEventName = Props.SourceEventName.ToString();
		Info.ExecutionMode = EnumToString(Props.ExecutionMode);
		Info.SpawnNumber = static_cast<int32>(Props.SpawnNumber);
		Info.bRandomSpawnNumber = Props.bRandomSpawnNumber;
		Info.MinSpawnNumber = static_cast<int32>(Props.MinSpawnNumber);
		Info.MaxEventsPerFrame = static_cast<int32>(Props.MaxEventsPerFrame);
		Info.bUpdateAttributeInitialValues = Props.UpdateAttributeInitialValues;

		Info.SourceEmitterName = EmitterName.ToString();
		if (Props.SourceEmitterID.IsValid())
		{
			const FNiagaraEmitterHandle* Source = System->GetEmitterHandles().FindByPredicate([&Props](const FNiagaraEmitterHandle& Handle) { return Handle.GetId() == Props.SourceEmitterID; });
			Info.SourceEmitterName = Source ? Source->GetName().ToString() : FString::Printf(TEXT("<unknown emitter %s>"), *Props.SourceEmitterID.ToString());
		}
	}
	return Result;
}

TArray<FNiagaraAutoFlowEmitterProfile> UNiagaraAutoFlowToolset_Topology::GetEmitterProfiles(UNiagaraSystem* System, FName EmitterName)
{
	TArray<FNiagaraAutoFlowEmitterProfile> Result;
	if (!ValidateSystem(System))
	{
		return Result;
	}

	if (!EmitterName.IsNone())
	{
		const int32 Index = FindEmitterIndex(*System, EmitterName);
		if (Index != INDEX_NONE)
		{
			Result.Add(BuildEmitterProfile(*System, Index));
		}
		return Result;
	}

	for (int32 Index = 0; Index < System->GetEmitterHandles().Num(); ++Index)
	{
		Result.Add(BuildEmitterProfile(*System, Index));
	}
	return Result;
}

FNiagaraAutoFlowEmitterProfile UNiagaraAutoFlowToolset_Topology::BuildEmitterProfile(const UNiagaraSystem& System, int32 HandleIndex)
{
	const FNiagaraEmitterHandle& Handle = System.GetEmitterHandles()[HandleIndex];

	FNiagaraAutoFlowEmitterProfile Profile;
	Profile.EmitterName = Handle.GetName().ToString();
	Profile.bEnabled = Handle.GetIsEnabled();
	Profile.EmitterMode = EnumToString(Handle.GetEmitterMode());
	for (int32 QualityLevel = 0; QualityLevel < GetNumQualityLevels(); ++QualityLevel)
	{
		if (Handle.IsEnabledOnEffectQualityLevel(QualityLevel))
		{
			Profile.EnabledQualityLevels.Add(QualityLevel);
		}
	}

	const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterMode() == ENiagaraEmitterMode::Standard ? Handle.GetEmitterData() : nullptr;
	if (EmitterData == nullptr)
	{
		return Profile;
	}

	Profile.SimTarget = EnumToString(EmitterData->SimTarget);
	Profile.bLocalSpace = EmitterData->bLocalSpace;
	Profile.bDeterminism = EmitterData->bDeterminism;
	Profile.AllocationMode = EnumToString(EmitterData->AllocationMode);
	Profile.PreAllocationCount = EmitterData->PreAllocationCount;
	Profile.CalculateBoundsMode = EnumToString(EmitterData->CalculateBoundsMode);
	Profile.FixedBoundsMin = EmitterData->FixedBounds.Min;
	Profile.FixedBoundsMax = EmitterData->FixedBounds.Max;
	Profile.bRequiresPersistentIDs = EmitterData->bRequiresPersistentIDs;
	Profile.MaxGPUParticlesSpawnPerFrame = EmitterData->MaxGPUParticlesSpawnPerFrame;
	Profile.InterpolatedSpawnMode = EnumToString(EmitterData->InterpolatedSpawnMode);
	Profile.NumEventHandlers = EmitterData->GetEventHandlers().Num();
	Profile.NumSimulationStages = EmitterData->GetSimulationStages().Num();

	const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
	for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
	{
		if (Renderers[RendererIndex] != nullptr)
		{
			Profile.Renderers.Add(BuildRendererInfo(*Renderers[RendererIndex], RendererIndex));
		}
	}
	return Profile;
}

TArray<FNiagaraAutoFlowSimStageInfo> UNiagaraAutoFlowToolset_Topology::BuildSimulationStages(const FVersionedNiagaraEmitterData& EmitterData)
{
	TArray<FNiagaraAutoFlowSimStageInfo> Result;

	const UNiagaraScript* GPUScript = EmitterData.SimTarget == ENiagaraSimTarget::GPUComputeSim ? EmitterData.GetGPUComputeScript() : nullptr;
	const TArray<FSimulationStageMetaData>* MetaData = GPUScript ? &GPUScript->GetVMExecutableData().SimulationStageMetaData : nullptr;

	const TArray<UNiagaraSimulationStageBase*>& Stages = EmitterData.GetSimulationStages();
	for (int32 Index = 0; Index < Stages.Num(); ++Index)
	{
		const UNiagaraSimulationStageBase* Stage = Stages[Index];
		if (Stage == nullptr)
		{
			continue;
		}

		FNiagaraAutoFlowSimStageInfo& Info = Result.AddDefaulted_GetRef();
		Info.Index = Index;
		Info.Name = Stage->SimulationStageName.ToString();
		Info.bEnabled = Stage->bEnabled;
		Info.StageClass = Stage->GetClass()->GetName();

		const FSimulationStageMetaData* StageMeta = MetaData ? MetaData->FindByPredicate([Stage](const FSimulationStageMetaData& Meta) { return Meta.SimulationStageName == Stage->SimulationStageName; }) : nullptr;
		if (StageMeta == nullptr)
		{
			continue;
		}

		Info.bHasCompiledMetaData = true;
		Info.IterationSource = EnumToString(StageMeta->IterationSourceType);
		Info.IterationDataInterface = StageMeta->IterationDataInterface.ToString();
		Info.ExecuteBehavior = EnumToString(StageMeta->ExecuteBehavior);
		Info.NumIterations = StageMeta->NumIterations;
		Info.NumIterationsBinding = StageMeta->NumIterationsBinding.IsNone() ? FString() : StageMeta->NumIterationsBinding.ToString();
		Info.GpuDispatchType = EnumToString(StageMeta->GpuDispatchType);
		Info.GpuDirectDispatchElementType = EnumToString(StageMeta->GpuDirectDispatchElementType);
		Info.ElementCount = StageMeta->ElementCount;
		Info.GpuDispatchNumThreads = StageMeta->GpuDispatchNumThreads;
		Info.bWritesParticles = StageMeta->bWritesParticles;
		Info.bPartialParticleUpdate = StageMeta->bPartialParticleUpdate;
		Info.bGpuIndirectDispatch = StageMeta->bGpuIndirectDispatch;
		for (const FName& DIName : StageMeta->InputDataInterfaces)
		{
			Info.InputDataInterfaces.Add(DIName.ToString());
		}
		for (const FName& Destination : StageMeta->OutputDestinations)
		{
			Info.OutputDestinations.Add(Destination.ToString());
		}
	}
	return Result;
}

FNiagaraAutoFlowRendererInfo UNiagaraAutoFlowToolset_Topology::BuildRendererInfo(const UNiagaraRendererProperties& Renderer, int32 Index)
{
	FNiagaraAutoFlowRendererInfo Info;
	Info.Index = Index;
	Info.ClassName = Renderer.GetClass()->GetName();
	Info.bEnabled = Renderer.GetIsEnabled();
	Info.SortOrderHint = Renderer.SortOrderHint;
	Info.EnabledQualityLevels = GetEnabledQualityLevels(Renderer.Platforms);

	for (const FNiagaraVariableAttributeBinding* Binding : Renderer.GetAttributeBindings())
	{
		Info.NumParticleBindings += (Binding && Binding->IsParticleBinding()) ? 1 : 0;
	}

	TArray<UMaterialInterface*> Materials;
	Renderer.GetUsedMaterials(nullptr, Materials);
	for (const UMaterialInterface* Material : Materials)
	{
		FNiagaraAutoFlowMaterialUse& Use = Info.Materials.AddDefaulted_GetRef();
		if (Material == nullptr)
		{
			Use.Path = TEXT("<none>");
			continue;
		}
		Use.Path = Material->GetPathName();
		Use.BlendMode = EnumToString(Material->GetBlendMode());
		Use.bTranslucent = IsTranslucentBlendMode(*Material);
	}

	if (const UNiagaraSpriteRendererProperties* Sprite = Cast<UNiagaraSpriteRendererProperties>(&Renderer))
	{
		Info.SortMode = EnumToString(Sprite->SortMode);
		Info.SubImageSize = Sprite->SubImageSize;
	}
	else if (const UNiagaraMeshRendererProperties* Mesh = Cast<UNiagaraMeshRendererProperties>(&Renderer))
	{
		Info.SortMode = EnumToString(Mesh->SortMode);
		Info.SubImageSize = Mesh->SubImageSize;
	}
	return Info;
}
