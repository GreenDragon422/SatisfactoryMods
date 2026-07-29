#if WITH_DEV_AUTOMATION_TESTS

#include "ConveyorSignsAttachmentLayout.h"
#include "Engine/StaticMesh.h"
#include "Misc/AutomationTest.h"

namespace ConveyorSignsAttachmentLayoutTests
{
	bool TestVector(
		FAutomationTestBase& test,
		const TCHAR* label,
		const FVector& actual,
		const FVector& expected)
	{
		return test.TestTrue(label, actual.Equals(expected, KINDA_SMALL_NUMBER));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsCreatesThreeFacesTest,
	"ConveyorSigns.AttachmentLayout.CreatesExactlyThreeExposedFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsCreatesThreeFacesTest::RunTest(const FString& parameters)
{
	const TArray<FTransform> transforms =
		FConveyorSignsAttachmentLayout::CreateEndpointTransforms(FTransform::Identity);

	TestEqual(TEXT("An endpoint has exactly three sign positions"), transforms.Num(), 3);
	if (transforms.Num() != 3)
	{
		return false;
	}

	ConveyorSignsAttachmentLayoutTests::TestVector(
		*this,
		TEXT("Left position is centered on the left face"),
		transforms[0].GetTranslation(),
		FVector(0.0f, -120.0f, 0.0f));
	ConveyorSignsAttachmentLayoutTests::TestVector(
		*this,
		TEXT("Right position is centered on the right face"),
		transforms[1].GetTranslation(),
		FVector(0.0f, 120.0f, 0.0f));
	ConveyorSignsAttachmentLayoutTests::TestVector(
		*this,
		TEXT("Rear position is opposite the conveyor connection"),
		transforms[2].GetTranslation(),
		FVector(-120.0f, 0.0f, 0.0f));

	for (const FTransform& transform : transforms)
	{
		TestEqual(TEXT("No position is placed on the horizontal top"), transform.GetTranslation().Z, 0.0);
		TestFalse(
			TEXT("No position is placed on the forward conveyor-connection face"),
			transform.GetTranslation().Equals(FVector(128.0f, 0.0f, 0.0f), KINDA_SMALL_NUMBER));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsFollowsEndpointTransformTest,
	"ConveyorSigns.AttachmentLayout.FollowsEndpointTranslationAndRotation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsFollowsEndpointTransformTest::RunTest(const FString& parameters)
{
	const FTransform endpointTransform(
		FRotator(0.0f, 90.0f, 0.0f),
		FVector(1200.0f, -340.0f, 800.0f));
	const TArray<FTransform> transforms =
		FConveyorSignsAttachmentLayout::CreateEndpointTransforms(endpointTransform);

	if (transforms.Num() != 3)
	{
		AddError(TEXT("Expected three transforms before checking endpoint propagation."));
		return false;
	}

	ConveyorSignsAttachmentLayoutTests::TestVector(
		*this,
		TEXT("Left point follows endpoint translation and rotation"),
		transforms[0].GetTranslation(),
		endpointTransform.TransformPosition(FVector(0.0f, -120.0f, 0.0f)));
	ConveyorSignsAttachmentLayoutTests::TestVector(
		*this,
		TEXT("Right point follows endpoint translation and rotation"),
		transforms[1].GetTranslation(),
		endpointTransform.TransformPosition(FVector(0.0f, 120.0f, 0.0f)));
	ConveyorSignsAttachmentLayoutTests::TestVector(
		*this,
		TEXT("Rear point follows endpoint translation and rotation"),
		transforms[2].GetTranslation(),
		endpointTransform.TransformPosition(FVector(-120.0f, 0.0f, 0.0f)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsSupportsReversedUpperEndpointTest,
	"ConveyorSigns.AttachmentLayout.SupportsReversedAndRotatedUpperEndpoints",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsSupportsReversedUpperEndpointTest::RunTest(const FString& parameters)
{
	const FTransform upperEndpointTransform(
		FRotator(0.0f, 180.0f, 0.0f),
		FVector(0.0f, 0.0f, 2400.0f));
	const TArray<FTransform> transforms =
		FConveyorSignsAttachmentLayout::CreateEndpointTransforms(upperEndpointTransform);

	if (transforms.Num() != 3)
	{
		AddError(TEXT("Expected three transforms before checking reversed placement."));
		return false;
	}

	ConveyorSignsAttachmentLayoutTests::TestVector(
		*this,
		TEXT("Reversed upper rear remains opposite its connection face"),
		transforms[2].GetTranslation(),
		upperEndpointTransform.TransformPosition(FVector(-120.0f, 0.0f, 0.0f)));
	TestTrue(
		TEXT("Upper face rotations include the endpoint rotation"),
		transforms[2].GetRotation().Equals(
			(FQuat(FRotator(0.0f, 180.0f, 0.0f)) * upperEndpointTransform.GetRotation()),
			KINDA_SMALL_NUMBER));

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsMatchesVanillaHousingBoundsTest,
	"ConveyorSigns.AttachmentLayout.MatchesVanillaEndpointHousingBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsMatchesVanillaHousingBoundsTest::RunTest(const FString& parameters)
{
	UStaticMesh* endpointMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/FactoryGame/Buildable/Factory/ConveyorLiftMk1/Mesh/SM_ConveyorLift_Bottom_01.SM_ConveyorLift_Bottom_01"));

	TestNotNull(TEXT("The vanilla Conveyor Lift endpoint mesh loads"), endpointMesh);
	if (endpointMesh == nullptr)
	{
		return false;
	}

	const FVector boxExtent = endpointMesh->GetBounds().BoxExtent;
	TestEqual(
		TEXT("Face depth is inset eight centimeters from the endpoint mesh X extent"),
		FConveyorSignsAttachmentLayout::EndpointHalfDepth,
		static_cast<float>(boxExtent.X) - FConveyorSignsAttachmentLayout::InsetCentimeters);
	TestEqual(
		TEXT("Face width is inset eight centimeters from the endpoint mesh Y extent"),
		FConveyorSignsAttachmentLayout::EndpointHalfWidth,
		static_cast<float>(boxExtent.Y) - FConveyorSignsAttachmentLayout::InsetCentimeters);
	return true;
}

#endif
