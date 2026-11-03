#pragma once

#include "NiagaraAutoFlowToolset.h"
#include "NiagaraScriptAuthoringUtilities.h"
#include "ToolsetRegistry/ToolCallAsyncResult.h"
#include "NiagaraAutoFlowToolset_Authoring.generated.h"

class UNiagaraScript;

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAuthoringResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	TObjectPtr<UNiagaraScript> Script = nullptr;

	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	FNiagaraModuleAuthoringDefinition Definition;

	/** Restore with UpdateModuleScriptDefinition when undoing an accepted update. */
	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	FNiagaraModuleAuthoringDefinition PreviousDefinition;

	/** A context system containing this module was compiled to completion. */
	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	bool bValidated = false;

	/** True only when context validation passed, including GPU shaders when present. */
	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	bool bCompileSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	bool bRolledBack = false;

	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	bool bRollbackCompileSucceeded = false;

	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	bool bSaved = false;

	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	int32 GPUComputeScriptsChecked = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Authoring")
	TArray<FString> Diagnostics;
};

UCLASS(BlueprintType)
class UNiagaraAutoFlowAsyncAuthoringResult : public UToolCallAsyncResult
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "NiagaraAutoFlow")
	bool SetValue(const FNiagaraAutoFlowAuthoringResult& InValue)
	{
		return MaybeBroadcastSuccessfulCompletion(FNiagaraAutoFlowAuthoringResult(InValue), Value);
	}

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FNiagaraAutoFlowAuthoringResult Value;
};

/** Author reusable single-node Custom HLSL module assets. Creation/update alone is not compilation validation. */
UCLASS(BlueprintType, Hidden)
class UNiagaraAutoFlowToolset_Authoring : public UNiagaraAutoFlowToolset
{
	GENERATED_BODY()
public:
	/** Create a new unsaved module asset. Never overwrites an existing package. Add it with official AddModule, then ValidateModuleScript. */
	UFUNCTION(meta = (AICallable), Category = "Authoring")
	static FNiagaraAutoFlowAuthoringResult CreateModuleScriptFromHlsl(const FString& AssetName, const FString& AssetPath, const FNiagaraModuleAuthoringDefinition& Definition);

	/** Read a managed module definition. Refuses graphs changed outside the authoring bridge. */
	UFUNCTION(meta = (AICallable), Category = "Authoring")
	static FNiagaraModuleAuthoringDefinition GetModuleScriptDefinition(UNiagaraScript* Script);

	/** Update a managed module. An optional context must already reference it. Failed context validation restores the previous definition by default. Save requires successful context validation. */
	UFUNCTION(meta = (AICallable), Category = "Authoring")
	static UNiagaraAutoFlowAsyncAuthoringResult* UpdateModuleScriptDefinition(UNiagaraScript* Script, const FNiagaraModuleAuthoringDefinition& Definition, UNiagaraSystem* ValidationSystem = nullptr, bool bRollbackOnFailure = true, bool bSaveOnSuccess = false);

	/** Force-compile a system that actually contains this module, including GPU shaders. Optional save writes the module only after validation passes. */
	UFUNCTION(meta = (AICallable), Category = "Authoring")
	static UNiagaraAutoFlowAsyncAuthoringResult* ValidateModuleScript(UNiagaraScript* Script, UNiagaraSystem* ValidationSystem, bool bSaveOnSuccess = false);
};
