#include "NiagaraAutoFlowToolset_Audit.h"

#include "Algo/Find.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Containers/Ticker.h"
#include "JsonObjectConverter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "NiagaraAutoFlowModule.h"
#include "NiagaraAutoFlowSettings.h"
#include "NiagaraAutoFlowToolset_Attributes.h"
#include "NiagaraAutoFlowToolset_Scalability.h"
#include "NiagaraAutoFlowToolset_Topology.h"
#include "NiagaraEffectType.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraGPUSortInfo.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "UObject/StrongObjectPtr.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraAutoFlowToolset_Audit)

namespace UE::NiagaraAutoFlow::Private
{
	struct FAuditRuleDef
	{
		const TCHAR* Id;
		ENiagaraAutoFlowSeverity Severity;
		const TCHAR* Description;
	};

	const FAuditRuleDef AuditRules[] =
	{
		{ TEXT("NAF001"), ENiagaraAutoFlowSeverity::High, TEXT("System has no Effect Type: no distance/instance culling or significance handling.") },
		{ TEXT("NAF002"), ENiagaraAutoFlowSeverity::Medium, TEXT("Distance culling is off at one or more effect quality levels.") },
		{ TEXT("NAF003"), ENiagaraAutoFlowSeverity::Medium, TEXT("Neither effect-type nor per-system instance cap is set at one or more quality levels.") },
		{ TEXT("NAF004"), ENiagaraAutoFlowSeverity::Low, TEXT("Emitter spawns at or above LowQualityMaxSpawnCountScale at the lowest quality level.") },
		{ TEXT("NAF005"), ENiagaraAutoFlowSeverity::High, TEXT("A script failed to compile.") },
		{ TEXT("NAF006"), ENiagaraAutoFlowSeverity::Info, TEXT("A script is not compiled (dirty/unknown); compiled-data rules are unreliable.") },
		{ TEXT("NAF007"), ENiagaraAutoFlowSeverity::Medium, TEXT("CPU emitter pre-allocates more than CpuParticleCountThreshold particles; GPU candidate.") },
		{ TEXT("NAF008"), ENiagaraAutoFlowSeverity::High, TEXT("GPU emitter has event handlers; Niagara events are CPU only.") },
		{ TEXT("NAF009"), ENiagaraAutoFlowSeverity::Low, TEXT("GPU emitter requires persistent IDs (extra ID table cost).") },
		{ TEXT("NAF010"), ENiagaraAutoFlowSeverity::Info, TEXT("GPU emitter simulates in local space; confirm it must follow the owner.") },
		{ TEXT("NAF011"), ENiagaraAutoFlowSeverity::High, TEXT("GPU emitter uses dynamic bounds without system fixed bounds; GPU sims cannot compute dynamic bounds.") },
		{ TEXT("NAF012"), ENiagaraAutoFlowSeverity::Low, TEXT("CPU emitter recomputes dynamic bounds every frame and the system has no fixed bounds.") },
		{ TEXT("NAF013"), ENiagaraAutoFlowSeverity::Low, TEXT("Translucent sprite/mesh renderer without sorting: order artifacts and overdraw risk.") },
		{ TEXT("NAF014"), ENiagaraAutoFlowSeverity::High, TEXT("Enabled renderer has no material.") },
		{ TEXT("NAF015"), ENiagaraAutoFlowSeverity::Low, TEXT("Translucent renderers in different emitters share material and SortOrderHint; draw order is undefined.") },
		{ TEXT("NAF016"), ENiagaraAutoFlowSeverity::Low, TEXT("Particle payload exceeds MaxBytesPerParticle.") },
		{ TEXT("NAF017"), ENiagaraAutoFlowSeverity::Info, TEXT("Attributes are written but bound by no renderer.") },
		{ TEXT("NAF018"), ENiagaraAutoFlowSeverity::Medium, TEXT("Renderer binds attributes missing from the data layout; it silently uses defaults.") },
		{ TEXT("NAF019"), ENiagaraAutoFlowSeverity::Medium, TEXT("Warmup time exceeds MaxWarmupSeconds (cost paid on every activation).") },
		{ TEXT("NAF020"), ENiagaraAutoFlowSeverity::Low, TEXT("Simulation stage iterates more than MaxSimStageIterations times per frame.") },
		{ TEXT("NAF021"), ENiagaraAutoFlowSeverity::Low, TEXT("Trim Attributes is off; unused attributes stay in the particle payload.") },
		{ TEXT("NAF022"), ENiagaraAutoFlowSeverity::Info, TEXT("Require Current Frame Data is on; disable for static or loosely attached systems.") },
		{ TEXT("NAF023"), ENiagaraAutoFlowSeverity::Info, TEXT("Emitter is disabled but still part of the asset.") },
		{ TEXT("NAF024"), ENiagaraAutoFlowSeverity::Info, TEXT("Effect type updates scalability continuously (most expensive frequency).") },
	};
}

