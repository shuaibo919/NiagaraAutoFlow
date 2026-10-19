#pragma once

#include "NiagaraAutoFlowToolset.h"
#include "ToolsetRegistry/ToolCallAsyncResult.h"

#include "NiagaraAutoFlowToolset_Compiled.generated.h"

class UNiagaraSystem;

UENUM(BlueprintType)
enum class ENiagaraAutoFlowSourceKind : uint8
{
	/** GpuHlsl for the GPU compute script, CpuHlsl otherwise. */
	Auto,
	CpuHlsl,
	GpuHlsl,
	VMAssembly,
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowDataInterfaceUse
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ClassName;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowScriptCompiledInfo
{
	GENERATED_BODY()

	/** Empty for system scripts; emitter spawn/update stacks are inlined into them. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ScriptUsage;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 UsageIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString CompileStatus;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bIsGPUScript = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 ByteCodeBytes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumTempRegisters = 0;

	/** Transient: 0 until RecompileSystem runs in this editor session. Same for the *Chars fields. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 LastOpCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 CpuHlslChars = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 GpuHlslChars = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 VMAssemblyChars = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumAttributes = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> AttributesWritten;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumExternalFunctionCalls = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowDataInterfaceUse> DataInterfaces;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumSimulationStages = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> ParameterCollections;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowCompileResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SystemPath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float ElapsedSeconds = 0.0f;

	/** Messages are available through the official GetSystemCompileState. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bHasErrors = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowScriptCompiledInfo> Scripts;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowSourcePage
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SourceKind;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 TotalChars = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 Offset = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Text;

	/** Continue with Offset + length of Text. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bHasMore = false;

	/** Explains why Text is empty. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Hint;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowSourceMatch
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ScriptUsage;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 UsageIndex = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SourceKind;

	/** Zero-based. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 Line = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString LineText;
};

UCLASS(BlueprintType)
class UNiagaraAutoFlowAsyncCompileResult : public UToolCallAsyncResult
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "NiagaraAutoFlow")
	bool SetValue(const FNiagaraAutoFlowCompileResult& InValue)
	{
		return MaybeBroadcastSuccessfulCompletion(FNiagaraAutoFlowCompileResult(InValue), Value);
	}

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FNiagaraAutoFlowCompileResult Value;
};

/**
 * Niagara compiled products per script: translated HLSL (CPU/GPU), VM assembly, op counts, registers, data interfaces, attribute writes.
 * HLSL, assembly and op counts are transient; call RecompileSystem once per editor session before reading them.
 * Scripts are addressed by (EmitterName, ScriptUsage, UsageIndex); EmitterName None means a system script.
 */
UCLASS(BlueprintType, Hidden)
class UNiagaraAutoFlowToolset_Compiled : public UNiagaraAutoFlowToolset
{
	GENERATED_BODY()

public:
	/**
	 * Force-recompiles a system and waits for the VM compile so HLSL / assembly / op counts become available. Does not save the asset.
	 * @param System The Niagara System.
	 */
	UFUNCTION(meta = (AICallable), Category = "Compiled")
	static UNiagaraAutoFlowAsyncCompileResult* RecompileSystem(UNiagaraSystem* System);

	/**
	 * Lists compiled info for every compilable script, system scripts first.
	 * @param System The Niagara System.
	 * @param EmitterName Restrict to one emitter; None returns all scripts.
	 */
	UFUNCTION(meta = (AICallable), Category = "Compiled")
	static TArray<FNiagaraAutoFlowScriptCompiledInfo> GetCompiledScripts(UNiagaraSystem* System, FName EmitterName);

	/**
	 * Reads one page of a script's HLSL or VM assembly.
	 * @param System The Niagara System.
	 * @param EmitterName Owning emitter; None for system scripts.
	 * @param ScriptUsage As reported by GetCompiledScripts.
	 * @param UsageIndex As reported by GetCompiledScripts.
	 * @param SourceKind Which translation to read.
	 * @param Offset Character offset.
	 * @param MaxChars Page size; 0 uses the project default.
	 */
	UFUNCTION(meta = (AICallable), Category = "Compiled")
	static FNiagaraAutoFlowSourcePage GetScriptSource(UNiagaraSystem* System, FName EmitterName, const FString& ScriptUsage, int32 UsageIndex, ENiagaraAutoFlowSourceKind SourceKind, int32 Offset, int32 MaxChars);

	/**
	 * Case-insensitive literal search over translated sources; use it to locate code before paging with GetScriptSource.
	 * @param System The Niagara System.
	 * @param EmitterName Restrict to one emitter; None searches every script.
	 * @param Pattern Literal text to find.
	 * @param SourceKind Which translation to search.
	 * @param MaxMatches Stop after this many matches.
	 */
	UFUNCTION(meta = (AICallable), Category = "Compiled")
	static TArray<FNiagaraAutoFlowSourceMatch> SearchScriptSource(UNiagaraSystem* System, FName EmitterName, const FString& Pattern, ENiagaraAutoFlowSourceKind SourceKind, int32 MaxMatches = 50);

	static FNiagaraAutoFlowScriptCompiledInfo BuildCompiledInfo(const FNiagaraAutoFlowScriptSlot& Slot);

	/** Calls OnDone once no VM compile is pending, the system is destroyed, or the timeout elapses. Error is empty on success. */
	static void RunWhenCompileComplete(UNiagaraSystem* System, TFunction<void(UNiagaraSystem*, const FString&)>&& OnDone);

private:
	/** @return null if SourceKind does not apply to the script. */
	static const FString* GetSourceText(const FNiagaraAutoFlowScriptSlot& Slot, ENiagaraAutoFlowSourceKind SourceKind, ENiagaraAutoFlowSourceKind& OutResolvedKind);
};
