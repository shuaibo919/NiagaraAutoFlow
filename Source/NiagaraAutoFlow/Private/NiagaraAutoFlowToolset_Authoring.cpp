#include "NiagaraAutoFlowToolset_Authoring.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "JsonObjectConverter.h"
#include "Misc/PackageName.h"
#include "NiagaraAutoFlowSettings.h"
#include "NiagaraExternalSystemEditorUtilities.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraEmitter.h"
#include "NiagaraScript.h"
#include "NiagaraSystem.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(NiagaraAutoFlowToolset_Authoring)

namespace NiagaraAutoFlowAuthoring
{
	static TSet<TWeakObjectPtr<UNiagaraScript>> PendingScripts;

	static bool CheckContext(UNiagaraScript* Script, UNiagaraSystem* System, FString& Error, const FNiagaraModuleAuthoringDefinition* Candidate = nullptr)
	{
		if (!Script || !System) { Error = TEXT("Provide a module and a context system containing it."); return false; }
		if (PendingScripts.Contains(Script) || System->HasActiveCompilations() || System->HasOutstandingCompilationRequests(true))
		{
			Error = TEXT("The script or context system is busy; wait for its compilation to finish.");
			return false;
		}
		FNiagaraExternalEditContext Context(System);
		auto StackMatches = [&](const FNiagaraExt_ScriptStackTopology& Stack)
		{
			const int64 Usage = StaticEnum<ENiagaraScriptUsage>()->GetValueByName(Stack.ScriptName);
			const bool bAllowed = Usage != INDEX_NONE && (Candidate ? Candidate->AllowedUsages.Contains(static_cast<ENiagaraScriptUsage>(Usage)) :
				(Script->GetLatestScriptData()->ModuleUsageBitmask & (1 << Usage)) != 0);
			return bAllowed && Stack.Modules.ContainsByPredicate([Script](const FNiagaraExt_ModuleTopology& Module) { return Module.ModuleScript == Script && Module.Enabled; });
		};
		bool bReferenced = false;
		for (const TCHAR* Usage : { TEXT("SystemSpawnScript"), TEXT("SystemUpdateScript") })
		{
			FNiagaraExt_ScriptStackTopology Stack;
			UNiagaraExternalEditUtilities::GetScriptStackTopology(FNiagaraExt_StackItemReference(System, NAME_None, Usage), Stack, Context);
			bReferenced |= StackMatches(Stack);
		}
		for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
		{
			if (!Handle.GetIsEnabled() || !Handle.GetEmitterData()) { continue; }
			FNiagaraExt_EmitterTopology Topology;
			UNiagaraExternalEditUtilities::GetEmitterTopology(FNiagaraExt_StackItemReference(System, Handle.GetName()), Topology, Context);
			bReferenced |= StackMatches(Topology.EmitterSpawnScript) || StackMatches(Topology.EmitterUpdateScript) ||
				StackMatches(Topology.ParticleSpawnScript) || StackMatches(Topology.ParticleUpdateScript);
		}
		if (Context.HasErrors())
		{
			for (const FText& Message : Context.Errors) { Error += Message.ToString() + TEXT("\n"); }
			return false;
		}
		if (!bReferenced)
		{
			Error = TEXT("Context has no enabled use of this module in an allowed stack. AddModule first; an unrelated/disabled/incompatible use cannot validate it.");
			return false;
		}
		return true;
	}

	static bool SaveModule(UNiagaraScript* Script, FString& Error)
	{
		UPackage* Package = Script->GetPackage();
		FString Filename;
		if (Package == GetTransientPackage() || !FPackageName::TryConvertLongPackageNameToFilename(Package->GetName(), Filename, FPackageName::GetAssetPackageExtension()))
		{
			Error = TEXT("Cannot save a transient module or an unmapped package.");
			return false;
		}
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		if (!UPackage::SavePackage(Package, Script, *Filename, Args))
		{
			Error = TEXT("Module compiled, but saving its package failed.");
			return false;
		}
		return true;
	}

