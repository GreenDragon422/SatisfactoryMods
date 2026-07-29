#if WITH_DEV_AUTOMATION_TESTS

#include "ConveyorSignsTestSignPolicy.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsRecognizesReloadedTestSourceNamesTest,
	"ConveyorSigns.Diagnostics.RecognizesTestSourceAfterReloadByActorName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsRecognizesReloadedTestSourceNamesTest::RunTest(const FString& parameters)
{
	TestTrue(
		TEXT("The requested base actor name identifies a test source"),
		FConveyorSignsTestSignPolicy::IsSourceName(FConveyorSignsTestSignPolicy::GetSourceBaseName()));
	TestTrue(
		TEXT("An Unreal uniqueness suffix remains identifiable after save and reload"),
		FConveyorSignsTestSignPolicy::IsSourceName(TEXT("ConveyorSignsAngleTestSource_17")));
	TestFalse(
		TEXT("A normal player sign is not identified as a test source"),
		FConveyorSignsTestSignPolicy::IsSourceName(TEXT("Build_StandaloneWidgetSign_Medium_C_2147358324")));
	return true;
}

#endif
