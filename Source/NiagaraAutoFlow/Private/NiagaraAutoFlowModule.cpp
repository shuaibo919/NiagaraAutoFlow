#include "NiagaraAutoFlowModule.h"

#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "NiagaraAutoFlowToolset_Attributes.h"
#include "NiagaraAutoFlowToolset_Audit.h"
#include "NiagaraAutoFlowToolset_Compiled.h"
#include "NiagaraAutoFlowToolset_Runtime.h"
#include "NiagaraAutoFlowToolset_Scalability.h"
#include "NiagaraAutoFlowToolset_Topology.h"
#include "ToolsetRegistry/UToolsetRegistry.h"

DEFINE_LOG_CATEGORY(LogNiagaraAutoFlow);

void FNiagaraAutoFlowModule::StartupModule()
{
	// The registry subsystem may not exist yet at PostEngineInit.
	FCoreDelegates::OnAllModuleLoadingPhasesComplete.AddRaw(this, &FNiagaraAutoFlowModule::RegisterToolsets);
	FCoreDelegates::OnPreExit.AddRaw(this, &FNiagaraAutoFlowModule::UnregisterToolsets);
	RegisterToolsets();
}

void FNiagaraAutoFlowModule::ShutdownModule()
{
	FCoreDelegates::OnAllModuleLoadingPhasesComplete.RemoveAll(this);
	FCoreDelegates::OnPreExit.RemoveAll(this);
	UnregisterToolsets();
}

void FNiagaraAutoFlowModule::RegisterToolsets()
{
	// UToolsetRegistry rejects commandlets and unattended runs; skip to avoid misleading warnings.
	if (RegisteredToolsets.Num() > 0 || IsRunningCommandlet() || FApp::IsUnattended() || !UToolsetRegistry::IsAvailable())
	{
		return;
	}

	const TSubclassOf<UToolsetDefinition> Toolsets[] =
	{
		UNiagaraAutoFlowToolset_Compiled::StaticClass(),
		UNiagaraAutoFlowToolset_Attributes::StaticClass(),
		UNiagaraAutoFlowToolset_Topology::StaticClass(),
		UNiagaraAutoFlowToolset_Scalability::StaticClass(),
		UNiagaraAutoFlowToolset_Audit::StaticClass(),
		UNiagaraAutoFlowToolset_Runtime::StaticClass(),
	};

	for (const TSubclassOf<UToolsetDefinition>& Toolset : Toolsets)
	{
		UToolsetRegistry::RegisterToolsetClass(Toolset);
		if (UToolsetRegistry::IsToolsetClassRegistered(Toolset))
		{
			RegisteredToolsets.Add(Toolset);
		}
		else
		{
			UE_LOG(LogNiagaraAutoFlow, Warning, TEXT("Toolset %s was rejected by the Toolset Registry; see LogToolsetRegistry for the invalid tool."), *Toolset->GetName());
		}
	}
}

void FNiagaraAutoFlowModule::UnregisterToolsets()
{
	if (UToolsetRegistry::IsAvailable())
	{
		for (const TSubclassOf<UToolsetDefinition>& Toolset : RegisteredToolsets)
		{
			UToolsetRegistry::UnregisterToolsetClass(Toolset);
		}
	}
	RegisteredToolsets.Reset();
}

IMPLEMENT_MODULE(FNiagaraAutoFlowModule, NiagaraAutoFlow)