	static bool CollectCompileResult(UNiagaraSystem* System, TArray<FString>& Messages, int32& GPUCount)
	{
		TArray<FNiagaraAutoFlowScriptSlot> Slots;
		UNiagaraAutoFlowToolset::GatherScriptSlots(*System, Slots);
		bool bSuccess = !Slots.IsEmpty();
		GPUCount = 0;
		for (const FNiagaraAutoFlowScriptSlot& Slot : Slots)
		{
			const ENiagaraScriptUsage Usage = Slot.Script->GetUsage();
			if (Usage == ENiagaraScriptUsage::EmitterSpawnScript || Usage == ENiagaraScriptUsage::EmitterUpdateScript) { continue; }
			if (!Slot.EmitterName.IsEmpty())
			{
				const FNiagaraEmitterHandle* Handle = UNiagaraAutoFlowToolset::FindEmitterHandle(*System, *Slot.EmitterName);
				if (!Handle || !Handle->GetIsEnabled()) { continue; }
				const FVersionedNiagaraEmitterData* Data = Handle->GetEmitterData();
				if (Data && Data->SimTarget == ENiagaraSimTarget::GPUComputeSim && Usage != ENiagaraScriptUsage::ParticleGPUComputeScript) { continue; }
			}
			const bool bGPU = Usage == ENiagaraScriptUsage::ParticleGPUComputeScript;
			const bool bReady = Slot.Script->GetVMExecutableData().LastCompileStatus != ENiagaraScriptCompileStatus::NCS_Error && Slot.Script->DidScriptCompilationSucceed(bGPU) &&
				Slot.Script->IsReadyToRun(bGPU ? ENiagaraSimTarget::GPUComputeSim : ENiagaraSimTarget::CPUSim);
			GPUCount += bGPU ? 1 : 0;
			bSuccess &= bReady;
			if (!bReady) { Messages.Add(Slot.EmitterName + TEXT("/") + Slot.ScriptUsage + TEXT(": compilation failed or executable is not ready.")); }
			for (const FNiagaraCompileEvent& Event : Slot.Script->GetVMExecutableData().LastCompileEvents)
			{
				Messages.Add(Slot.EmitterName + TEXT("/") + Slot.ScriptUsage + TEXT(": ") + Event.Message);
			}
		}
		return bSuccess;
	}

	static void BeginValidation(UNiagaraAutoFlowAsyncAuthoringResult* Result, UNiagaraScript* Script, UNiagaraSystem* System,
		FNiagaraAutoFlowAuthoringResult Value, bool bRollback, bool bSave)
	{
		PendingScripts.Add(Script);
		System->RequestCompile(true);
		FString ExpectedDefinition;
		FJsonObjectConverter::UStructToJsonObjectString(Value.Definition, ExpectedDefinition);
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
			[StrongResult = TStrongObjectPtr<UNiagaraAutoFlowAsyncAuthoringResult>(Result),
			 StrongScript = TStrongObjectPtr<UNiagaraScript>(Script), StrongSystem = TStrongObjectPtr<UNiagaraSystem>(System),
			 Value = MoveTemp(Value), ExpectedDefinition = MoveTemp(ExpectedDefinition), bRollback, bSave,
			 bRestoring = false, StartTime = FPlatformTime::Seconds()](float) mutable -> bool
			{
				UNiagaraScript* LiveScript = StrongScript.Get();
				UNiagaraSystem* LiveSystem = StrongSystem.Get();
				const bool bBusy = LiveSystem->HasActiveCompilations() || LiveSystem->HasOutstandingCompilationRequests(true);
				const bool bTimeout = FPlatformTime::Seconds() - StartTime > GetDefault<UNiagaraAutoFlowSettings>()->CompileWaitTimeoutSeconds;
				if (bBusy && !bTimeout) { return true; }
				int32 GPUCount = 0;
				bool bSuccess = false;
				if (bTimeout) { Value.Diagnostics.Add(TEXT("Timed out waiting for VM/GPU compilation; no save performed.")); }
				else { bSuccess = CollectCompileResult(LiveSystem, Value.Diagnostics, GPUCount); }

				FNiagaraModuleAuthoringDefinition Current;
				FString Error;
				FString CurrentDefinition;
				const bool bManaged = FNiagaraScriptAuthoringUtilities::GetModuleDefinition(LiveScript, Current, Error);
				if (bManaged) { FJsonObjectConverter::UStructToJsonObjectString(Current, CurrentDefinition); }
				const bool bUnchanged = bManaged && CurrentDefinition == ExpectedDefinition;
				if (!bUnchanged)
				{
					bSuccess = false;
					Value.Diagnostics.Add(TEXT("Module changed while validation was running; refusing automatic save or rollback."));
				}
				if (bRestoring)
				{
					Value.bRollbackCompileSucceeded = bSuccess;
				}
				else
				{
					Value.bValidated = !bTimeout && bUnchanged;
					Value.bCompileSucceeded = bSuccess;
					Value.GPUComputeScriptsChecked = GPUCount;
					if (!bSuccess && bRollback && bUnchanged && !bBusy)
					{
						if (FNiagaraScriptAuthoringUtilities::UpdateModuleDefinition(LiveScript, Value.PreviousDefinition, Error))
						{
							Value.bRolledBack = true;
							Value.Definition = Value.PreviousDefinition;
							FJsonObjectConverter::UStructToJsonObjectString(Value.Definition, ExpectedDefinition);
							bRestoring = true;
							StartTime = FPlatformTime::Seconds();
							LiveSystem->RequestCompile(true);
							return true;
						}
						Value.Diagnostics.Add(TEXT("Rollback failed: ") + Error);
					}
					else if (bTimeout && bRollback) { Value.Diagnostics.Add(TEXT("Compilation is still busy; previous definition is returned for explicit recovery after it finishes.")); }
					if (bSuccess && bSave)
					{
						Value.bSaved = SaveModule(LiveScript, Error);
						if (!Value.bSaved) { Value.Diagnostics.Add(Error); }
					}
				}
				PendingScripts.Remove(LiveScript);
				StrongResult->SetValue(Value);
				return false;
			}));
	}
}

