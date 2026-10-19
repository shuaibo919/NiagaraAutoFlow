#pragma once

#include "NiagaraAutoFlowToolset.h"

#include "NiagaraAutoFlowToolset_Scalability.generated.h"

class UNiagaraEffectType;
class UNiagaraSystem;
struct FNiagaraEmitterScalabilitySettings;
struct FNiagaraSystemScalabilitySettings;

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowQualityLevels
{
	GENERATED_BODY()

	/** Indexed by quality level, 0 = lowest. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> Names;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 CurrentQualityLevel = 0;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowSystemScalabilityValues
{
	GENERATED_BODY()

	/** Quality levels this entry applies to; empty for resolved results. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<int32> QualityLevels;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bCullByDistance = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float MaxDistance = 0.0f;

	/** Cap shared by every system of the effect type. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bCullMaxInstanceCount = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 MaxInstances = 0;

	/** Cap per system asset. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bCullPerSystemMaxInstanceCount = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 MaxSystemInstances = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bCullWhenNotRendered = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bCullByViewFrustum = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float MaxTimeWithoutRender = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float MaxTimeOutsideViewFrustum = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString CullProxyMode;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bCullByGlobalBudget = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float MaxGlobalBudgetUsage = 0.0f;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowEmitterScalabilityValues
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<int32> QualityLevels;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bScaleSpawnCount = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float SpawnCountScale = 1.0f;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowEffectTypeInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Path;

	/** SpawnOnly is cheapest, Continuous most expensive. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString UpdateFrequency;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString CullReaction;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bAllowCullingForLocalPlayers = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SignificanceHandler;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowSystemScalabilityValues> SystemSettings;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowEmitterScalabilityValues> EmitterSettings;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowEmitterEffectiveScalability
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterName;

	/** False if gated off at this quality level (costs nothing there). */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bEnabledAtQuality = true;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bScaleSpawnCount = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float SpawnCountScale = 1.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bSpawnScaleFromOverride = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<int32> EnabledRenderers;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowEffectiveScalability
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SystemPath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 QualityLevel = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString QualityLevelName;

	/** Empty means no culling or significance handling at all. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EffectTypePath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString UpdateFrequency;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString CullReaction;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bSystemEnabledAtQuality = true;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bSystemOverrideApplied = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FNiagaraAutoFlowSystemScalabilityValues System;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowEmitterEffectiveScalability> Emitters;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> Limitations;
};

/** Niagara scalability: effect type settings and the values that actually apply at a quality level after all overrides merge. */
UCLASS(BlueprintType, Hidden)
class UNiagaraAutoFlowToolset_Scalability : public UNiagaraAutoFlowToolset
{
	GENERATED_BODY()

public:
	/** Returns the effect quality level names and the editor's current level. */
	UFUNCTION(meta = (AICallable), Category = "Scalability")
	static FNiagaraAutoFlowQualityLevels GetQualityLevels();

	/**
	 * Describes an effect type: update frequency, cull reaction, significance handler and every per-quality system/emitter entry.
	 * @param EffectType The effect type asset.
	 */
	UFUNCTION(meta = (AICallable), Category = "Scalability")
	static FNiagaraAutoFlowEffectTypeInfo GetEffectTypeInfo(UNiagaraEffectType* EffectType);

	/**
	 * Resolves culling, instance caps, budget scaling, per-emitter spawn scale and emitter/renderer gating at one quality level.
	 * @param System The Niagara System.
	 * @param QualityLevel Effect quality level; negative uses the current level.
	 */
	UFUNCTION(meta = (AICallable), Category = "Scalability")
	static FNiagaraAutoFlowEffectiveScalability GetEffectiveScalability(UNiagaraSystem* System, int32 QualityLevel = -1);

	static FNiagaraAutoFlowEffectiveScalability ResolveEffectiveScalability(UNiagaraSystem& System, int32 QualityLevel);

private:
	static FNiagaraAutoFlowSystemScalabilityValues ToValues(const FNiagaraSystemScalabilitySettings& Settings);
	static FNiagaraAutoFlowEmitterScalabilityValues ToValues(const FNiagaraEmitterScalabilitySettings& Settings);
};
