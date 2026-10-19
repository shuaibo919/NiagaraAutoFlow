#pragma once

#include "NiagaraAutoFlowToolset.h"
#include "ToolsetRegistry/ToolCallAsyncResult.h"

#include "NiagaraAutoFlowToolset_Audit.generated.h"

class UNiagaraSystem;

UENUM(BlueprintType)
enum class ENiagaraAutoFlowSeverity : uint8
{
	Info,
	Low,
	Medium,
	High,
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAuditRule
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString RuleId;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	ENiagaraAutoFlowSeverity Severity = ENiagaraAutoFlowSeverity::Info;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Description;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAuditFinding
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString RuleId;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	ENiagaraAutoFlowSeverity Severity = ENiagaraAutoFlowSeverity::Info;

	/** Empty for system-level findings. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterName;

	/** -1 unless the finding targets a renderer. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 RendererIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Message;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Suggestion;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAuditReport
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SystemPath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumHigh = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumMedium = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumLow = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumInfo = 0;

	/** Highest severity first. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowAuditFinding> Findings;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAuditSummary
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SystemPath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumHigh = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumMedium = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumLow = 0;

	/** Rule ids of High and Medium findings; details are in the report file. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> TopRuleIds;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowBatchAuditResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString PackagePath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumAssetsFound = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumAudited = 0;

	/** Assets beyond MaxAssets. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumSkipped = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> FailedToLoad;

	/** Absolute path of the JSON file with every full report. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ReportFile;

	/** Systems with findings, worst first. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowAuditSummary> Systems;
};

USTRUCT()
struct FNiagaraAutoFlowAuditReportFile
{
	GENERATED_BODY()

	UPROPERTY()
	FString PackagePath;

	UPROPERTY()
	FString CreatedUtc;

	UPROPERTY()
	TArray<FNiagaraAutoFlowAuditRule> Rules;

	UPROPERTY()
	TArray<FNiagaraAutoFlowAuditReport> Reports;
};

UCLASS(BlueprintType)
class UNiagaraAutoFlowAsyncBatchAuditResult : public UToolCallAsyncResult
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "NiagaraAutoFlow")
	bool SetValue(const FNiagaraAutoFlowBatchAuditResult& InValue)
	{
		return MaybeBroadcastSuccessfulCompletion(FNiagaraAutoFlowBatchAuditResult(InValue), Value);
	}

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FNiagaraAutoFlowBatchAuditResult Value;
};

/**
 * Rule-based static audit of Niagara Systems (GPU/CPU path, bounds, overdraw, scalability, payload, attribute flow, compile state).
 * Thresholds live in Project Settings > Plugins > Niagara AutoFlow. Fix with the official NiagaraToolset_System edit tools, then re-audit.
 */
UCLASS(BlueprintType, Hidden)
class UNiagaraAutoFlowToolset_Audit : public UNiagaraAutoFlowToolset
{
	GENERATED_BODY()

public:
	/** Lists every audit rule with its id, severity and description. */
	UFUNCTION(meta = (AICallable), Category = "Audit")
	static TArray<FNiagaraAutoFlowAuditRule> GetAuditRules();

	/**
	 * Audits one Niagara System against every rule.
	 * @param System The Niagara System.
	 * @param MinSeverity Drop findings below this severity.
	 */
	UFUNCTION(meta = (AICallable), Category = "Audit")
	static FNiagaraAutoFlowAuditReport AuditSystem(UNiagaraSystem* System, ENiagaraAutoFlowSeverity MinSeverity = ENiagaraAutoFlowSeverity::Info);

	/**
	 * Audits every Niagara System under a content path across editor ticks; writes full reports to JSON and returns a summary.
	 * @param PackagePath Content path such as /Game/FX.
	 * @param bRecursive Include sub-folders.
	 * @param MaxAssets Audit at most this many systems (alphabetical).
	 * @param MinSeverity Drop findings below this severity.
	 */
	UFUNCTION(meta = (AICallable), Category = "Audit")
	static UNiagaraAutoFlowAsyncBatchAuditResult* AuditAssets(const FString& PackagePath, bool bRecursive = true, int32 MaxAssets = 500, ENiagaraAutoFlowSeverity MinSeverity = ENiagaraAutoFlowSeverity::Low);

	static FNiagaraAutoFlowAuditReport RunAudit(UNiagaraSystem& System, ENiagaraAutoFlowSeverity MinSeverity);

private:
	static FString WriteReportFile(const FNiagaraAutoFlowAuditReportFile& File);
};
