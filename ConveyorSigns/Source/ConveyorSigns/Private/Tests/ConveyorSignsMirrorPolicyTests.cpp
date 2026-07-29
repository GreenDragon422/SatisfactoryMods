#if WITH_DEV_AUTOMATION_TESTS

#include "ConveyorSignsAttachmentLayout.h"
#include "ConveyorSignsMirrorPolicy.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsMirrorsSameLogicalFaceTest,
	"ConveyorSigns.Mirrors.MapsInputLeftToOutputLeft",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsMirrorsSameLogicalFaceTest::RunTest(const FString& parameters)
{
	const FTransform liftTransform(
		FRotator(0.0f, 35.0f, 0.0f),
		FVector(800.0f, -200.0f, 100.0f));
	const FConveyorSignsEndpointTransforms endpoints = {
		FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(0.0f, 0.0f, 2400.0f)),
		FTransform(FRotator(0.0f, 90.0f, 0.0f), FVector(1200.0f, -300.0f, 0.0f))};
	FTransform sourceTransform =
		FConveyorSignsAttachmentLayout::CreateFaceTransform(endpoints.Input, EConveyorSignsFace::Left)
		* liftTransform;
	const FQuat sourceOrientationOffset(FRotator(0.0f, -90.0f, 0.0f));
	sourceTransform.SetRotation(sourceOrientationOffset * sourceTransform.GetRotation());
	FTransform expectedMirror =
		FConveyorSignsAttachmentLayout::CreateFaceTransform(endpoints.Output, EConveyorSignsFace::Left)
		* liftTransform;
	expectedMirror.SetRotation(sourceOrientationOffset * expectedMirror.GetRotation());
	FTransform mirrorTransform;

	const bool foundMirror = FConveyorSignsMirrorPolicy::TryGetMirrorTransform(
		sourceTransform,
		liftTransform,
		endpoints,
		mirrorTransform);

	TestTrue(TEXT("Input Left is recognized as a source position"), foundMirror);
	TestTrue(
		TEXT("The mirror transfers the placed sign's face-relative orientation to Output Left"),
		mirrorTransform.Equals(expectedMirror));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsRejectsUnrelatedSignsTest,
	"ConveyorSigns.Mirrors.RejectsSignsAwayFromInputFaces",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsRejectsUnrelatedSignsTest::RunTest(const FString& parameters)
{
	const FConveyorSignsEndpointTransforms endpoints = {
		FTransform::Identity,
		FTransform(FVector(0.0f, 0.0f, 2400.0f))};
	const FTransform unrelatedSign(FVector(5000.0f, 5000.0f, 5000.0f));
	FTransform mirrorTransform;

	TestFalse(
		TEXT("A normal sign elsewhere in the factory is not mirrored"),
		FConveyorSignsMirrorPolicy::TryGetMirrorTransform(
			unrelatedSign,
			FTransform::Identity,
			endpoints,
			mirrorTransform));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsMigratesOldFaceOffsetTest,
	"ConveyorSigns.Mirrors.MigratesExistingSignsToCloserFaceOffset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsMigratesOldFaceOffsetTest::RunTest(const FString& parameters)
{
	const FConveyorSignsEndpointTransforms endpoints = {
		FTransform::Identity,
		FTransform(FVector(0.0f, 0.0f, 2400.0f))};
	const FTransform oldSourceTransform(FVector(0.0f, -128.0f, 0.0f));
	const FTransform expectedSourceTransform(
		FRotator::ZeroRotator,
		FVector(0.0f, -120.0f, 0.0f));
	const FTransform expectedMirrorTransform(
		FRotator::ZeroRotator,
		FVector(0.0f, -120.0f, 2400.0f));
	FTransform mirrorTransform;
	FTransform canonicalSourceTransform;

	const bool foundMirror = FConveyorSignsMirrorPolicy::TryGetMirrorTransform(
		oldSourceTransform,
		FTransform::Identity,
		endpoints,
		mirrorTransform,
		&canonicalSourceTransform);

	TestTrue(TEXT("The previous 128 cm source position is recognized"), foundMirror);
	TestTrue(TEXT("The existing source moves to the new 120 cm face without changing its rotation"), canonicalSourceTransform.Equals(expectedSourceTransform));
	TestTrue(TEXT("The mirror uses the closer output face and preserves the source's face-relative rotation"), mirrorTransform.Equals(expectedMirrorTransform));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsDeletesOnlyGameplayPartnerTest,
	"ConveyorSigns.Mirrors.DeletesPartnerOnlyWhenExplicitlyDestroyed",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsDeletesOnlyGameplayPartnerTest::RunTest(const FString& parameters)
{
	TestTrue(
		TEXT("Explicit destruction deletes the paired sign"),
		FConveyorSignsMirrorPolicy::ShouldDeletePartner(EEndPlayReason::Destroyed));
	TestFalse(
		TEXT("World travel does not delete the paired sign"),
		FConveyorSignsMirrorPolicy::ShouldDeletePartner(EEndPlayReason::LevelTransition));
	TestFalse(
		TEXT("Removing a world does not delete the paired sign"),
		FConveyorSignsMirrorPolicy::ShouldDeletePartner(EEndPlayReason::RemovedFromWorld));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FConveyorSignsMirrorLifecycleTest,
	"ConveyorSigns.Mirrors.AreTransientSelectableAndDeleteWithLift",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FConveyorSignsMirrorLifecycleTest::RunTest(const FString& parameters)
{
	TStrongObjectPtr<AActor> mirror(NewObject<AActor>());
	mirror->Tags.Add(FConveyorSignsMirrorPolicy::GetMirrorTag());
	TStrongObjectPtr<AActor> source(NewObject<AActor>());

	TestTrue(TEXT("The generated mirror tag is recognized"), FConveyorSignsMirrorPolicy::IsMirror(mirror.Get()));
	TestFalse(TEXT("The placed source is not classified as a mirror"), FConveyorSignsMirrorPolicy::IsMirror(source.Get()));
	TestFalse(TEXT("Generated mirrors are excluded from saves"), FConveyorSignsMirrorPolicy::ShouldSaveMirror());
	TestFalse(TEXT("Generated mirrors do not open the sign configuration interaction"), FConveyorSignsMirrorPolicy::ShouldAllowMirrorUse());
	TestTrue(TEXT("Generated mirrors retain collision for dismantle highlighting"), FConveyorSignsMirrorPolicy::ShouldKeepMirrorCollision());
	TestTrue(
		TEXT("Dismantling the lift deletes its attached source and mirror"),
		FConveyorSignsMirrorPolicy::ShouldDeleteSignsWithLift(EEndPlayReason::Destroyed));
	TestFalse(
		TEXT("World travel does not trigger a gameplay deletion cascade"),
		FConveyorSignsMirrorPolicy::ShouldDeleteSignsWithLift(EEndPlayReason::LevelTransition));

	return true;
}

#endif
