using UnrealBuildTool;

public class PathFinder : ModuleRules
{
	public PathFinder(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		OptimizeCode = CodeOptimization.Never;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FactoryGame",
			"RenderCore",
			"Slate",
			"SlateCore",
			"SML",
			"UMG"
		});
	}
}