TArray<FNiagaraAutoFlowAuditRule> UNiagaraAutoFlowToolset_Audit::GetAuditRules()
{
	TArray<FNiagaraAutoFlowAuditRule> Rules;
	for (const UE::NiagaraAutoFlow::Private::FAuditRuleDef& Def : UE::NiagaraAutoFlow::Private::AuditRules)
	{
		FNiagaraAutoFlowAuditRule& Rule = Rules.AddDefaulted_GetRef();
		Rule.RuleId = Def.Id;
		Rule.Severity = Def.Severity;
		Rule.Description = Def.Description;
	}
	return Rules;
}

FNiagaraAutoFlowAuditReport UNiagaraAutoFlowToolset_Audit::AuditSystem(UNiagaraSystem* System, ENiagaraAutoFlowSeverity MinSeverity)
{
	if (!ValidateSystem(System))
	{
		return FNiagaraAutoFlowAuditReport();
	}
	return RunAudit(*System, MinSeverity);
}

UNiagaraAutoFlowAsyncBatchAuditResult* UNiagaraAutoFlowToolset_Audit::AuditAssets(const FString& PackagePath, bool bRecursive, int32 MaxAssets, ENiagaraAutoFlowSeverity MinSeverity)
{
	UNiagaraAutoFlowAsyncBatchAuditResult* Result = NewObject<UNiagaraAutoFlowAsyncBatchAuditResult>();
	if (!PackagePath.StartsWith(TEXT("/")))
	{
		Result->SetError(FString::Printf(TEXT("PackagePath '%s' must be a content path such as /Game/FX."), *PackagePath));
		return Result;
	}

	FARFilter Filter;
	Filter.PackagePaths.Add(FName(*PackagePath));
	Filter.bRecursivePaths = bRecursive;
	Filter.ClassPaths.Add(UNiagaraSystem::StaticClass()->GetClassPathName());

	TArray<FAssetData> Assets;
	IAssetRegistry::GetChecked().GetAssets(Filter, Assets);
	Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString(); });

	struct FBatchState
	{
		TArray<FAssetData> Assets;
		int32 NextIndex = 0;
		ENiagaraAutoFlowSeverity MinSeverity = ENiagaraAutoFlowSeverity::Low;
		FNiagaraAutoFlowBatchAuditResult Summary;
		FNiagaraAutoFlowAuditReportFile File;
	};
	TSharedRef<FBatchState> State = MakeShared<FBatchState>();
	State->Summary.PackagePath = PackagePath;
	State->Summary.NumAssetsFound = Assets.Num();
	State->Summary.NumSkipped = FMath::Max(0, Assets.Num() - MaxAssets);
	State->MinSeverity = MinSeverity;
	State->File.PackagePath = PackagePath;
	State->File.CreatedUtc = FDateTime::UtcNow().ToIso8601();
	State->File.Rules = GetAuditRules();
	Assets.SetNum(FMath::Min(Assets.Num(), FMath::Max(0, MaxAssets)));
	State->Assets = MoveTemp(Assets);

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([State, StrongResult = TStrongObjectPtr<UNiagaraAutoFlowAsyncBatchAuditResult>(Result)](float) -> bool
	{
		const int32 PerTick = GetDefault<UNiagaraAutoFlowSettings>()->BatchAssetsPerTick;
		for (int32 Processed = 0; Processed < PerTick && State->NextIndex < State->Assets.Num(); ++Processed)
		{
			const FAssetData& Asset = State->Assets[State->NextIndex++];
			UNiagaraSystem* System = Cast<UNiagaraSystem>(Asset.GetAsset());
			if (System == nullptr)
			{
				State->Summary.FailedToLoad.Add(Asset.GetSoftObjectPath().ToString());
				continue;
			}

			if (System->HasOutstandingCompilationRequests(/*bIncludingGPUShaders*/ false))
			{
				System->WaitForCompilationComplete(/*bIncludingGPUShaders*/ false, /*bShowProgress*/ false);
			}

			FNiagaraAutoFlowAuditReport Report = RunAudit(*System, State->MinSeverity);
			++State->Summary.NumAudited;
			if (Report.Findings.Num() == 0)
			{
				continue;
			}

			FNiagaraAutoFlowAuditSummary& Summary = State->Summary.Systems.AddDefaulted_GetRef();
			Summary.SystemPath = Report.SystemPath;
			Summary.NumHigh = Report.NumHigh;
			Summary.NumMedium = Report.NumMedium;
			Summary.NumLow = Report.NumLow;
			for (const FNiagaraAutoFlowAuditFinding& Finding : Report.Findings)
			{
				if (Finding.Severity >= ENiagaraAutoFlowSeverity::Medium)
				{
					Summary.TopRuleIds.AddUnique(Finding.RuleId);
				}
			}
			State->File.Reports.Add(MoveTemp(Report));
		}

		if (State->NextIndex < State->Assets.Num())
		{
			return true;
		}

		State->Summary.Systems.StableSort([](const FNiagaraAutoFlowAuditSummary& A, const FNiagaraAutoFlowAuditSummary& B)
		{
			return A.NumHigh != B.NumHigh ? A.NumHigh > B.NumHigh : (A.NumMedium != B.NumMedium ? A.NumMedium > B.NumMedium : A.NumLow > B.NumLow);
		});
		State->Summary.ReportFile = WriteReportFile(State->File);
		StrongResult->SetValue(State->Summary);
		return false;
	}));

	return Result;
}

