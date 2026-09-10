using UnrealBuildTool;

public class BeltPurge : ModuleRules
{
	public BeltPurge(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		CppStandard = CppStandardVersion.Cpp20;

		RuntimeDependencies.Add("$(PluginDir)/Resources/Icon128.png", StagedFileType.NonUFS);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"AbstractInstance",
			"FactoryGame",
			"InputCore",
			"SML"
		});
	}
}
