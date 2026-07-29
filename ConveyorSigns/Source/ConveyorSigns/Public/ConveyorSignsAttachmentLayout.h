#pragma once

#include "CoreMinimal.h"

enum class EConveyorSignsFace : uint8
{
	Left,
	Right,
	Rear
};

class CONVEYORSIGNS_API FConveyorSignsAttachmentLayout
{
public:
	static constexpr int32 FaceCount = 3;
	static constexpr float HousingHalfDepth = 128.0f;
	static constexpr float HousingHalfWidth = 128.0f;
	static constexpr float InsetCentimeters = 8.0f;
	static constexpr float EndpointHalfDepth = HousingHalfDepth - InsetCentimeters;
	static constexpr float EndpointHalfWidth = HousingHalfWidth - InsetCentimeters;

	static TArray<FTransform> CreateEndpointTransforms(const FTransform& endpointTransform);
	static FTransform CreateFaceTransform(const FTransform& endpointTransform, EConveyorSignsFace face);

private:
	static FVector GetFaceOffset(EConveyorSignsFace face);
	static FRotator GetFaceRotation(EConveyorSignsFace face);
};
