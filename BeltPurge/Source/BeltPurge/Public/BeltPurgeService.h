#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"

#include "BeltPurgeService.generated.h"

class AFGBuildable;
class AFGBuildableConveyorBase;
class AFGBuildableConveyorAttachment;
class AFGConveyorChainActor;
class AFGPlayerController;
class AFGResourceSinkSubsystem;
class UFGItemDescriptor;
struct FBeltPurgeNetwork;
struct FBeltPurgeOperation;
struct FBeltPurgeRemovalResult;

UCLASS()
class BELTPURGE_API UBeltPurgeService final : public UObject
{
	GENERATED_BODY()

public:
	static bool RequestPurgeFromAim(AFGPlayerController* controller);
	static void PurgeNetwork(AFGBuildable* startTarget);

private:
	friend struct FBeltPurgeOperation;

	static void GatherNetworkRecursive(
		AFGBuildableConveyorBase* conveyor,
		FBeltPurgeNetwork& network);
	static void GatherAttachmentRecursive(
		AFGBuildableConveyorAttachment* attachment,
		class UFGFactoryConnectionComponent* entryConnection,
		FBeltPurgeNetwork& network);
	static bool IsEligibleForPurge(
		TSubclassOf<UFGItemDescriptor> itemClass,
		AFGResourceSinkSubsystem* sinkSubsystem);
	static FBeltPurgeRemovalResult RemoveEligibleInventoryItems(
		class UFGInventoryComponent* inventory,
		int32 maximumItems,
		AFGResourceSinkSubsystem* sinkSubsystem);
	static FBeltPurgeRemovalResult EmptyChainBatch(
		AFGConveyorChainActor* chain,
		int32 maximumItems,
		AFGResourceSinkSubsystem* sinkSubsystem);
};
