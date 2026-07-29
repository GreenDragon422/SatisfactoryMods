#if WITH_DEV_AUTOMATION_TESTS

#include "ConveyorSignsAttachmentLayout.h"
#include "ConveyorSignsEndpoints.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsResolvesLogicalEndpointsTest,
	"ConveyorSigns.Endpoints.ResolvesInputAndOutputByConnectorLocation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsResolvesLogicalEndpointsTest::RunTest(const FString& parameters)
{
	const FTransform lowerTransform = FTransform::Identity;
	const FTransform upperTransform(
		FRotator(0.0f, 180.0f, 0.0f),
		FVector(0.0f, 0.0f, 2400.0f));
	const FVector inputLocation = upperTransform.TransformPosition(FVector(128.0f, 0.0f, 0.0f));
	const FVector outputLocation = lowerTransform.TransformPosition(FVector(128.0f, 0.0f, 0.0f));

	const FConveyorSignsEndpointTransforms endpoints = FConveyorSignsEndpoints::ResolveTransforms(
		lowerTransform,
		upperTransform,
		inputLocation,
		outputLocation);

	TestTrue(TEXT("Input uses the endpoint nearest the input connector"), endpoints.Input.Equals(upperTransform));
	TestTrue(TEXT("Output uses the endpoint nearest the output connector"), endpoints.Output.Equals(lowerTransform));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsPreservesFaceAcrossEndpointsTest,
	"ConveyorSigns.Endpoints.PreservesFaceRelativeToEachConnector",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsPreservesFaceAcrossEndpointsTest::RunTest(const FString& parameters)
{
	const FTransform inputTransform(
		FRotator(0.0f, 180.0f, 0.0f),
		FVector(0.0f, 0.0f, 2400.0f));
	const FTransform outputTransform(
		FRotator(0.0f, 90.0f, 0.0f),
		FVector(1200.0f, -300.0f, 0.0f));
	const FTransform inputLeft = FConveyorSignsAttachmentLayout::CreateFaceTransform(
		inputTransform,
		EConveyorSignsFace::Left);
	const FTransform outputLeft = FConveyorSignsAttachmentLayout::CreateFaceTransform(
		outputTransform,
		EConveyorSignsFace::Left);

	TestTrue(
		TEXT("Input Left is left in the input connector frame"),
		inputLeft.GetTranslation().Equals(
			inputTransform.TransformPosition(FVector(0.0f, -120.0f, 0.0f)),
			KINDA_SMALL_NUMBER));
	TestTrue(
		TEXT("Output Left is left in the output connector frame"),
		outputLeft.GetTranslation().Equals(
			outputTransform.TransformPosition(FVector(0.0f, -120.0f, 0.0f)),
			KINDA_SMALL_NUMBER));
	TestFalse(
		TEXT("Rotated endpoint Left is not copied as the same world-space offset"),
		(inputLeft.GetTranslation() - inputTransform.GetTranslation()).Equals(
			outputLeft.GetTranslation() - outputTransform.GetTranslation(),
			KINDA_SMALL_NUMBER));
	return true;
}

#endif
