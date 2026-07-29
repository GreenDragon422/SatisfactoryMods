using UnrealBuildTool;

public class BeltPurge : ModuleRules
{
	public BeltPurge(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FactoryGame",
			"InputCore",
			"SML"
		});
	}
}
