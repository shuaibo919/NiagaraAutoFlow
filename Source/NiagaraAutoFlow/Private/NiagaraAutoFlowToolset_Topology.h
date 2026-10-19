#pragma once

#include "NiagaraAutoFlowToolset.h"

#include "NiagaraAutoFlowToolset_Topology.generated.h"

class UNiagaraRendererProperties;
class UNiagaraSystem;

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowSimStageInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bEnabled = true;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString StageClass;

	/** False for CPU, disabled or uncompiled stages; the fields below are then defaults. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bHasCompiledMetaData = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString IterationSource;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString IterationDataInterface;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ExecuteBehavior;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumIterations = 1;

	/** Parameter driving NumIterations at runtime, if any. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString NumIterationsBinding;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString GpuDispatchType;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString GpuDirectDispatchElementType;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FIntVector ElementCount = FIntVector::ZeroValue;

	/** Zero means the engine default thread group size. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FIntVector GpuDispatchNumThreads = FIntVector::ZeroValue;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bWritesParticles = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bPartialParticleUpdate = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bGpuIndirectDispatch = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> InputDataInterfaces;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> OutputDestinations;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowEventHandlerInfo
{
	GENERATED_BODY()

	/** UsageIndex for ScriptUsage ParticleEventScript in the Compiled toolset. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SourceEmitterName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SourceEventName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ExecutionMode;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 SpawnNumber = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bRandomSpawnNumber = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 MinSpawnNumber = 0;

	/** 0 = unlimited. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 MaxEventsPerFrame = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bUpdateAttributeInitialValues = false;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowMaterialUse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Path;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString BlendMode;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bTranslucent = false;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowRendererInfo
{
	GENERATED_BODY()

	/** Same as FNiagaraExt_StackItemReference::RendererIndex. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 Index = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ClassName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bEnabled = true;

	/** As authored; user-parameter material bindings only resolve at runtime. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowMaterialUse> Materials;

	/** Sprite and mesh renderers only. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SortMode;

	/** Flipbook grid; 1x1 means no flipbook. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FVector2D SubImageSize = FVector2D(1.0, 1.0);

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 SortOrderHint = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<int32> EnabledQualityLevels;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumParticleBindings = 0;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowEmitterProfile
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bEnabled = true;

	/** Stateless emitters only report the fields above. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterMode;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SimTarget;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bLocalSpace = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bDeterminism = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString AllocationMode;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 PreAllocationCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString CalculateBoundsMode;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FVector FixedBoundsMin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FVector FixedBoundsMax = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bRequiresPersistentIDs = false;

	/** 0 uses fx.MaxNiagaraGPUParticlesSpawnPerFrame. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 MaxGPUParticlesSpawnPerFrame = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString InterpolatedSpawnMode;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<int32> EnabledQualityLevels;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumEventHandlers = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumSimulationStages = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowRendererInfo> Renderers;
};

/** Emitter structure the official toolsets do not walk: simulation stages, event handlers, typed emitter and renderer settings. */
UCLASS(BlueprintType, Hidden)
class UNiagaraAutoFlowToolset_Topology : public UNiagaraAutoFlowToolset
{
	GENERATED_BODY()

public:
	/**
	 * Lists simulation stages with iteration source/count, dispatch type and thread counts (compiled data from the GPU compute script).
	 * @param System The Niagara System.
	 * @param EmitterName Emitter name as shown in GetSystemSummary.
	 */
	UFUNCTION(meta = (AICallable), Category = "Topology")
	static TArray<FNiagaraAutoFlowSimStageInfo> GetSimulationStages(UNiagaraSystem* System, FName EmitterName);

	/**
	 * Lists event handlers: source emitter and event, execution mode and spawn settings.
	 * @param System The Niagara System.
	 * @param EmitterName Emitter name as shown in GetSystemSummary.
	 */
	UFUNCTION(meta = (AICallable), Category = "Topology")
	static TArray<FNiagaraAutoFlowEventHandlerInfo> GetEventHandlers(UNiagaraSystem* System, FName EmitterName);

	/**
	 * Returns typed emitter settings plus per-renderer materials, blend modes, sort mode and flipbook size.
	 * @param System The Niagara System.
	 * @param EmitterName Emitter name; None returns every emitter.
	 */
	UFUNCTION(meta = (AICallable), Category = "Topology")
	static TArray<FNiagaraAutoFlowEmitterProfile> GetEmitterProfiles(UNiagaraSystem* System, FName EmitterName);

	static FNiagaraAutoFlowEmitterProfile BuildEmitterProfile(const UNiagaraSystem& System, int32 HandleIndex);

	static TArray<FNiagaraAutoFlowSimStageInfo> BuildSimulationStages(const FVersionedNiagaraEmitterData& EmitterData);

private:
	static FNiagaraAutoFlowRendererInfo BuildRendererInfo(const UNiagaraRendererProperties& Renderer, int32 Index);
};
