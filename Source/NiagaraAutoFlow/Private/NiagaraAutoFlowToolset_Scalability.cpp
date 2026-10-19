#include "NiagaraAutoFlowToolset_Scalability.h"

#include "NiagaraEffectType.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSettings.h"
#include "NiagaraSystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraAutoFlowToolset_Scalability)

FNiagaraAutoFlowQualityLevels UNiagaraAutoFlowToolset_Scalability::GetQualityLevels()
{
	FNiagaraAutoFlowQualityLevels Result;
	for (const FText& Name : GetDefault<UNiagaraSettings>()->QualityLevels)
	{
		Result.Names.Add(Name.ToString());
	}
	Result.CurrentQualityLevel = FNiagaraPlatformSet::GetQualityLevel();
	return Result;
}

FNiagaraAutoFlowEffectTypeInfo UNiagaraAutoFlowToolset_Scalability::GetEffectTypeInfo(UNiagaraEffectType* EffectType)
{
	FNiagaraAutoFlowEffectTypeInfo Info;
	if (EffectType == nullptr)
	{
		Error(TEXT("EffectType was null. Pass a Niagara Effect Type object path."));
		return Info;
	}

	Info.Path = EffectType->GetPathName();
	Info.UpdateFrequency = EnumToString(EffectType->UpdateFrequency);
	Info.CullReaction = EnumToString(EffectType->CullReaction);
	Info.bAllowCullingForLocalPlayers = EffectType->bAllowCullingForLocalPlayers;
	Info.SignificanceHandler = EffectType->GetSignificanceHandler() ? EffectType->GetSignificanceHandler()->GetClass()->GetName() : FString();

	for (const FNiagaraSystemScalabilitySettings& Settings : EffectType->GetSystemScalabilitySettings().Settings)
	{
		Info.SystemSettings.Add(ToValues(Settings));
	}
	for (const FNiagaraEmitterScalabilitySettings& Settings : EffectType->GetEmitterScalabilitySettings().Settings)
	{
		Info.EmitterSettings.Add(ToValues(Settings));
	}
	return Info;
}

FNiagaraAutoFlowEffectiveScalability UNiagaraAutoFlowToolset_Scalability::GetEffectiveScalability(UNiagaraSystem* System, int32 QualityLevel)
{
	if (!ValidateSystem(System))
	{
		return FNiagaraAutoFlowEffectiveScalability();
	}

	const int32 ResolvedLevel = QualityLevel < 0 ? FNiagaraPlatformSet::GetQualityLevel() : QualityLevel;
	if (ResolvedLevel >= GetNumQualityLevels())
	{
		Error(FString::Printf(TEXT("QualityLevel %d is out of range; the project defines %d effect quality levels (0..%d)."), ResolvedLevel, GetNumQualityLevels(), GetNumQualityLevels() - 1));
		return FNiagaraAutoFlowEffectiveScalability();
	}
	return ResolveEffectiveScalability(*System, ResolvedLevel);
}

