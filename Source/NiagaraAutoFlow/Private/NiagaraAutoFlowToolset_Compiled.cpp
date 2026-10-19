#include "NiagaraAutoFlowToolset_Compiled.h"

#include "Containers/Ticker.h"
#include "NiagaraAutoFlowSettings.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "UObject/StrongObjectPtr.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraAutoFlowToolset_Compiled)

UNiagaraAutoFlowAsyncCompileResult* UNiagaraAutoFlowToolset_Compiled::RecompileSystem(UNiagaraSystem* System)
{
	UNiagaraAutoFlowAsyncCompileResult* Result = NewObject<UNiagaraAutoFlowAsyncCompileResult>();
	if (System == nullptr)
	{
		Result->SetError(TEXT("System was null."));
		return Result;
	}

	const double StartTime = FPlatformTime::Seconds();
	System->RequestCompile(/*bForce*/ true);

	RunWhenCompileComplete(System, [StrongResult = TStrongObjectPtr<UNiagaraAutoFlowAsyncCompileResult>(Result), StartTime](UNiagaraSystem* LiveSystem, const FString& WaitError)
	{
		if (!WaitError.IsEmpty())
		{
			StrongResult->SetError(WaitError);
			return;
		}

		FNiagaraAutoFlowCompileResult Value;
		Value.SystemPath = LiveSystem->GetPathName();
		Value.ElapsedSeconds = static_cast<float>(FPlatformTime::Seconds() - StartTime);

		TArray<FNiagaraAutoFlowScriptSlot> Slots;
		GatherScriptSlots(*LiveSystem, Slots);
		for (const FNiagaraAutoFlowScriptSlot& Slot : Slots)
		{
			FNiagaraAutoFlowScriptCompiledInfo& Info = Value.Scripts.Add_GetRef(BuildCompiledInfo(Slot));
			Value.bHasErrors |= Info.CompileStatus == EnumToString(ENiagaraScriptCompileStatus::NCS_Error);
		}
		StrongResult->SetValue(Value);
	});

	return Result;
}

TArray<FNiagaraAutoFlowScriptCompiledInfo> UNiagaraAutoFlowToolset_Compiled::GetCompiledScripts(UNiagaraSystem* System, FName EmitterName)
{
	TArray<FNiagaraAutoFlowScriptCompiledInfo> Result;
	if (!ValidateSystem(System))
	{
		return Result;
	}
	if (!EmitterName.IsNone() && FindEmitterHandle(*System, EmitterName) == nullptr)
	{
		return Result;
	}

	TArray<FNiagaraAutoFlowScriptSlot> Slots;
	GatherScriptSlots(*System, Slots);
	for (const FNiagaraAutoFlowScriptSlot& Slot : Slots)
	{
		if (EmitterName.IsNone() || Slot.EmitterName == EmitterName.ToString())
		{
			Result.Add(BuildCompiledInfo(Slot));
		}
	}
	return Result;
}

FNiagaraAutoFlowSourcePage UNiagaraAutoFlowToolset_Compiled::GetScriptSource(UNiagaraSystem* System, FName EmitterName, const FString& ScriptUsage, int32 UsageIndex, ENiagaraAutoFlowSourceKind SourceKind, int32 Offset, int32 MaxChars)
{
	FNiagaraAutoFlowSourcePage Page;
	if (!ValidateSystem(System))
	{
		return Page;
	}

	TArray<FNiagaraAutoFlowScriptSlot> Slots;
	GatherScriptSlots(*System, Slots);
	const FNiagaraAutoFlowScriptSlot* Slot = FindScriptSlot(Slots, EmitterName, ScriptUsage, UsageIndex);
	if (Slot == nullptr)
	{
		return Page;
	}

	ENiagaraAutoFlowSourceKind ResolvedKind = SourceKind;
	const FString* Source = GetSourceText(*Slot, SourceKind, ResolvedKind);
	Page.SourceKind = EnumToString(ResolvedKind);
	if (Source == nullptr)
	{
		Page.Hint = FString::Printf(TEXT("%s does not exist for %s. GPU HLSL only exists on ParticleGPUComputeScript; CPU HLSL and VMAssembly only on non-GPU scripts."), *Page.SourceKind, *Slot->ScriptUsage);
		return Page;
	}
	if (Source->IsEmpty())
	{
		Page.Hint = TEXT("Source is empty: translations are transient and not loaded from DDC. Call RecompileSystem on this system first, then retry.");
		return Page;
	}

	const int32 PageChars = MaxChars > 0 ? MaxChars : GetDefault<UNiagaraAutoFlowSettings>()->DefaultSourcePageChars;
	Page.TotalChars = Source->Len();
	Page.Offset = FMath::Clamp(Offset, 0, Page.TotalChars);
	Page.Text = Source->Mid(Page.Offset, PageChars);
	Page.bHasMore = Page.Offset + Page.Text.Len() < Page.TotalChars;
	return Page;
}

