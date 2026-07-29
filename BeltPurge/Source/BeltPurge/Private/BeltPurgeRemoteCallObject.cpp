#include "BeltPurgeRemoteCallObject.h"

#include "BeltPurgeService.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Net/UnrealNetwork.h"

void UBeltPurgeRemoteCallObject::GetLifetimeReplicatedProps(
	TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBeltPurgeRemoteCallObject, forceReplication);
}

void UBeltPurgeRemoteCallObject::ServerPurgeNetwork_Implementation(
	AFGBuildableConveyorBelt* belt)
{
	UBeltPurgeService::PurgeNetwork(belt);
}
