#pragma once

#include "CoreMinimal.h"

class AFGBuildableConveyorLift;
class UFGFactoryConnectionComponent;

struct FConveyorSignsEndpointTransforms
{
	FTransform Input;
	FTransform Output;
};

class CONVEYORSIGNS_API FConveyorSignsEndpoints
{
public:
	static UFGFactoryConnectionComponent* GetInputConnector(const AFGBuildableConveyorLift* conveyorLift);
	static UFGFactoryConnectionComponent* GetOutputConnector(const AFGBuildableConveyorLift* conveyorLift);
	static FConveyorSignsEndpointTransforms GetTransforms(const AFGBuildableConveyorLift* conveyorLift);

	static FConveyorSignsEndpointTransforms ResolveTransforms(
		const FTransform& lowerTransform,
		const FTransform& upperTransform,
		const FVector& inputLocation,
		const FVector& outputLocation);
};