TArray<FNiagaraAutoFlowSourceMatch> UNiagaraAutoFlowToolset_Compiled::SearchScriptSource(UNiagaraSystem* System, FName EmitterName, const FString& Pattern, ENiagaraAutoFlowSourceKind SourceKind, int32 MaxMatches)
{
	TArray<FNiagaraAutoFlowSourceMatch> Matches;
	if (!ValidateSystem(System))
	{
		return Matches;
	}
	if (Pattern.IsEmpty())
	{
		Error(TEXT("Pattern must not be empty."));
		return Matches;
	}
	if (!EmitterName.IsNone() && FindEmitterHandle(*System, EmitterName) == nullptr)
	{
		return Matches;
	}

	TArray<FNiagaraAutoFlowScriptSlot> Slots;
	GatherScriptSlots(*System, Slots);

	bool bAnySourceAvailable = false;
	for (const FNiagaraAutoFlowScriptSlot& Slot : Slots)
	{
		if (!EmitterName.IsNone() && Slot.EmitterName != EmitterName.ToString())
		{
			continue;
		}

		ENiagaraAutoFlowSourceKind ResolvedKind = SourceKind;
		const FString* Source = GetSourceText(Slot, SourceKind, ResolvedKind);
		if (Source == nullptr || Source->IsEmpty())
		{
			continue;
		}
		bAnySourceAvailable = true;

		TArray<FString> Lines;
		Source->ParseIntoArrayLines(Lines, /*bCullEmpty*/ false);
		for (int32 LineIndex = 0; LineIndex < Lines.Num(); ++LineIndex)
		{
			if (!Lines[LineIndex].Contains(Pattern, ESearchCase::IgnoreCase))
			{
				continue;
			}

			FNiagaraAutoFlowSourceMatch& Match = Matches.AddDefaulted_GetRef();
			Match.EmitterName = Slot.EmitterName;
			Match.ScriptUsage = Slot.ScriptUsage;
			Match.UsageIndex = Slot.UsageIndex;
			Match.SourceKind = EnumToString(ResolvedKind);
			Match.Line = LineIndex;
			Match.LineText = Lines[LineIndex].TrimStartAndEnd();
			if (Matches.Num() >= MaxMatches)
			{
				return Matches;
			}
		}
	}

	if (!bAnySourceAvailable)
	{
		Error(TEXT("No translated source is available for the selected scripts. Call RecompileSystem on this system first, then retry."));
	}
	return Matches;
}