FNiagaraAutoFlowAuditReport UNiagaraAutoFlowToolset_Audit::RunAudit(UNiagaraSystem& System, ENiagaraAutoFlowSeverity MinSeverity)
{
	using namespace UE::NiagaraAutoFlow::Private;
	const UNiagaraAutoFlowSettings* Settings = GetDefault<UNiagaraAutoFlowSettings>();

	FNiagaraAutoFlowAuditReport Report;
	Report.SystemPath = System.GetPathName();

	auto AddFinding = [&Report, MinSeverity](const TCHAR* RuleId, const FString& EmitterName, int32 RendererIndex, const FString& Message, const FString& Suggestion)
	{
		const FAuditRuleDef* Def = Algo::FindByPredicate(AuditRules, [RuleId](const FAuditRuleDef& Rule) { return FCString::Strcmp(Rule.Id, RuleId) == 0; });
		check(Def);
		if (Def->Severity < MinSeverity)
		{
			return;
		}
		FNiagaraAutoFlowAuditFinding& Finding = Report.Findings.AddDefaulted_GetRef();
		Finding.RuleId = RuleId;
		Finding.Severity = Def->Severity;
		Finding.EmitterName = EmitterName;
		Finding.RendererIndex = RendererIndex;
		Finding.Message = Message;
		Finding.Suggestion = Suggestion;
	};

	const UNiagaraEffectType* EffectType = System.GetEffectType();
	if (EffectType == nullptr)
	{
		AddFinding(TEXT("NAF001"), FString(), INDEX_NONE, TEXT("No Effect Type assigned."), TEXT("Assign the project effect type matching gameplay/environment and burst/looping usage."));
	}
	else
	{
		TArray<FString> NoDistanceLevels;
		TArray<FString> NoCapLevels;
		for (int32 QualityLevel = 0; QualityLevel < GetNumQualityLevels(); ++QualityLevel)
		{
			if (!System.GetScalabilityPlatformSet().IsEffectQualityEnabled(QualityLevel))
			{
				continue;
			}
			const FNiagaraAutoFlowEffectiveScalability Effective = UNiagaraAutoFlowToolset_Scalability::ResolveEffectiveScalability(System, QualityLevel);
			if (!Effective.System.bCullByDistance)
			{
				NoDistanceLevels.Add(Effective.QualityLevelName);
			}
			if (!Effective.System.bCullMaxInstanceCount && !Effective.System.bCullPerSystemMaxInstanceCount)
			{
				NoCapLevels.Add(Effective.QualityLevelName);
			}
		}
		if (NoDistanceLevels.Num() > 0)
		{
			AddFinding(TEXT("NAF002"), FString(), INDEX_NONE, FString::Printf(TEXT("No distance culling at quality levels: %s."), *FString::Join(NoDistanceLevels, TEXT(", "))), TEXT("Enable Cull By Distance in the effect type or a system scalability override."));
		}
		if (NoCapLevels.Num() > 0)
		{
			AddFinding(TEXT("NAF003"), FString(), INDEX_NONE, FString::Printf(TEXT("No instance cap at quality levels: %s."), *FString::Join(NoCapLevels, TEXT(", "))), TEXT("Set Max Instances / Max System Instances in the effect type or a system override."));
		}
		if (EffectType->UpdateFrequency == ENiagaraScalabilityUpdateFrequency::Continuous)
		{
			AddFinding(TEXT("NAF024"), FString(), INDEX_NONE, FString::Printf(TEXT("Effect type %s uses Continuous update frequency."), *EffectType->GetName()), TEXT("Use SpawnOnly or Low unless the effect must react to distance changes every frame."));
		}
	}

	if (System.GetWarmupTime() > Settings->MaxWarmupSeconds)
	{
		AddFinding(TEXT("NAF019"), FString(), INDEX_NONE, FString::Printf(TEXT("Warmup is %.2fs."), System.GetWarmupTime()), TEXT("Shorten warmup or bake the warmed-up state."));
	}
	if (!System.ShouldTrimAttributes())
	{
		AddFinding(TEXT("NAF021"), FString(), INDEX_NONE, TEXT("Trim Attributes is disabled."), TEXT("Enable Trim Attributes in the system properties."));
	}
	if (System.bRequireCurrentFrameData)
	{
		AddFinding(TEXT("NAF022"), FString(), INDEX_NONE, TEXT("Require Current Frame Data is enabled."), TEXT("Disable for systems that are static or loosely attached."));
	}

	bool bReportedNotCompiled = false;
	TArray<FNiagaraAutoFlowScriptSlot> Slots;
	GatherScriptSlots(System, Slots);
	for (const FNiagaraAutoFlowScriptSlot& Slot : Slots)
	{
		const ENiagaraScriptCompileStatus Status = Slot.Script->GetVMExecutableData().LastCompileStatus;
		if (Status == ENiagaraScriptCompileStatus::NCS_Error)
		{
			AddFinding(TEXT("NAF005"), Slot.EmitterName, INDEX_NONE, FString::Printf(TEXT("%s failed to compile."), *Slot.ScriptUsage), TEXT("Read the errors with the official GetSystemCompileState / GetStackIssues tools."));
		}
		else if (!bReportedNotCompiled && (Status == ENiagaraScriptCompileStatus::NCS_Dirty || Status == ENiagaraScriptCompileStatus::NCS_Unknown || Status == ENiagaraScriptCompileStatus::NCS_BeingCreated))
		{
			bReportedNotCompiled = true;
			AddFinding(TEXT("NAF006"), Slot.EmitterName, INDEX_NONE, FString::Printf(TEXT("%s is %s."), *Slot.ScriptUsage, *EnumToString(Status)), TEXT("Compile the system (RecompileSystem) and re-run the audit."));
		}
	}

	TMap<FString, TArray<FString>> TranslucentSortKeys;
	for (int32 HandleIndex = 0; HandleIndex < System.GetEmitterHandles().Num(); ++HandleIndex)
	{
		const FNiagaraEmitterHandle& Handle = System.GetEmitterHandles()[HandleIndex];
		const FString EmitterName = Handle.GetName().ToString();
		if (!Handle.GetIsEnabled())
		{
			AddFinding(TEXT("NAF023"), EmitterName, INDEX_NONE, TEXT("Emitter is disabled."), TEXT("Remove it if it is no longer needed."));
			continue;
		}

		const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterMode() == ENiagaraEmitterMode::Standard ? Handle.GetEmitterData() : nullptr;
		if (EmitterData == nullptr)
		{
			continue;
		}

		const bool bGPU = EmitterData->SimTarget == ENiagaraSimTarget::GPUComputeSim;
		const bool bDynamicBounds = !System.bFixedBounds && EmitterData->CalculateBoundsMode == ENiagaraEmitterCalculateBoundMode::Dynamic;
		if (bGPU)
		{
			if (EmitterData->GetEventHandlers().Num() > 0)
			{
				AddFinding(TEXT("NAF008"), EmitterName, INDEX_NONE, FString::Printf(TEXT("%d event handler(s) on a GPU emitter."), EmitterData->GetEventHandlers().Num()), TEXT("Move event-driven logic to a CPU emitter or use Particle Attribute Reader / Data Channels."));
			}
			if (EmitterData->bRequiresPersistentIDs)
			{
				AddFinding(TEXT("NAF009"), EmitterName, INDEX_NONE, TEXT("Requires persistent IDs."), TEXT("Disable unless ribbons, attribute readers or IDs require it."));
			}
			if (EmitterData->bLocalSpace)
			{
				AddFinding(TEXT("NAF010"), EmitterName, INDEX_NONE, TEXT("GPU emitter in local space."), TEXT("Use world space unless particles must rigidly follow the owner."));
			}
			if (bDynamicBounds)
			{
				AddFinding(TEXT("NAF011"), EmitterName, INDEX_NONE, TEXT("GPU emitter with dynamic bounds and no system fixed bounds."), TEXT("Set fixed or programmable emitter bounds, or enable system fixed bounds."));
			}
		}
		else
		{
			const int32 PreAllocation = EmitterData->AllocationMode == EParticleAllocationMode::AutomaticEstimate ? 0 : EmitterData->PreAllocationCount;
			if (PreAllocation > Settings->CpuParticleCountThreshold)
			{
				AddFinding(TEXT("NAF007"), EmitterName, INDEX_NONE, FString::Printf(TEXT("CPU emitter pre-allocates %d particles."), PreAllocation), TEXT("Move to GPUComputeSim if no CPU-only feature (events, CPU DIs) is needed, or lower the count."));
			}
			if (bDynamicBounds)
			{
				AddFinding(TEXT("NAF012"), EmitterName, INDEX_NONE, TEXT("Dynamic bounds recomputed every frame."), TEXT("Use fixed bounds sized to the effect."));
			}
		}

		const FNiagaraAutoFlowEmitterProfile Profile = UNiagaraAutoFlowToolset_Topology::BuildEmitterProfile(System, HandleIndex);
		for (const FNiagaraAutoFlowRendererInfo& Renderer : Profile.Renderers)
		{
			if (!Renderer.bEnabled)
			{
				continue;
			}

			bool bTranslucent = false;
			for (const FNiagaraAutoFlowMaterialUse& Material : Renderer.Materials)
			{
				if (Material.Path == TEXT("<none>"))
				{
					AddFinding(TEXT("NAF014"), EmitterName, Renderer.Index, FString::Printf(TEXT("%s has no material."), *Renderer.ClassName), TEXT("Assign a material instance with the matching Niagara usage flag."));
				}
				else if (Material.bTranslucent)
				{
					bTranslucent = true;
					TranslucentSortKeys.FindOrAdd(FString::Printf(TEXT("%s|%d"), *Material.Path, Renderer.SortOrderHint)).AddUnique(EmitterName);
				}
			}

			if (bTranslucent && Renderer.SortMode == EnumToString(ENiagaraSortMode::None))
			{
				AddFinding(TEXT("NAF013"), EmitterName, Renderer.Index, FString::Printf(TEXT("%s: translucent material with SortMode None."), *Renderer.ClassName), TEXT("Use ViewDepth/ViewDistance sorting, or a masked material if partial transparency is not needed."));
			}
		}

		const FNiagaraAutoFlowAttributeSet AttributeSet = UNiagaraAutoFlowToolset_Attributes::BuildAttributeSet(System, HandleIndex);
		if (AttributeSet.bCompiledDataValid)
		{
			if (AttributeSet.BytesPerParticle > Settings->MaxBytesPerParticle)
			{
				AddFinding(TEXT("NAF016"), EmitterName, INDEX_NONE, FString::Printf(TEXT("%d bytes per particle (%d attributes)."), AttributeSet.BytesPerParticle, AttributeSet.Attributes.Num()), TEXT("Inspect GetAttributeFlow and drop or pack unused attributes."));
			}

			const FNiagaraAutoFlowAttributeFlow Flow = UNiagaraAutoFlowToolset_Attributes::BuildAttributeFlow(System, HandleIndex);
			if (Flow.WrittenButNotRendered.Num() > 0)
			{
				AddFinding(TEXT("NAF017"), EmitterName, INDEX_NONE, FString::Printf(TEXT("Written but not rendered: %s."), *FString::Join(Flow.WrittenButNotRendered, TEXT(", "))), TEXT("Confirm no module reads them (SearchScriptSource) before removing the writes."));
			}
			if (Flow.RenderedButMissing.Num() > 0)
			{
				AddFinding(TEXT("NAF018"), EmitterName, INDEX_NONE, FString::Printf(TEXT("Bound but missing: %s."), *FString::Join(Flow.RenderedButMissing, TEXT(", "))), TEXT("Write the attribute in a particle script or fix the renderer binding."));
			}
		}

		if (bGPU)
		{
			for (const FNiagaraAutoFlowSimStageInfo& Stage : UNiagaraAutoFlowToolset_Topology::BuildSimulationStages(*EmitterData))
			{
				if (Stage.bEnabled && Stage.bHasCompiledMetaData && Stage.NumIterationsBinding.IsEmpty() && Stage.NumIterations > Settings->MaxSimStageIterations)
				{
					AddFinding(TEXT("NAF020"), EmitterName, INDEX_NONE, FString::Printf(TEXT("Simulation stage '%s' runs %d iterations per frame."), *Stage.Name, Stage.NumIterations), TEXT("Lower the iteration count or lower the grid resolution."));
				}
			}
		}
	}

	for (const TPair<FString, TArray<FString>>& Pair : TranslucentSortKeys)
	{
		if (Pair.Value.Num() > 1)
		{
			AddFinding(TEXT("NAF015"), FString(), INDEX_NONE, FString::Printf(TEXT("Emitters %s share material/SortOrderHint '%s'."), *FString::Join(Pair.Value, TEXT(", ")), *Pair.Key), TEXT("Give each emitter a distinct Sort Order Hint."));
		}
	}

	if (EffectType != nullptr && GetNumQualityLevels() > 0)
	{
		const FNiagaraAutoFlowEffectiveScalability Lowest = UNiagaraAutoFlowToolset_Scalability::ResolveEffectiveScalability(System, 0);
		if (Lowest.bSystemEnabledAtQuality)
		{
			for (const FNiagaraAutoFlowEmitterEffectiveScalability& Emitter : Lowest.Emitters)
			{
				const float Scale = Emitter.bScaleSpawnCount ? Emitter.SpawnCountScale : 1.0f;
				if (Emitter.bEnabledAtQuality && Scale >= Settings->LowQualityMaxSpawnCountScale)
				{
					AddFinding(TEXT("NAF004"), Emitter.EmitterName, INDEX_NONE, FString::Printf(TEXT("Spawn count scale %.3f at %s."), Scale, *Lowest.QualityLevelName), TEXT("Scale spawn count down in the effect type or an emitter scalability override, or gate the emitter off."));
				}
			}
		}
	}

	Report.Findings.StableSort([](const FNiagaraAutoFlowAuditFinding& A, const FNiagaraAutoFlowAuditFinding& B) { return A.Severity > B.Severity; });
	for (const FNiagaraAutoFlowAuditFinding& Finding : Report.Findings)
	{
		switch (Finding.Severity)
		{
		case ENiagaraAutoFlowSeverity::High: ++Report.NumHigh; break;
		case ENiagaraAutoFlowSeverity::Medium: ++Report.NumMedium; break;
		case ENiagaraAutoFlowSeverity::Low: ++Report.NumLow; break;
		default: ++Report.NumInfo; break;
		}
	}
	return Report;
}

FString UNiagaraAutoFlowToolset_Audit::WriteReportFile(const FNiagaraAutoFlowAuditReportFile& File)
{
	const FString Directory = FPaths::IsRelative(GetDefault<UNiagaraAutoFlowSettings>()->ReportDirectory) ? FPaths::Combine(FPaths::ProjectSavedDir(), GetDefault<UNiagaraAutoFlowSettings>()->ReportDirectory) : GetDefault<UNiagaraAutoFlowSettings>()->ReportDirectory;
	const FString SafePath = FPaths::MakeValidFileName(File.PackagePath.Replace(TEXT("/"), TEXT("_")));
	const FString FilePath = FPaths::ConvertRelativePathToFull(FPaths::Combine(Directory, FString::Printf(TEXT("NiagaraAudit%s_%s.json"), *SafePath, *FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S")))));

	FString Json;
	if (!FJsonObjectConverter::UStructToJsonObjectString(File, Json) || !FFileHelper::SaveStringToFile(Json, *FilePath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		// Runs from a ticker after the call returned; there is no script frame to raise into.
		UE_LOG(LogNiagaraAutoFlow, Error, TEXT("Failed to write audit report to %s."), *FilePath);
		return FString();
	}
	return FilePath;
}
