using UnrealBuildTool;

public class ConveyorSigns : ModuleRules
{
	public ConveyorSigns(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		OptimizeCode = CodeOptimization.Never;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FactoryGame",
			"SML"
		});
	}
}
