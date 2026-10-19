#pragma once

#include "ToolsetRegistry/ToolsetDefinition.h"
#include "UObject/Class.h"

#include "NiagaraAutoFlowToolset.generated.h"

class UNiagaraScript;
class UNiagaraSystem;
struct FNiagaraEmitterHandle;
struct FNiagaraPlatformSet;
struct FVersionedNiagaraEmitterData;

/** A compiled script addressed as (EmitterName, ScriptUsage, UsageIndex); EmitterName is empty for system scripts. */
struct FNiagaraAutoFlowScriptSlot
{
	const UNiagaraScript* Script = nullptr;
	FString EmitterName;
	FString ScriptUsage;

	/** Distinguishes repeated usages, e.g. several event handlers. */
	int32 UsageIndex = 0;
};

/**
 * Base for NiagaraAutoFlow toolsets. Bad arguments come from the AI caller, so they are raised as script errors
 * (returned to the caller) rather than check().
 */
UCLASS(Abstract, BlueprintType, Hidden)
class UNiagaraAutoFlowToolset : public UToolsetDefinition
{
	GENERATED_BODY()

public:
	static void Error(const FString& Message);

	static bool ValidateSystem(const UNiagaraSystem* System);

	/** Raises an error listing the available emitters and returns INDEX_NONE when not found. */
	static int32 FindEmitterIndex(const UNiagaraSystem& System, FName EmitterName);

	static const FNiagaraEmitterHandle* FindEmitterHandle(const UNiagaraSystem& System, FName EmitterName);

	/** Also requires a Standard (non-stateless) emitter. */
	static FVersionedNiagaraEmitterData* FindEmitterData(const UNiagaraSystem& System, FName EmitterName);

	static void GatherScriptSlots(const UNiagaraSystem& System, TArray<FNiagaraAutoFlowScriptSlot>& OutSlots);

	static const FNiagaraAutoFlowScriptSlot* FindScriptSlot(const TArray<FNiagaraAutoFlowScriptSlot>& Slots, FName EmitterName, const FString& ScriptUsage, int32 UsageIndex);

	/** Data set variables carry no Particles. namespace while renderer bindings do. */
	static FString NormalizeAttributeName(const FString& Name);

	static TArray<FString> GetEmitterNames(const UNiagaraSystem& System);

	static int32 GetNumQualityLevels();

	/** Evaluates the effect quality mask only; device-profile and CVar conditions are ignored. */
	static TArray<int32> GetEnabledQualityLevels(const FNiagaraPlatformSet& Platforms);

	template<typename TEnum>
	static FString EnumToString(TEnum Value)
	{
		return StaticEnum<TEnum>()->GetNameStringByValue(static_cast<int64>(Value));
	}
};
