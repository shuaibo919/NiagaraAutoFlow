#include "NiagaraAutoFlowToolset_Runtime.h"

#include "Containers/Ticker.h"
#include "Editor.h"
#include "NiagaraComponent.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitterInstance.h"
#include "NiagaraSimCache.h"
#include "NiagaraSystem.h"
#include "NiagaraSystemInstance.h"
#include "NiagaraSystemInstanceController.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectIterator.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraAutoFlowToolset_Runtime)

namespace UE::NiagaraAutoFlow::Private
{
	// Frames are only captured while the world ticks; an idle editor world must not hang the call.
	constexpr double CaptureTimeoutSeconds = 20.0;
}

FNiagaraAutoFlowInstanceList UNiagaraAutoFlowToolset_Runtime::ListActiveInstances(ENiagaraAutoFlowWorld World, const FString& SystemFilter, bool bIncludeInactive, int32 MaxResults)
{
	FNiagaraAutoFlowInstanceList Result;
	UWorld* TargetWorld = ResolveWorld(World);
	if (TargetWorld == nullptr)
	{
		return Result;
	}
	Result.WorldName = TargetWorld->GetName();
	Result.WorldType = TargetWorld->WorldType == EWorldType::PIE ? TEXT("PIE") : TEXT("Editor");

	TMap<FString, FNiagaraAutoFlowSystemAggregate> Aggregates;
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		UNiagaraComponent* Component = *It;
		if (Component->GetWorld() != TargetWorld || Component->IsTemplate() || !Component->IsRegistered())
		{
			continue;
		}

		const UNiagaraSystem* Asset = Component->GetAsset();
		const FString SystemPath = Asset ? Asset->GetPathName() : FString(TEXT("<none>"));
		if (!SystemFilter.IsEmpty() && !SystemPath.Contains(SystemFilter, ESearchCase::IgnoreCase))
		{
			continue;
		}

		const FNiagaraSystemInstance* Instance = GetSystemInstance(*Component);
		const bool bActive = Component->IsActive() && Instance != nullptr;
		if (!bActive && !bIncludeInactive)
		{
			continue;
		}

		FNiagaraAutoFlowInstanceInfo Info;
		Info.ComponentPath = Component->GetPathName();
		Info.OwnerName = Component->GetOwner() ? Component->GetOwner()->GetActorNameOrLabel() : FString();
		Info.SystemPath = SystemPath;
		Info.ExecutionState = EnumToString(Component->GetExecutionState());
		Info.RequestedExecutionState = EnumToString(Component->GetRequestedExecutionState());
		Info.bActive = bActive;
		Info.bPaused = Component->IsPaused();
		Info.Age = Instance ? Instance->GetAge() : 0.0f;
		Info.TotalParticles = Instance ? CountParticles(*Instance) : 0;
		Info.bManagedByScalability = Component->IsRegisteredWithScalabilityManager();
		Info.Location = Component->GetComponentLocation();

		FNiagaraAutoFlowSystemAggregate& Aggregate = Aggregates.FindOrAdd(SystemPath);
		Aggregate.SystemPath = SystemPath;
		++Aggregate.NumInstances;
		Aggregate.NumActive += bActive ? 1 : 0;
		Aggregate.TotalParticles += Info.TotalParticles;

		Result.Instances.Add(MoveTemp(Info));
	}

	Result.NumMatched = Result.Instances.Num();
	Result.Instances.Sort([](const FNiagaraAutoFlowInstanceInfo& A, const FNiagaraAutoFlowInstanceInfo& B) { return A.TotalParticles > B.TotalParticles; });
	if (MaxResults >= 0 && Result.Instances.Num() > MaxResults)
	{
		Result.Instances.SetNum(MaxResults);
	}

	Aggregates.GenerateValueArray(Result.BySystem);
	Result.BySystem.Sort([](const FNiagaraAutoFlowSystemAggregate& A, const FNiagaraAutoFlowSystemAggregate& B) { return A.TotalParticles > B.TotalParticles; });
	return Result;
}