FNiagaraAutoFlowAuthoringResult UNiagaraAutoFlowToolset_Authoring::CreateModuleScriptFromHlsl(const FString& AssetName, const FString& AssetPath, const FNiagaraModuleAuthoringDefinition& Definition)
{
	FNiagaraAutoFlowAuthoringResult Result;
	FString ErrorText;
	if (!FNiagaraScriptAuthoringUtilities::ValidateDefinition(Definition, ErrorText)) { Error(ErrorText); return Result; }
	const FString PackageName = AssetPath / AssetName;
	FText Reason;
	if (!AssetPath.StartsWith(TEXT("/Game/")) || AssetName.IsEmpty() || AssetName.Contains(TEXT("/")) || AssetName.Contains(TEXT(".")) ||
		!FPackageName::IsValidLongPackageName(PackageName, false, &Reason) || FPackageName::DoesPackageExist(PackageName) || FindPackage(nullptr, *PackageName))
	{
		Error(TEXT("Use a new asset/package below /Game/. Existing packages are never overwritten. ") + Reason.ToString());
		return Result;
	}
	UPackage* Package = CreatePackage(*PackageName);
	Result.Script = FNiagaraScriptAuthoringUtilities::CreateModuleScript(Package, *AssetName, Definition, ErrorText);
	if (!Result.Script) { Error(ErrorText); return Result; }
	FAssetRegistryModule::AssetCreated(Result.Script);
	Result.Definition = Definition;
	Result.Diagnostics.Add(TEXT("Created in memory, not compiled or saved. Add it to a system and call ValidateModuleScript; CPU and GPU compatibility depend on the actual contexts tested."));
	return Result;
}

FNiagaraModuleAuthoringDefinition UNiagaraAutoFlowToolset_Authoring::GetModuleScriptDefinition(UNiagaraScript* Script)
{
	FNiagaraModuleAuthoringDefinition Definition;
	FString ErrorText;
	if (!FNiagaraScriptAuthoringUtilities::GetModuleDefinition(Script, Definition, ErrorText)) { Error(ErrorText); }
	return Definition;
}

UNiagaraAutoFlowAsyncAuthoringResult* UNiagaraAutoFlowToolset_Authoring::UpdateModuleScriptDefinition(UNiagaraScript* Script,
	const FNiagaraModuleAuthoringDefinition& Definition, UNiagaraSystem* ValidationSystem, bool bRollbackOnFailure, bool bSaveOnSuccess)
{
	using namespace NiagaraAutoFlowAuthoring;
	UNiagaraAutoFlowAsyncAuthoringResult* Result = NewObject<UNiagaraAutoFlowAsyncAuthoringResult>();
	FNiagaraAutoFlowAuthoringResult Value;
	FString ErrorText;
	if (PendingScripts.Contains(Script) || (!ValidationSystem && bSaveOnSuccess))
	{
		Result->SetError(TEXT("Module is busy, or saving was requested without a validation context."));
		return Result;
	}
	if ((ValidationSystem && !CheckContext(Script, ValidationSystem, ErrorText, &Definition)) ||
		!FNiagaraScriptAuthoringUtilities::GetModuleDefinition(Script, Value.PreviousDefinition, ErrorText) ||
		!FNiagaraScriptAuthoringUtilities::UpdateModuleDefinition(Script, Definition, ErrorText))
	{
		Result->SetError(ErrorText);
		return Result;
	}
	Value.Script = Script;
	Value.Definition = Definition;
	if (!ValidationSystem)
	{
		Value.Diagnostics.Add(TEXT("Updated in memory only. No compile validation, rollback-on-compile-failure or save was performed; PreviousDefinition can restore this update."));
		Result->SetValue(Value);
	}
	else { BeginValidation(Result, Script, ValidationSystem, MoveTemp(Value), bRollbackOnFailure, bSaveOnSuccess); }
	return Result;
}

UNiagaraAutoFlowAsyncAuthoringResult* UNiagaraAutoFlowToolset_Authoring::ValidateModuleScript(UNiagaraScript* Script, UNiagaraSystem* ValidationSystem, bool bSaveOnSuccess)
{
	UNiagaraAutoFlowAsyncAuthoringResult* Result = NewObject<UNiagaraAutoFlowAsyncAuthoringResult>();
	FNiagaraAutoFlowAuthoringResult Value;
	FString ErrorText;
	if (!FNiagaraScriptAuthoringUtilities::GetModuleDefinition(Script, Value.Definition, ErrorText) ||
		!NiagaraAutoFlowAuthoring::CheckContext(Script, ValidationSystem, ErrorText))
	{
		Result->SetError(ErrorText);
		return Result;
	}
	Value.Script = Script;
	NiagaraAutoFlowAuthoring::BeginValidation(Result, Script, ValidationSystem, MoveTemp(Value), false, bSaveOnSuccess);
	return Result;
}