FNiagaraAutoFlowEffectiveScalability UNiagaraAutoFlowToolset_Scalability::ResolveEffectiveScalability(UNiagaraSystem& System, int32 QualityLevel)
{
	FNiagaraAutoFlowEffectiveScalability Result;
	Result.SystemPath = System.GetPathName();
	Result.QualityLevel = QualityLevel;
	const TArray<FText>& QualityNames = GetDefault<UNiagaraSettings>()->QualityLevels;
	Result.QualityLevelName = QualityNames.IsValidIndex(QualityLevel) ? QualityNames[QualityLevel].ToString() : FString();
	Result.bSystemEnabledAtQuality = System.GetScalabilityPlatformSet().IsEffectQualityEnabled(QualityLevel);
	Result.Limitations.Add(TEXT("Platform sets are evaluated by effect quality mask only; device-profile and CVar conditions are ignored."));
	Result.Limitations.Add(TEXT("The engine clamps MaxDistance to fx.Niagara.Scalability.MinMaxDistance at runtime; this value is unclamped."));

	// Mirrors UNiagaraSystem / UNiagaraEmitter::ResolveScalabilitySettings for an arbitrary quality level instead of the active one.
	FNiagaraSystemScalabilitySettings Settings;
	Settings.Clear();

	const UNiagaraEffectType* EffectType = System.GetEffectType();
	if (EffectType)
	{
		Result.EffectTypePath = EffectType->GetPathName();
		Result.UpdateFrequency = EnumToString(EffectType->UpdateFrequency);
		Result.CullReaction = EnumToString(EffectType->CullReaction);

		const FNiagaraSystemScalabilitySettings* Match = EffectType->GetSystemScalabilitySettings().Settings.FindByPredicate([QualityLevel](const FNiagaraSystemScalabilitySettings& Entry) { return Entry.Platforms.IsEffectQualityEnabled(QualityLevel); });
		if (Match)
		{
			Settings = *Match;
		}
	}

	if (System.GetOverrideScalabilitySettings())
	{
		for (const FNiagaraSystemScalabilityOverride& Override : System.GetScalabilityOverrides().Overrides)
		{
			if (!Override.Platforms.IsEffectQualityEnabled(QualityLevel))
			{
				continue;
			}

			Result.bSystemOverrideApplied = true;
			if (Override.bOverrideDistanceSettings)
			{
				Settings.bCullByDistance = Override.bCullByDistance;
				Settings.MaxDistance = Override.MaxDistance;
			}
			if (Override.bOverrideInstanceCountSettings)
			{
				Settings.bCullMaxInstanceCount = Override.bCullMaxInstanceCount;
				Settings.MaxInstances = Override.MaxInstances;
			}
			if (Override.bOverridePerSystemInstanceCountSettings)
			{
				Settings.bCullPerSystemMaxInstanceCount = Override.bCullPerSystemMaxInstanceCount;
				Settings.MaxSystemInstances = Override.MaxSystemInstances;
			}
			if (Override.bOverrideVisibilitySettings)
			{
				Settings.VisibilityCulling = Override.VisibilityCulling;
			}
			if (Override.bOverrideGlobalBudgetScalingSettings)
			{
				Settings.BudgetScaling = Override.BudgetScaling;
			}
			if (Override.bOverrideCullProxySettings)
			{
				Settings.CullProxyMode = Override.CullProxyMode;
				Settings.MaxSystemProxies = Override.MaxSystemProxies;
			}
			// Same as the engine: first matching override wins.
			break;
		}
	}

	Result.System = ToValues(Settings);
	Result.System.QualityLevels.Reset();

	const FNiagaraEmitterScalabilitySettings* EffectTypeEmitterSettings = EffectType ? EffectType->GetEmitterScalabilitySettings().Settings.FindByPredicate([QualityLevel](const FNiagaraEmitterScalabilitySettings& Entry) { return Entry.Platforms.IsEffectQualityEnabled(QualityLevel); }) : nullptr;

	for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
	{
		FNiagaraAutoFlowEmitterEffectiveScalability& Emitter = Result.Emitters.AddDefaulted_GetRef();
		Emitter.EmitterName = Handle.GetName().ToString();
		Emitter.bEnabledAtQuality = Handle.GetIsEnabled() && Handle.IsEnabledOnEffectQualityLevel(QualityLevel);
		if (EffectTypeEmitterSettings)
		{
			Emitter.bScaleSpawnCount = EffectTypeEmitterSettings->bScaleSpawnCount;
			Emitter.SpawnCountScale = EffectTypeEmitterSettings->SpawnCountScale;
		}

		const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterMode() == ENiagaraEmitterMode::Standard ? Handle.GetEmitterData() : nullptr;
		if (EmitterData == nullptr)
		{
			continue;
		}

		for (const FNiagaraEmitterScalabilityOverride& Override : EmitterData->ScalabilityOverrides.Overrides)
		{
			if (Override.bOverrideSpawnCountScale && Override.Platforms.IsEffectQualityEnabled(QualityLevel))
			{
				Emitter.bScaleSpawnCount = Override.bScaleSpawnCount;
				Emitter.SpawnCountScale = Override.SpawnCountScale;
				Emitter.bSpawnScaleFromOverride = true;
			}
		}

		const TArray<UNiagaraRendererProperties*>& Renderers = EmitterData->GetRenderers();
		for (int32 RendererIndex = 0; RendererIndex < Renderers.Num(); ++RendererIndex)
		{
			const UNiagaraRendererProperties* Renderer = Renderers[RendererIndex];
			if (Renderer && Renderer->GetIsEnabled() && Renderer->Platforms.IsEffectQualityEnabled(QualityLevel))
			{
				Emitter.EnabledRenderers.Add(RendererIndex);
			}
		}
	}

	return Result;
}

FNiagaraAutoFlowSystemScalabilityValues UNiagaraAutoFlowToolset_Scalability::ToValues(const FNiagaraSystemScalabilitySettings& Settings)
{
	FNiagaraAutoFlowSystemScalabilityValues Values;
	Values.QualityLevels = GetEnabledQualityLevels(Settings.Platforms);
	Values.bCullByDistance = Settings.bCullByDistance;
	Values.MaxDistance = Settings.MaxDistance;
	Values.bCullMaxInstanceCount = Settings.bCullMaxInstanceCount;
	Values.MaxInstances = Settings.MaxInstances;
	Values.bCullPerSystemMaxInstanceCount = Settings.bCullPerSystemMaxInstanceCount;
	Values.MaxSystemInstances = Settings.MaxSystemInstances;
	Values.bCullWhenNotRendered = Settings.VisibilityCulling.bCullWhenNotRendered;
	Values.bCullByViewFrustum = Settings.VisibilityCulling.bCullByViewFrustum;
	Values.MaxTimeWithoutRender = Settings.VisibilityCulling.MaxTimeWithoutRender;
	Values.MaxTimeOutsideViewFrustum = Settings.VisibilityCulling.MaxTimeOutsideViewFrustum;
	Values.CullProxyMode = EnumToString(Settings.CullProxyMode);
	Values.bCullByGlobalBudget = Settings.BudgetScaling.bCullByGlobalBudget;
	Values.MaxGlobalBudgetUsage = Settings.BudgetScaling.MaxGlobalBudgetUsage;
	return Values;
}

FNiagaraAutoFlowEmitterScalabilityValues UNiagaraAutoFlowToolset_Scalability::ToValues(const FNiagaraEmitterScalabilitySettings& Settings)
{
	FNiagaraAutoFlowEmitterScalabilityValues Values;
	Values.QualityLevels = GetEnabledQualityLevels(Settings.Platforms);
	Values.bScaleSpawnCount = Settings.bScaleSpawnCount;
	Values.SpawnCountScale = Settings.SpawnCountScale;
	return Values;
}