FNiagaraAutoFlowScriptCompiledInfo UNiagaraAutoFlowToolset_Compiled::BuildCompiledInfo(const FNiagaraAutoFlowScriptSlot& Slot)
{
	check(Slot.Script);

	FNiagaraAutoFlowScriptCompiledInfo Info;
	Info.EmitterName = Slot.EmitterName;
	Info.ScriptUsage = Slot.ScriptUsage;
	Info.UsageIndex = Slot.UsageIndex;
	Info.bIsGPUScript = Slot.Script->IsGPUScript();

	const FNiagaraVMExecutableData& ExeData = Slot.Script->GetVMExecutableData();
	Info.CompileStatus = EnumToString(ExeData.LastCompileStatus);
	Info.ByteCodeBytes = ExeData.ByteCode.GetLength();
	Info.NumTempRegisters = ExeData.NumTempRegisters;
	Info.NumAttributes = ExeData.Attributes.Num();
	Info.NumExternalFunctionCalls = ExeData.CalledVMExternalFunctions.Num();
	Info.NumSimulationStages = ExeData.SimulationStageMetaData.Num();

#if WITH_EDITORONLY_DATA
	Info.LastOpCount = static_cast<int32>(ExeData.LastOpCount);
	Info.CpuHlslChars = ExeData.LastHlslTranslation.Len();
	Info.GpuHlslChars = ExeData.LastHlslTranslationGPU.Len();
	Info.VMAssemblyChars = ExeData.LastAssemblyTranslation.Len();
	Info.ParameterCollections = ExeData.ParameterCollectionPaths;

	for (const FNiagaraVariableBase& Attribute : ExeData.AttributesWritten)
	{
		Info.AttributesWritten.Add(NormalizeAttributeName(Attribute.GetName().ToString()));
	}

	for (const FNiagaraScriptDataInterfaceCompileInfo& DIInfo : ExeData.DataInterfaceInfo)
	{
		FNiagaraAutoFlowDataInterfaceUse& Use = Info.DataInterfaces.AddDefaulted_GetRef();
		Use.Name = DIInfo.Name.ToString();
		const UClass* DIClass = DIInfo.Type.GetClass();
		Use.ClassName = DIClass ? DIClass->GetName() : DIInfo.Type.GetName();
	}
#endif

	return Info;
}

void UNiagaraAutoFlowToolset_Compiled::RunWhenCompileComplete(UNiagaraSystem* System, TFunction<void(UNiagaraSystem*, const FString&)>&& OnDone)
{
	check(System);

	// Poll rather than bind OnSystemCompiled so a cancelled compile cannot leave the result hanging; flushing also kicks deferred requests.
	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakSystem = TWeakObjectPtr<UNiagaraSystem>(System), Callback = MoveTemp(OnDone), StartTime = FPlatformTime::Seconds()](float) mutable -> bool
	{
		UNiagaraSystem* LiveSystem = WeakSystem.Get();
		if (LiveSystem == nullptr)
		{
			Callback(nullptr, TEXT("System was destroyed before the compile completed."));
			return false;
		}

		LiveSystem->PollForCompilationComplete(/*bFlushRequestCompile*/ true);
		if (LiveSystem->HasActiveCompilations() || LiveSystem->HasOutstandingCompilationRequests(/*bIncludingGPUShaders*/ false))
		{
			const float Timeout = GetDefault<UNiagaraAutoFlowSettings>()->CompileWaitTimeoutSeconds;
			if (FPlatformTime::Seconds() - StartTime > Timeout)
			{
				Callback(LiveSystem, FString::Printf(TEXT("Timed out after %.0f seconds waiting for the Niagara System compile."), Timeout));
				return false;
			}
			return true;
		}

		Callback(LiveSystem, FString());
		return false;
	}));
}

const FString* UNiagaraAutoFlowToolset_Compiled::GetSourceText(const FNiagaraAutoFlowScriptSlot& Slot, ENiagaraAutoFlowSourceKind SourceKind, ENiagaraAutoFlowSourceKind& OutResolvedKind)
{
	const bool bIsGPUScript = Slot.Script->IsGPUScript();
	OutResolvedKind = SourceKind == ENiagaraAutoFlowSourceKind::Auto ? (bIsGPUScript ? ENiagaraAutoFlowSourceKind::GpuHlsl : ENiagaraAutoFlowSourceKind::CpuHlsl) : SourceKind;

#if WITH_EDITORONLY_DATA
	const FNiagaraVMExecutableData& ExeData = Slot.Script->GetVMExecutableData();
	switch (OutResolvedKind)
	{
	case ENiagaraAutoFlowSourceKind::GpuHlsl:
		return bIsGPUScript ? &ExeData.LastHlslTranslationGPU : nullptr;
	case ENiagaraAutoFlowSourceKind::CpuHlsl:
		return bIsGPUScript ? nullptr : &ExeData.LastHlslTranslation;
	case ENiagaraAutoFlowSourceKind::VMAssembly:
		return bIsGPUScript ? nullptr : &ExeData.LastAssemblyTranslation;
	default:
		break;
	}
#endif
	return nullptr;
}
