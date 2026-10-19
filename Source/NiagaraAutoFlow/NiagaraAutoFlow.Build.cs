using UnrealBuildTool;

public class NiagaraAutoFlow : ModuleRules
{
	public NiagaraAutoFlow(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;
		bWarningsAsErrors = true;
		CppCompileWarningSettings.ShadowVariableWarningLevel = WarningLevel.Error;

		if (Target.bBuildEditor)
		{
			OptimizeCode = CodeOptimization.InShippingBuildsOnly;
		}

		// Experimental ToolsetRegistry stays private so its API churn stays inside this module.
		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"DeveloperSettings",
			"Json",
			"JsonUtilities",
			"AssetRegistry",
			"UnrealEd",
			"Niagara",
			"NiagaraCore",
			"NiagaraShader",
			"ToolsetRegistry",
		});
	}
}
