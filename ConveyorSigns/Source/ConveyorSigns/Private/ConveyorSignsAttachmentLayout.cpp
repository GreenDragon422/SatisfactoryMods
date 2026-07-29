#include "ConveyorSignsAttachmentLayout.h"

TArray<FTransform> FConveyorSignsAttachmentLayout::CreateEndpointTransforms(const FTransform& endpointTransform)
{
	TArray<FTransform> transforms;
	transforms.Reserve(FaceCount);
	transforms.Add(CreateFaceTransform(endpointTransform, EConveyorSignsFace::Left));
	transforms.Add(CreateFaceTransform(endpointTransform, EConveyorSignsFace::Right));
	transforms.Add(CreateFaceTransform(endpointTransform, EConveyorSignsFace::Rear));
	return transforms;
}

FTransform FConveyorSignsAttachmentLayout::CreateFaceTransform(
	const FTransform& endpointTransform,
	EConveyorSignsFace face)
{
	const FVector faceOffset = GetFaceOffset(face);
	const FRotator faceRotation = GetFaceRotation(face);
	return FTransform(faceRotation, faceOffset) * endpointTransform;
}

FVector FConveyorSignsAttachmentLayout::GetFaceOffset(EConveyorSignsFace face)
{
	switch (face)
	{
	case EConveyorSignsFace::Left:
		return FVector(0.0, -EndpointHalfWidth, 0.0);
	case EConveyorSignsFace::Right:
		return FVector(0.0, EndpointHalfWidth, 0.0);
	case EConveyorSignsFace::Rear:
		return FVector(-EndpointHalfDepth, 0.0, 0.0);
	default:
		checkNoEntry();
		return FVector::ZeroVector;
	}
}

FRotator FConveyorSignsAttachmentLayout::GetFaceRotation(EConveyorSignsFace face)
{
	switch (face)
	{
	case EConveyorSignsFace::Left:
		return FRotator(0.0, -90.0, 0.0);
	case EConveyorSignsFace::Right:
		return FRotator(0.0, 90.0, 0.0);
	case EConveyorSignsFace::Rear:
		return FRotator(0.0, 180.0, 0.0);
	default:
		checkNoEntry();
		return FRotator::ZeroRotator;
	}
}
