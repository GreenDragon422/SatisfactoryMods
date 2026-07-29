#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "BeltPurgeService.generated.h"

class AFGBuildableConveyorBelt;
class AFGBuildableConveyorAttachment;
class AFGConveyorChainActor;
class AFGPlayerController;
struct FBeltPurgeNetwork;
struct FBeltPurgeOperation;

UCLASS()
class BELTPURGE_API UBeltPurgeService final : public UObject
{
	GENERATED_BODY()

public:
	static bool RequestPurgeFromAim(AFGPlayerController* controller);
	static void PurgeNetwork(AFGBuildableConveyorBelt* startingBelt);

private:
	friend struct FBeltPurgeOperation;

	static void GatherNetworkRecursive(
		AFGBuildableConveyorBelt* belt,
		FBeltPurgeNetwork& network);
	static int32 CountInventoryItems(class UFGInventoryComponent* inventory);
	static int32 EmptyAttachment(AFGBuildableConveyorAttachment* attachment);
	static int32 EmptyChainBatch(AFGConveyorChainActor* chain, int32 maximumItems);
	static void FinalizeChain(AFGConveyorChainActor* chain);
};