FNiagaraAutoFlowInstanceStats UNiagaraAutoFlowToolset_Runtime::GetInstanceStats(UNiagaraComponent* Component)
{
	FNiagaraAutoFlowInstanceStats Stats;
	if (Component == nullptr)
	{
		Error(TEXT("Component was null. Pass a ComponentPath returned by ListActiveInstances."));
		return Stats;
	}

	Stats.ComponentPath = Component->GetPathName();
	Stats.SystemPath = Component->GetAsset() ? Component->GetAsset()->GetPathName() : FString();
	Stats.ExecutionState = EnumToString(Component->GetExecutionState());
	Stats.bManagedByScalability = Component->IsRegisteredWithScalabilityManager();

	const FNiagaraSystemInstance* Instance = GetSystemInstance(*Component);
	if (Instance == nullptr)
	{
		return Stats;
	}

	Stats.bHasInstance = true;
	Stats.Age = Instance->GetAge();
	Stats.TickCount = Instance->GetTickCount();
	Stats.bSolo = Instance->IsSolo();

	for (const FNiagaraEmitterInstanceRef& Emitter : Instance->GetEmitters())
	{
		FNiagaraAutoFlowEmitterRuntimeStats& EmitterStats = Stats.Emitters.AddDefaulted_GetRef();
		EmitterStats.EmitterName = Emitter->GetEmitterHandle().GetName().ToString();
		EmitterStats.SimTarget = EnumToString(Emitter->GetSimTarget());
		EmitterStats.ExecutionState = EnumToString(Emitter->GetExecutionState());
		EmitterStats.NumParticles = Emitter->GetNumParticles();
		EmitterStats.TotalSpawnedParticles = Emitter->GetTotalSpawnedParticles();
		EmitterStats.BytesUsed = Emitter->GetTotalBytesUsed();
		const FBox Bounds = Emitter->GetBounds();
		EmitterStats.BoundsMin = Bounds.IsValid ? Bounds.Min : FVector::ZeroVector;
		EmitterStats.BoundsMax = Bounds.IsValid ? Bounds.Max : FVector::ZeroVector;

		Stats.TotalParticles += EmitterStats.NumParticles;
		Stats.TotalBytesUsed += EmitterStats.BytesUsed;
	}
	return Stats;
}

UNiagaraAutoFlowAsyncParticleCapture* UNiagaraAutoFlowToolset_Runtime::CaptureParticleData(UNiagaraComponent* Component, int32 NumFrames, int32 MaxSamples, const TArray<FString>& Attributes)
{
	UNiagaraAutoFlowAsyncParticleCapture* Result = NewObject<UNiagaraAutoFlowAsyncParticleCapture>();
	if (Component == nullptr)
	{
		Result->SetError(TEXT("Component was null. Pass a ComponentPath returned by ListActiveInstances."));
		return Result;
	}
	if (GetSystemInstance(*Component) == nullptr)
	{
		Result->SetError(TEXT("Component has no live system instance; activate it or pick an active component."));
		return Result;
	}

	FNiagaraSimCacheCreateParameters CreateParameters;
	CreateParameters.bAllowRebasing = false;
	CreateParameters.bAllowDataInterfaceCaching = false;

	UNiagaraSimCache* SimCache = NewObject<UNiagaraSimCache>(GetTransientPackage());
	if (!SimCache->BeginWrite(CreateParameters, Component))
	{
		Result->SetError(TEXT("Failed to begin the simulation cache capture (see the Niagara log)."));
		return Result;
	}

	TSet<FString> RequestedAttributes;
	RequestedAttributes.Append(Attributes);
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[StrongResult = TStrongObjectPtr<UNiagaraAutoFlowAsyncParticleCapture>(Result), StrongCache = TStrongObjectPtr<UNiagaraSimCache>(SimCache), WeakComponent = TWeakObjectPtr<UNiagaraComponent>(Component),
		ComponentPath = Component->GetPathName(), TargetFrames = FMath::Max(1, NumFrames), SampleCount = FMath::Max(0, MaxSamples), AttributeFilter = MoveTemp(RequestedAttributes), StartTime = FPlatformTime::Seconds()](float) -> bool
	{
		UNiagaraComponent* LiveComponent = WeakComponent.Get();
		const bool bTimedOut = FPlatformTime::Seconds() - StartTime > UE::NiagaraAutoFlow::Private::CaptureTimeoutSeconds;
		if (LiveComponent != nullptr && StrongCache->GetNumFrames() < TargetFrames && !bTimedOut)
		{
			StrongCache->WriteFrame(LiveComponent);
			if (StrongCache->GetNumFrames() < TargetFrames)
			{
				return true;
			}
		}
		StrongCache->EndWrite();

		FNiagaraAutoFlowParticleCapture Capture;
		Capture.ComponentPath = ComponentPath;
		Capture.NumFramesCaptured = StrongCache->GetNumFrames();
		Capture.DurationSeconds = StrongCache->GetDurationSeconds();
		if (LiveComponent == nullptr)
		{
			Capture.Notes.Add(TEXT("Component was destroyed during capture."));
		}
		if (bTimedOut)
		{
			Capture.Notes.Add(TEXT("Timed out waiting for frames: the world is probably not ticking (start PIE or enable realtime in the viewport)."));
		}
		if (Capture.NumFramesCaptured == 0)
		{
			StrongResult->SetValue(Capture);
			return false;
		}

		const int32 LastFrame = Capture.NumFramesCaptured - 1;
		const TArray<FName> EmitterNames = StrongCache->GetEmitterNames();
		for (int32 EmitterIndex = 0; EmitterIndex < EmitterNames.Num(); ++EmitterIndex)
		{
			FNiagaraAutoFlowEmitterCapture& EmitterCapture = Capture.Emitters.AddDefaulted_GetRef();
			EmitterCapture.EmitterName = EmitterNames[EmitterIndex].ToString();
			for (int32 Frame = 0; Frame < Capture.NumFramesCaptured; ++Frame)
			{
				EmitterCapture.NumParticlesPerFrame.Add(StrongCache->GetEmitterNumInstances(EmitterIndex, Frame));
			}

			const int32 NumInstances = StrongCache->GetEmitterNumInstances(EmitterIndex, LastFrame);
			StrongCache->ForEachEmitterAttribute(EmitterIndex, [&](const FNiagaraSimCacheVariable& CacheVariable)
			{
				const FName AttributeName = CacheVariable.Variable.GetName();
				const FString AttributeString = NormalizeAttributeName(AttributeName.ToString());
				if (AttributeFilter.Num() > 0 && !AttributeFilter.Contains(AttributeString))
				{
					return true;
				}

				TArray<float> Floats;
				TArray<FFloat16> Halfs;
				TArray<int32> Ints;
				StrongCache->ReadAttribute(Floats, Halfs, Ints, AttributeName, EmitterNames[EmitterIndex], LastFrame);

				// ReadAttribute lays out each component as a block of NumInstances values: floats, then halfs, then ints.
				TArray<TFunction<double(int32)>> Components;
				for (int32 Comp = 0; Comp < CacheVariable.FloatCount; ++Comp)
				{
					Components.Add([&Floats, Comp, NumInstances](int32 Particle) { return static_cast<double>(Floats[Comp * NumInstances + Particle]); });
				}
				for (int32 Comp = 0; Comp < CacheVariable.HalfCount; ++Comp)
				{
					Components.Add([&Halfs, Comp, NumInstances](int32 Particle) { return static_cast<double>(Halfs[Comp * NumInstances + Particle].GetFloat()); });
				}
				for (int32 Comp = 0; Comp < CacheVariable.Int32Count; ++Comp)
				{
					Components.Add([&Ints, Comp, NumInstances](int32 Particle) { return static_cast<double>(Ints[Comp * NumInstances + Particle]); });
				}

				FNiagaraAutoFlowAttributeSample& Sample = EmitterCapture.Attributes.AddDefaulted_GetRef();
				Sample.Name = AttributeString;
				Sample.TypeName = CacheVariable.Variable.GetType().GetName();
				if (NumInstances <= 0 || Components.Num() == 0 || Floats.Num() + Halfs.Num() + Ints.Num() < Components.Num() * NumInstances)
				{
					return true;
				}

				for (const TFunction<double(int32)>& Read : Components)
				{
					double MinValue = TNumericLimits<double>::Max();
					double MaxValue = TNumericLimits<double>::Lowest();
					double Sum = 0.0;
					for (int32 Particle = 0; Particle < NumInstances; ++Particle)
					{
						const double Value = Read(Particle);
						MinValue = FMath::Min(MinValue, Value);
						MaxValue = FMath::Max(MaxValue, Value);
						Sum += Value;
					}
					Sample.Min.Add(MinValue);
					Sample.Max.Add(MaxValue);
					Sample.Mean.Add(Sum / NumInstances);
				}

				for (int32 Particle = 0; Particle < FMath::Min(SampleCount, NumInstances); ++Particle)
				{
					TArray<FString> Values;
					for (const TFunction<double(int32)>& Read : Components)
					{
						Values.Add(FString::SanitizeFloat(Read(Particle)));
					}
					Sample.Samples.Add(FString::Join(Values, TEXT(",")));
				}
				return true;
			});
		}

		StrongResult->SetValue(Capture);
		return false;
	}));

	return Result;
}

