using System;
using System.IO;
using UnrealBuildTool;

public class TruckStationSigns : ModuleRules
{
	public TruckStationSigns(ReadOnlyTargetRules target) : base(target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		OptimizeCode = CodeOptimization.Never;
		string testBuildSetting =
			Environment.GetEnvironmentVariable("TRUCKSTATIONSIGNS_TEST_BUILD") ?? string.Empty;
		bool compileTests =
			Directory.Exists(Path.Combine(ModuleDirectory, "Private", "Tests")) &&
			(target.Configuration != UnrealTargetConfiguration.Shipping || testBuildSetting == "1");
		PublicDefinitions.Add($"TRUCKSTATIONSIGNS_WITH_TESTS={(compileTests ? 1 : 0)}");

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"FactoryGame",
			"SML"
		});

		if (compileTests)
		{
			PrivateDependencyModuleNames.Add("AbstractInstance");
		}
	}
}
