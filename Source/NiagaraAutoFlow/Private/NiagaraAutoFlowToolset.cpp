#include "NiagaraAutoFlowToolset.h"

#include "Kismet/KismetSystemLibrary.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraPlatformSet.h"
#include "NiagaraScript.h"
#include "NiagaraSettings.h"
#include "NiagaraSystem.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraAutoFlowToolset)

void UNiagaraAutoFlowToolset::Error(const FString& Message)
{
	UKismetSystemLibrary::RaiseScriptError(Message);
}

bool UNiagaraAutoFlowToolset::ValidateSystem(const UNiagaraSystem* System)
{
	if (System == nullptr)
	{
		Error(TEXT("System was null. Pass a Niagara System object path, e.g. /Game/FX/NS_Foo.NS_Foo."));
		return false;
	}
	return true;
}

TArray<FString> UNiagaraAutoFlowToolset::GetEmitterNames(const UNiagaraSystem& System)
{
	TArray<FString> Names;
	for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
	{
		Names.Add(Handle.GetName().ToString());
	}
	return Names;
}

int32 UNiagaraAutoFlowToolset::FindEmitterIndex(const UNiagaraSystem& System, FName EmitterName)
{
	const int32 Index = System.GetEmitterHandles().IndexOfByPredicate([EmitterName](const FNiagaraEmitterHandle& Handle) { return Handle.GetName() == EmitterName; });
	if (Index == INDEX_NONE)
	{
		Error(FString::Printf(TEXT("Emitter '%s' not found in %s. Available emitters: [%s]"), *EmitterName.ToString(), *System.GetPathName(), *FString::Join(GetEmitterNames(System), TEXT(", "))));
	}
	return Index;
}

const FNiagaraEmitterHandle* UNiagaraAutoFlowToolset::FindEmitterHandle(const UNiagaraSystem& System, FName EmitterName)
{
	const int32 Index = FindEmitterIndex(System, EmitterName);
	return Index == INDEX_NONE ? nullptr : &System.GetEmitterHandles()[Index];
}

FVersionedNiagaraEmitterData* UNiagaraAutoFlowToolset::FindEmitterData(const UNiagaraSystem& System, FName EmitterName)
{
	const FNiagaraEmitterHandle* Handle = FindEmitterHandle(System, EmitterName);
	if (Handle == nullptr)
	{
		return nullptr;
	}

	if (Handle->GetEmitterMode() != ENiagaraEmitterMode::Standard)
	{
		Error(FString::Printf(TEXT("Emitter '%s' is a %s emitter; only Standard emitters carry scripts and compiled data."), *EmitterName.ToString(), *EnumToString(Handle->GetEmitterMode())));
		return nullptr;
	}

	FVersionedNiagaraEmitterData* EmitterData = Handle->GetEmitterData();
	if (EmitterData == nullptr)
	{
		Error(FString::Printf(TEXT("Emitter '%s' has no emitter data (missing emitter asset?)."), *EmitterName.ToString()));
	}
	return EmitterData;
}

void UNiagaraAutoFlowToolset::GatherScriptSlots(const UNiagaraSystem& System, TArray<FNiagaraAutoFlowScriptSlot>& OutSlots)
{
	TMap<TPair<FString, FString>, int32> UsageCounts;
	auto AddSlot = [&OutSlots, &UsageCounts](const UNiagaraScript* Script, const FString& EmitterName)
	{
		if (Script == nullptr)
		{
			return;
		}

		ENiagaraScriptUsage Usage = Script->GetUsage();
		if (Usage == ENiagaraScriptUsage::ParticleSpawnScriptInterpolated)
		{
			Usage = ENiagaraScriptUsage::ParticleSpawnScript;
		}

		FNiagaraAutoFlowScriptSlot& Slot = OutSlots.AddDefaulted_GetRef();
		Slot.Script = Script;
		Slot.EmitterName = EmitterName;
		Slot.ScriptUsage = EnumToString(Usage);
		Slot.UsageIndex = UsageCounts.FindOrAdd(TPair<FString, FString>(EmitterName, Slot.ScriptUsage))++;
	};

	AddSlot(System.GetSystemSpawnScript(), FString());
	AddSlot(System.GetSystemUpdateScript(), FString());

	for (const FNiagaraEmitterHandle& Handle : System.GetEmitterHandles())
	{
		if (Handle.GetEmitterMode() != ENiagaraEmitterMode::Standard)
		{
			continue;
		}

		if (const FVersionedNiagaraEmitterData* EmitterData = Handle.GetEmitterData())
		{
			TArray<UNiagaraScript*> Scripts;
			EmitterData->GetScripts(Scripts, /*bCompilableOnly*/ true);
			for (UNiagaraScript* Script : Scripts)
			{
				AddSlot(Script, Handle.GetName().ToString());
			}
		}
	}
}

const FNiagaraAutoFlowScriptSlot* UNiagaraAutoFlowToolset::FindScriptSlot(const TArray<FNiagaraAutoFlowScriptSlot>& Slots, FName EmitterName, const FString& ScriptUsage, int32 UsageIndex)
{
	const FString EmitterString = EmitterName.IsNone() ? FString() : EmitterName.ToString();
	for (const FNiagaraAutoFlowScriptSlot& Slot : Slots)
	{
		if (Slot.EmitterName.Equals(EmitterString, ESearchCase::IgnoreCase) && Slot.ScriptUsage.Equals(ScriptUsage, ESearchCase::IgnoreCase) && Slot.UsageIndex == UsageIndex)
		{
			return &Slot;
		}
	}

	TArray<FString> Available;
	for (const FNiagaraAutoFlowScriptSlot& Slot : Slots)
	{
		Available.Add(FString::Printf(TEXT("(%s, %s, %d)"), Slot.EmitterName.IsEmpty() ? TEXT("<System>") : *Slot.EmitterName, *Slot.ScriptUsage, Slot.UsageIndex));
	}
	Error(FString::Printf(TEXT("Script (%s, %s, %d) not found. Use EmitterName None for system scripts. Available (EmitterName, ScriptUsage, UsageIndex): %s"), EmitterName.IsNone() ? TEXT("<System>") : *EmitterString, *ScriptUsage, UsageIndex, *FString::Join(Available, TEXT(", "))));
	return nullptr;
}

FString UNiagaraAutoFlowToolset::NormalizeAttributeName(const FString& Name)
{
	static const FString ParticlesPrefix(TEXT("Particles."));
	return Name.StartsWith(ParticlesPrefix) ? Name.RightChop(ParticlesPrefix.Len()) : Name;
}

int32 UNiagaraAutoFlowToolset::GetNumQualityLevels()
{
	return GetDefault<UNiagaraSettings>()->QualityLevels.Num();
}

TArray<int32> UNiagaraAutoFlowToolset::GetEnabledQualityLevels(const FNiagaraPlatformSet& Platforms)
{
	TArray<int32> Levels;
	for (int32 QualityLevel = 0; QualityLevel < GetNumQualityLevels(); ++QualityLevel)
	{
		if (Platforms.IsEffectQualityEnabled(QualityLevel))
		{
			Levels.Add(QualityLevel);
		}
	}
	return Levels;
}
