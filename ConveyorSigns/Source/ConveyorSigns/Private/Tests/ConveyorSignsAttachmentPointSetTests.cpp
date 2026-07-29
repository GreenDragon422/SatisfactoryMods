#if WITH_DEV_AUTOMATION_TESTS

#include "ConveyorSignsAttachmentLayout.h"
#include "ConveyorSignsAttachmentPointSet.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsAttachmentPointSetDoesNotDuplicateTest,
	"ConveyorSigns.AttachmentPoints.RepeatedQueriesDoNotAccumulateDuplicates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsAttachmentPointSetDoesNotDuplicateTest::RunTest(const FString& parameters)
{
	FConveyorSignsAttachmentPointSet attachmentPointSet;
	TArray<const FFGAttachmentPoint*> outputPoints;

	attachmentPointSet.AppendTo(outputPoints);
	attachmentPointSet.AppendTo(outputPoints);

	TestEqual(
		TEXT("Repeated queries expose only the three incoming-head attachment entries"),
		outputPoints.Num(),
		FConveyorSignsAttachmentLayout::FaceCount);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsGenericCenterTypeLoadsTest,
	"ConveyorSigns.AttachmentPoints.GenericCenterTypeSupportsEveryVanillaWallSign",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsGenericCenterTypeLoadsTest::RunTest(const FString& parameters)
{
	const TSubclassOf<UFGAttachmentPointType> attachmentType = LoadClass<UFGAttachmentPointType>(
		nullptr,
		TEXT("/Game/FactoryGame/Buildable/-Shared/AttachmentPointTypes/Sign/APT_SignCenter.APT_SignCenter_C"));

	TestNotNull(TEXT("The vanilla generic center sign attachment type loads"), attachmentType.Get());
	return true;
}

#endif
