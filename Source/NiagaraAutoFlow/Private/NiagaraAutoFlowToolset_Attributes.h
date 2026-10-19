#pragma once

#include "NiagaraAutoFlowToolset.h"

#include "NiagaraAutoFlowToolset_Attributes.generated.h"

class UNiagaraSystem;

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAttributeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString TypeName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 FloatComponents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 Int32Components = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 HalfComponents = 0;

	/** Bytes per particle. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 Bytes = 0;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAttributeSet
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SimTarget;

	/** False until the system has been compiled. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bCompiledDataValid = false;

	/** When false the layout may contain dead attributes. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bTrimAttributes = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bRequiresPersistentIDs = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowAttributeInfo> Attributes;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 TotalFloatComponents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 TotalInt32Components = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 TotalHalfComponents = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 BytesPerParticle = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString AllocationMode;

	/** 0 under AutomaticEstimate. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 PreAllocationCount = 0;

	/** BytesPerParticle * PreAllocationCount * 2 (double buffered). */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int64 EstimatedBytesAtPreAllocation = 0;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAttributeFlowEntry
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString TypeName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bInDataSet = false;

	/** Script usages that explicitly write the attribute. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> WrittenBy;

	/** "<RendererClass>[<Index>]". */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> BoundByRenderers;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAttributeFlow
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowAttributeFlowEntry> Attributes;

	/** Removal candidates unless another module reads them. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> WrittenButNotRendered;

	/** The renderer silently falls back to its default value. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> RenderedButMissing;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> Notes;
};

/** Niagara particle attribute set: compiled per-particle layout, payload size and the write -> render flow of each attribute. */
UCLASS(BlueprintType, Hidden)
class UNiagaraAutoFlowToolset_Attributes : public UNiagaraAutoFlowToolset
{
	GENERATED_BODY()

public:
	/**
	 * Returns an emitter's compiled particle layout: attribute types, component counts, bytes per particle and a memory estimate.
	 * @param System The Niagara System.
	 * @param EmitterName Emitter name as shown in GetSystemSummary.
	 */
	UFUNCTION(meta = (AICallable), Category = "Attributes")
	static FNiagaraAutoFlowAttributeSet GetParticleAttributeSet(UNiagaraSystem* System, FName EmitterName);

	/**
	 * Cross-references script writes and renderer bindings per attribute. Script reads are not tracked; confirm with SearchScriptSource before removing a write.
	 * @param System The Niagara System.
	 * @param EmitterName Emitter name as shown in GetSystemSummary.
	 */
	UFUNCTION(meta = (AICallable), Category = "Attributes")
	static FNiagaraAutoFlowAttributeFlow GetAttributeFlow(UNiagaraSystem* System, FName EmitterName);

	static FNiagaraAutoFlowAttributeSet BuildAttributeSet(const UNiagaraSystem& System, int32 HandleIndex);

	static FNiagaraAutoFlowAttributeFlow BuildAttributeFlow(const UNiagaraSystem& System, int32 HandleIndex);
};
