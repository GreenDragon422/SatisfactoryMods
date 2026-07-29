#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"

#include "BeltPurgeRemoteCallObject.generated.h"

class AFGBuildableConveyorBelt;

UCLASS()
class BELTPURGE_API UBeltPurgeRemoteCallObject final : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(Server, Reliable)
	void ServerPurgeNetwork(AFGBuildableConveyorBelt* belt);

private:
	UPROPERTY(Replicated)
	bool forceReplication = true;
};
