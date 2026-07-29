#pragma once

#include "Containers/StaticArray.h"
#include "ConveyorSignsAttachmentLayout.h"
#include "FGAttachmentPoint.h"

class AFGBuildableConveyorLift;

class CONVEYORSIGNS_API FConveyorSignsAttachmentPointSet
{
public:
	static constexpr int32 AttachmentPointCount = FConveyorSignsAttachmentLayout::FaceCount;

	void Update(
		AFGBuildableConveyorLift* conveyorLift,
		TSubclassOf<UFGAttachmentPointType> attachmentType);
	void AppendTo(TArray<const FFGAttachmentPoint*>& outputPoints) const;

private:
	TStaticArray<FFGAttachmentPoint, AttachmentPointCount> attachmentPoints;
};
