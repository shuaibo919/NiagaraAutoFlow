#pragma once

#include "NiagaraAutoFlowToolset.h"
#include "ToolsetRegistry/ToolCallAsyncResult.h"

#include "NiagaraAutoFlowToolset_Runtime.generated.h"

class FNiagaraSystemInstance;
class UNiagaraComponent;
class UWorld;

UENUM(BlueprintType)
enum class ENiagaraAutoFlowWorld : uint8
{
	/** The PIE world if a play session is running, otherwise the editor world. */
	Auto,
	Editor,
	PIE,
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowInstanceInfo
{
	GENERATED_BODY()

	/** Pass to GetInstanceStats / CaptureParticleData. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ComponentPath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString OwnerName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SystemPath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ExecutionState;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString RequestedExecutionState;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bActive = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bPaused = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float Age = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 TotalParticles = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bManagedByScalability = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FVector Location = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowSystemAggregate
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SystemPath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumInstances = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumActive = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 TotalParticles = 0;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowInstanceList
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString WorldName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString WorldType;

	/** Before MaxResults truncation. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumMatched = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowInstanceInfo> Instances;

	/** Covers every match regardless of MaxResults. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowSystemAggregate> BySystem;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowEmitterRuntimeStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SimTarget;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ExecutionState;

	/** GPU counts come from the last readback and may lag a frame. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumParticles = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 TotalSpawnedParticles = 0;

	/** CPU-side memory only. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int64 BytesUsed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FVector BoundsMin = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FVector BoundsMax = FVector::ZeroVector;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowInstanceStats
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ComponentPath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString SystemPath;

	/** False when inactive, culled or not yet activated. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bHasInstance = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ExecutionState;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float Age = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 TickCount = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bSolo = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	bool bManagedByScalability = false;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 TotalParticles = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int64 TotalBytesUsed = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowEmitterRuntimeStats> Emitters;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowAttributeSample
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString TypeName;

	/** Per component, over every particle of the last captured frame. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<double> Min;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<double> Max;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<double> Mean;

	/** First particles of the last frame, components comma separated. */
	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> Samples;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowEmitterCapture
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString EmitterName;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<int32> NumParticlesPerFrame;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowAttributeSample> Attributes;
};

USTRUCT(BlueprintType)
struct FNiagaraAutoFlowParticleCapture
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FString ComponentPath;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	int32 NumFramesCaptured = 0;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	float DurationSeconds = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FNiagaraAutoFlowEmitterCapture> Emitters;

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	TArray<FString> Notes;
};

UCLASS(BlueprintType)
class UNiagaraAutoFlowAsyncParticleCapture : public UToolCallAsyncResult
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "NiagaraAutoFlow")
	bool SetValue(const FNiagaraAutoFlowParticleCapture& InValue)
	{
		return MaybeBroadcastSuccessfulCompletion(FNiagaraAutoFlowParticleCapture(InValue), Value);
	}

	UPROPERTY(BlueprintReadOnly, Category = "NiagaraAutoFlow")
	FNiagaraAutoFlowParticleCapture Value;
};

/** Live Niagara data from the editor or PIE world. Counts and captures only change while the world ticks (PIE or realtime viewport). */
UCLASS(BlueprintType, Hidden)
class UNiagaraAutoFlowToolset_Runtime : public UNiagaraAutoFlowToolset
{
	GENERATED_BODY()

public:
	/**
	 * Lists Niagara components with execution state and particle counts, plus a per-system aggregate.
	 * @param World Which world to inspect.
	 * @param SystemFilter Case-insensitive substring of the system path; empty matches all.
	 * @param bIncludeInactive Also list components without an active simulation.
	 * @param MaxResults Maximum entries in Instances.
	 */
	UFUNCTION(meta = (AICallable), Category = "Runtime")
	static FNiagaraAutoFlowInstanceList ListActiveInstances(ENiagaraAutoFlowWorld World, const FString& SystemFilter, bool bIncludeInactive = false, int32 MaxResults = 100);

	/**
	 * Returns per-emitter execution state, particle counts, memory and bounds of one component.
	 * @param Component ComponentPath from ListActiveInstances.
	 */
	UFUNCTION(meta = (AICallable), Category = "Runtime")
	static FNiagaraAutoFlowInstanceStats GetInstanceStats(UNiagaraComponent* Component);

	/**
	 * Captures particle attributes over the next frames (CPU and GPU) and returns per-attribute min/max/mean plus sample particles.
	 * @param Component ComponentPath from ListActiveInstances.
	 * @param NumFrames Frames to capture.
	 * @param MaxSamples Particles listed per attribute.
	 * @param Attributes Attribute names to include, e.g. Position; empty includes all.
	 */
	UFUNCTION(meta = (AICallable), Category = "Runtime")
	static UNiagaraAutoFlowAsyncParticleCapture* CaptureParticleData(UNiagaraComponent* Component, int32 NumFrames, int32 MaxSamples, const TArray<FString>& Attributes);

private:
	static UWorld* ResolveWorld(ENiagaraAutoFlowWorld World);

	/** Only safe on the game thread between ticks. */
	static FNiagaraSystemInstance* GetSystemInstance(const UNiagaraComponent& Component);

	static int32 CountParticles(const FNiagaraSystemInstance& Instance);
};
