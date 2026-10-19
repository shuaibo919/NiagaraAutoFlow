#pragma once

#include "Logging/LogMacros.h"
#include "Modules/ModuleInterface.h"
#include "Templates/SubclassOf.h"

class UToolsetDefinition;

DECLARE_LOG_CATEGORY_EXTERN(LogNiagaraAutoFlow, Log, All);

/** Registers the NiagaraAutoFlow toolsets with the Toolset Registry (Experimental plugin, kept private to this module). */
class FNiagaraAutoFlowModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterToolsets();
	void UnregisterToolsets();

	TArray<TSubclassOf<UToolsetDefinition>> RegisteredToolsets;
};
