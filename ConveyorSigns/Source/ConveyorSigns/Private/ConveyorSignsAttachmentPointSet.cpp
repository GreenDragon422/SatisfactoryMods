#include "ConveyorSignsAttachmentPointSet.h"

#include "ConveyorSignsAttachmentLayout.h"
#include "ConveyorSignsEndpoints.h"
#include "Buildables/FGBuildableConveyorLift.h"

void FConveyorSignsAttachmentPointSet::Update(
	AFGBuildableConveyorLift* conveyorLift,
	TSubclassOf<UFGAttachmentPointType> attachmentType)
{
	check(conveyorLift != nullptr);

	const FConveyorSignsEndpointTransforms endpoints = FConveyorSignsEndpoints::GetTransforms(conveyorLift);
	const TArray<FTransform> inputTransforms =
		FConveyorSignsAttachmentLayout::CreateEndpointTransforms(endpoints.Input);

	check(inputTransforms.Num() == FConveyorSignsAttachmentLayout::FaceCount);

	for (int32 faceIndex = 0; faceIndex < FConveyorSignsAttachmentLayout::FaceCount; ++faceIndex)
	{
		FFGAttachmentPoint& attachmentPoint = attachmentPoints[faceIndex];
		attachmentPoint.RelativeTransform = inputTransforms[faceIndex];
		attachmentPoint.Type = attachmentType;
		attachmentPoint.Owner = conveyorLift;
	}
}

void FConveyorSignsAttachmentPointSet::AppendTo(TArray<const FFGAttachmentPoint*>& outputPoints) const
{
	for (const FFGAttachmentPoint& attachmentPoint : attachmentPoints)
	{
		outputPoints.AddUnique(&attachmentPoint);
	}
}
