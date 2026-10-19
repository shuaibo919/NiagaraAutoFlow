#pragma once

#include "Engine/DeveloperSettings.h"

#include "NiagaraAutoFlowSettings.generated.h"

UCLASS(config = NiagaraAutoFlow, defaultconfig, meta = (DisplayName = "Niagara AutoFlow"))
class UNiagaraAutoFlowSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	UPROPERTY(config, EditAnywhere, Category = "Compile", meta = (ClampMin = "1"))
	float CompileWaitTimeoutSeconds = 120.0f;

	/** Characters per page when reading HLSL / VM assembly. */
	UPROPERTY(config, EditAnywhere, Category = "Compile", meta = (ClampMin = "1000"))
	int32 DefaultSourcePageChars = 20000;

	/** CPU emitters pre-allocating more particles than this are flagged as GPU candidates. */
	UPROPERTY(config, EditAnywhere, Category = "Audit", meta = (ClampMin = "1"))
	int32 CpuParticleCountThreshold = 1000;

	UPROPERTY(config, EditAnywhere, Category = "Audit", meta = (ClampMin = "1"))
	int32 MaxBytesPerParticle = 128;

	UPROPERTY(config, EditAnywhere, Category = "Audit", meta = (ClampMin = "0"))
	float MaxWarmupSeconds = 2.0f;

	/** Spawn-count scale at or above this at the lowest effect quality is flagged. */
	UPROPERTY(config, EditAnywhere, Category = "Audit", meta = (ClampMin = "0"))
	float LowQualityMaxSpawnCountScale = 0.5f;

	UPROPERTY(config, EditAnywhere, Category = "Audit", meta = (ClampMin = "1"))
	int32 MaxSimStageIterations = 8;

	/** Assets loaded and audited per editor tick in batch audits. */
	UPROPERTY(config, EditAnywhere, Category = "Audit", meta = (ClampMin = "1"))
	int32 BatchAssetsPerTick = 4;

	/** Relative paths resolve against the project Saved directory. */
	UPROPERTY(config, EditAnywhere, Category = "Audit")
	FString ReportDirectory = TEXT("NiagaraAutoFlow");
};
