using UnrealBuildTool;

public class CodexBridge : ModuleRules
{
	public CodexBridge(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		OptimizeCode = CodeOptimization.Never;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FactoryGame",
			"Json",
			"Projects",
			"SML"
		});
	}
}
