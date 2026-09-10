#pragma once

#include "CoreMinimal.h"
#include "FGRemoteCallObject.h"

#include "BeltPurgeRemoteCallObject.generated.h"

class AFGBuildable;

UCLASS()
class BELTPURGE_API UBeltPurgeRemoteCallObject final : public UFGRemoteCallObject
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(Server, Reliable)
	void ServerPurgeNetwork(AFGBuildable* target);

private:
	UPROPERTY(Replicated)
	bool forceReplication = true;
};
