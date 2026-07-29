using UnrealBuildTool;

public class TruckStationSigns : ModuleRules
{
	public TruckStationSigns(ReadOnlyTargetRules target) : base(target)
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