UWorld* UNiagaraAutoFlowToolset_Runtime::ResolveWorld(ENiagaraAutoFlowWorld World)
{
	if (GEditor == nullptr)
	{
		Error(TEXT("No editor instance."));
		return nullptr;
	}

	UWorld* PIEWorld = GEditor->PlayWorld;
	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	UWorld* Selected = World == ENiagaraAutoFlowWorld::PIE ? PIEWorld : (World == ENiagaraAutoFlowWorld::Editor ? EditorWorld : (PIEWorld ? PIEWorld : EditorWorld));
	if (Selected == nullptr)
	{
		Error(World == ENiagaraAutoFlowWorld::PIE ? TEXT("No PIE session is running.") : TEXT("No editor world is available."));
	}
	return Selected;
}

FNiagaraSystemInstance* UNiagaraAutoFlowToolset_Runtime::GetSystemInstance(const UNiagaraComponent& Component)
{
	FNiagaraSystemInstanceControllerConstPtr Controller = Component.GetSystemInstanceController();
	return Controller.IsValid() && Controller->IsValid() ? Controller->GetSystemInstance_Unsafe() : nullptr;
}

int32 UNiagaraAutoFlowToolset_Runtime::CountParticles(const FNiagaraSystemInstance& Instance)
{
	int32 Total = 0;
	for (const FNiagaraEmitterInstanceRef& Emitter : Instance.GetEmitters())
	{
		Total += Emitter->GetNumParticles();
	}
	return Total;
}
