#include "ConveyorSignsMirrorPolicy.h"

#include "ConveyorSignsAttachmentLayout.h"
#include "GameFramework/Actor.h"

namespace ConveyorSignsMirrorPolicyConstants
{
	const FName MirrorTag(TEXT("ConveyorSigns.Mirror"));
}

FName FConveyorSignsMirrorPolicy::GetMirrorTag()
{
	return ConveyorSignsMirrorPolicyConstants::MirrorTag;
}

bool FConveyorSignsMirrorPolicy::IsMirror(const AActor* actor)
{
	return IsValid(actor) && actor->Tags.Contains(GetMirrorTag());
}

bool FConveyorSignsMirrorPolicy::ShouldSaveMirror()
{
	return false;
}

bool FConveyorSignsMirrorPolicy::ShouldAllowMirrorUse()
{
	return false;
}

bool FConveyorSignsMirrorPolicy::ShouldKeepMirrorCollision()
{
	return true;
}

bool FConveyorSignsMirrorPolicy::TryGetMirrorTransform(
	const FTransform& sourceTransform,
	const FTransform& liftTransform,
	const FConveyorSignsEndpointTransforms& endpoints,
	FTransform& mirrorTransform,
	FTransform* canonicalSourceTransform)
{
	const EConveyorSignsFace faces[] = {
		EConveyorSignsFace::Left,
		EConveyorSignsFace::Right,
		EConveyorSignsFace::Rear};
	double closestDistance = TNumericLimits<double>::Max();
	EConveyorSignsFace closestFace = EConveyorSignsFace::Left;

	for (EConveyorSignsFace face : faces)
	{
		const FTransform faceTransform =
			FConveyorSignsAttachmentLayout::CreateFaceTransform(endpoints.Input, face)
			* liftTransform;
		const double faceDistance = FVector::Dist(
			sourceTransform.GetTranslation(),
			faceTransform.GetTranslation());
		if (faceDistance < closestDistance)
		{
			closestDistance = faceDistance;
			closestFace = face;
		}
	}

	if (closestDistance > MaxSourceDistance)
	{
		return false;
	}

	const FTransform inputFaceTransform =
		FConveyorSignsAttachmentLayout::CreateFaceTransform(endpoints.Input, closestFace)
		* liftTransform;
	FTransform snappedSourceTransform = inputFaceTransform;
	snappedSourceTransform.SetRotation(sourceTransform.GetRotation());
	if (canonicalSourceTransform != nullptr)
	{
		*canonicalSourceTransform = snappedSourceTransform;
	}
	mirrorTransform =
		FConveyorSignsAttachmentLayout::CreateFaceTransform(endpoints.Output, closestFace)
		* liftTransform;
	const FQuat sourceFaceRelativeRotation =
		sourceTransform.GetRotation() * inputFaceTransform.GetRotation().Inverse();
	mirrorTransform.SetRotation(
		(sourceFaceRelativeRotation * mirrorTransform.GetRotation()).GetNormalized());
	return true;
}

bool FConveyorSignsMirrorPolicy::ShouldDeletePartner(EEndPlayReason::Type reason)
{
	return reason == EEndPlayReason::Destroyed;
}

bool FConveyorSignsMirrorPolicy::ShouldDeleteSignsWithLift(EEndPlayReason::Type reason)
{
	return reason == EEndPlayReason::Destroyed;
}
