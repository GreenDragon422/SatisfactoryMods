#include "ConveyorSignsEndpoints.h"

#include "Buildables/FGBuildableConveyorLift.h"
#include "FGFactoryConnectionComponent.h"

UFGFactoryConnectionComponent* FConveyorSignsEndpoints::GetInputConnector(
	const AFGBuildableConveyorLift* conveyorLift)
{
	check(conveyorLift != nullptr);
	return conveyorLift->GetConnection0();
}

UFGFactoryConnectionComponent* FConveyorSignsEndpoints::GetOutputConnector(
	const AFGBuildableConveyorLift* conveyorLift)
{
	check(conveyorLift != nullptr);
	return conveyorLift->GetConnection1();
}

FConveyorSignsEndpointTransforms FConveyorSignsEndpoints::GetTransforms(
	const AFGBuildableConveyorLift* conveyorLift)
{
	check(conveyorLift != nullptr);

	UFGFactoryConnectionComponent* inputConnector = GetInputConnector(conveyorLift);
	UFGFactoryConnectionComponent* outputConnector = GetOutputConnector(conveyorLift);
	check(inputConnector != nullptr);
	check(outputConnector != nullptr);

	const FTransform actorTransform = conveyorLift->GetActorTransform();
	const FVector inputLocation = actorTransform.InverseTransformPosition(
		inputConnector->GetConnectorLocation());
	const FVector outputLocation = actorTransform.InverseTransformPosition(
		outputConnector->GetConnectorLocation());

	return ResolveTransforms(
		FTransform::Identity,
		conveyorLift->GetTopTransform(),
		inputLocation,
		outputLocation);
}

FConveyorSignsEndpointTransforms FConveyorSignsEndpoints::ResolveTransforms(
	const FTransform& lowerTransform,
	const FTransform& upperTransform,
	const FVector& inputLocation,
	const FVector& outputLocation)
{
	const double lowerIsInputDistance = FVector::DistSquared(inputLocation, lowerTransform.GetTranslation())
		+ FVector::DistSquared(outputLocation, upperTransform.GetTranslation());
	const double upperIsInputDistance = FVector::DistSquared(inputLocation, upperTransform.GetTranslation())
		+ FVector::DistSquared(outputLocation, lowerTransform.GetTranslation());

	if (lowerIsInputDistance <= upperIsInputDistance)
	{
		return {lowerTransform, upperTransform};
	}

	return {upperTransform, lowerTransform};
}
