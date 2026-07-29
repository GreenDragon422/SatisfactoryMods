#include "BeltPurgeService.h"

#include "BeltPurgeLog.h"
#include "BeltPurgeRemoteCallObject.h"
#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableConveyorBelt.h"
#include "Buildables/FGBuildableGeneratorFuel.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "FGConveyorChainActor.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "FGPlayerController.h"
#include "HAL/PlatformTime.h"

struct FBeltPurgeNetwork
{
	TSet<AFGBuildableConveyorBelt*> Belts;
	TSet<AFGBuildableConveyorAttachment*> Attachments;
	TSet<UFGInventoryComponent*> InputInventories;
};

struct FBeltPurgeOperation
{
	static constexpr int32 MaximumItemsPerTick = 256;

	uint64 OperationId = 0;
	TWeakObjectPtr<UWorld> World;
	TArray<TWeakObjectPtr<AFGConveyorChainActor>> Chains;
	TArray<int32> InitialChainItemCounts;
	int32 CurrentChainIndex = 0;
	int32 InitialConveyorItems = 0;
	int32 InitialInventoryItems = 0;
	int32 RemovedConveyorItems = 0;
	int32 RemovedInventoryItems = 0;
	int32 ClearedAttachmentBuffers = 0;
	int32 ClearedInputInventories = 0;
	int32 SkippedChains = 0;
	double StartTimeSeconds = 0.0;

	bool Tick(float deltaTime);
	void Finish(bool succeeded, const TCHAR* reason) const;
};

namespace
{
	uint64 NextOperationId = 1;

	struct FBeltPurgeAttachmentLink
	{
		AFGBuildableConveyorAttachment* Attachment = nullptr;
		UFGFactoryConnectionComponent* ConnectionOnAttachment = nullptr;
	};

	AFGConveyorChainActor* GetBeltChain(AFGBuildableConveyorBelt* belt)
	{
		return belt != nullptr ? belt->GetConveyorChainActor() : nullptr;
	}

	FBeltPurgeAttachmentLink GetAttachmentLink(
		UFGFactoryConnectionComponent* beltConnection,
		AFGBuildableConveyorBelt* sourceBelt)
	{
		FBeltPurgeAttachmentLink link;
		if (beltConnection == nullptr)
		{
			return link;
		}

		UFGFactoryConnectionComponent* connectedConnection = beltConnection->GetConnection();
		if (connectedConnection == nullptr)
		{
			return link;
		}

		AActor* owner = connectedConnection->GetOwner();
		if (owner == nullptr || owner == sourceBelt)
		{
			return link;
		}

		link.Attachment = Cast<AFGBuildableConveyorAttachment>(owner);
		link.ConnectionOnAttachment = connectedConnection;
		return link;
	}

	UFGInventoryComponent* GetInputInventory(
		UFGFactoryConnectionComponent* beltConnection,
		AFGBuildableConveyorBelt* sourceBelt)
	{
		if (beltConnection == nullptr)
		{
			return nullptr;
		}

		UFGFactoryConnectionComponent* connectedConnection = beltConnection->GetConnection();
		if (connectedConnection == nullptr ||
			connectedConnection->GetDirection() != EFactoryConnectionDirection::FCD_INPUT)
		{
			return nullptr;
		}

		AActor* owner = connectedConnection->GetOwner();
		if (owner == nullptr || owner == sourceBelt)
		{
			return nullptr;
		}

		if (AFGBuildableManufacturer* manufacturer = Cast<AFGBuildableManufacturer>(owner))
		{
			return manufacturer->GetInputInventory();
		}

		if (AFGBuildableGeneratorFuel* generator = Cast<AFGBuildableGeneratorFuel>(owner))
		{
			return generator->GetFuelInventory();
		}

		return nullptr;
	}

	TArray<AFGBuildableConveyorBelt*> GetLinkedBelts(
		AFGBuildableConveyorAttachment* attachment,
		UFGFactoryConnectionComponent* connectionToSkip)
	{
		TArray<AFGBuildableConveyorBelt*> linkedBelts;
		if (attachment == nullptr)
		{
			return linkedBelts;
		}

		TInlineComponentArray<UFGFactoryConnectionComponent*> attachmentConnections;
		attachment->GetComponents(attachmentConnections);

		for (UFGFactoryConnectionComponent* attachmentConnection : attachmentConnections)
		{
			if (attachmentConnection == nullptr || attachmentConnection == connectionToSkip)
			{
				continue;
			}

			UFGFactoryConnectionComponent* connectedConnection =
				attachmentConnection->GetConnection();
			if (connectedConnection == nullptr)
			{
				continue;
			}

			if (AFGBuildableConveyorBelt* linkedBelt =
				Cast<AFGBuildableConveyorBelt>(connectedConnection->GetOwner()))
			{
				linkedBelts.Add(linkedBelt);
			}
		}

		return linkedBelts;
	}

}

bool FBeltPurgeOperation::Tick(float deltaTime)
{
	(void)deltaTime;

	if (!World.IsValid())
	{
		Finish(false, TEXT("world became invalid"));
		return false;
	}

	if (CurrentChainIndex >= Chains.Num())
	{
		Finish(true, TEXT("complete"));
		return false;
	}

	AFGConveyorChainActor* chain = Chains[CurrentChainIndex].Get();
	if (chain == nullptr)
	{
		++SkippedChains;
		UE_LOG(
			LogBeltPurge,
			Warning,
			TEXT("[BeltPurge:%llu] Progress chain=%d/%d skipped because it no longer exists."),
			OperationId,
			CurrentChainIndex + 1,
			Chains.Num());
		++CurrentChainIndex;
		return true;
	}

	const int32 beforeCount = chain->GetNumActualItems();
	const int32 removedThisTick =
		UBeltPurgeService::EmptyChainBatch(chain, MaximumItemsPerTick);
	const int32 afterCount = chain->GetNumActualItems();
	RemovedConveyorItems += removedThisTick;

	if (beforeCount > 0 && removedThisTick == 0 && afterCount >= beforeCount)
	{
		UE_LOG(
			LogBeltPurge,
			Error,
			TEXT("[BeltPurge:%llu] Progress stalled at chain=%d/%d itemsBefore=%d itemsAfter=%d. Aborting instead of blocking the game thread."),
			OperationId,
			CurrentChainIndex + 1,
			Chains.Num(),
			beforeCount,
			afterCount);
		Finish(false, TEXT("conveyor removal made no progress"));
		return false;
	}

	if (afterCount > 0)
	{
		return true;
	}

	UBeltPurgeService::FinalizeChain(chain);
	const int32 initialChainItems = InitialChainItemCounts.IsValidIndex(CurrentChainIndex)
		? InitialChainItemCounts[CurrentChainIndex]
		: removedThisTick;
	const int32 remainingConveyorItems =
		FMath::Max(0, InitialConveyorItems - RemovedConveyorItems);
	UE_LOG(
		LogBeltPurge,
		Display,
		TEXT("[BeltPurge:%llu] Progress chain=%d/%d segments=%d chainItemsRemoved=%d conveyorItemsRemoved=%d inventoryItemsRemoved=%d conveyorItemsRemaining=%d."),
		OperationId,
		CurrentChainIndex + 1,
		Chains.Num(),
		chain->GetChainSegments().Num(),
		initialChainItems,
		RemovedConveyorItems,
		RemovedInventoryItems,
		remainingConveyorItems);

	++CurrentChainIndex;
	return true;
}

void FBeltPurgeOperation::Finish(bool succeeded, const TCHAR* reason) const
{
	const double elapsedMilliseconds =
		(FPlatformTime::Seconds() - StartTimeSeconds) * 1000.0;
	if (succeeded)
	{
		UE_LOG(
			LogBeltPurge,
			Display,
			TEXT("[BeltPurge:%llu] End status=success reason=\"%s\" chains=%d processedChains=%d skippedChains=%d attachmentBuffers=%d inputInventories=%d conveyorItemsDiscovered=%d inventoryItemsDiscovered=%d conveyorItemsRemoved=%d inventoryItemsRemoved=%d totalItemsDestroyed=%d elapsedMs=%.2f."),
			OperationId,
			reason,
			Chains.Num(),
			CurrentChainIndex,
			SkippedChains,
			ClearedAttachmentBuffers,
			ClearedInputInventories,
			InitialConveyorItems,
			InitialInventoryItems,
			RemovedConveyorItems,
			RemovedInventoryItems,
			RemovedConveyorItems + RemovedInventoryItems,
			elapsedMilliseconds);
		return;
	}

	UE_LOG(
		LogBeltPurge,
		Error,
		TEXT("[BeltPurge:%llu] End status=failed reason=\"%s\" chains=%d processedChains=%d skippedChains=%d attachmentBuffers=%d inputInventories=%d conveyorItemsDiscovered=%d inventoryItemsDiscovered=%d conveyorItemsRemoved=%d inventoryItemsRemoved=%d totalItemsDestroyed=%d elapsedMs=%.2f."),
		OperationId,
		reason,
		Chains.Num(),
		CurrentChainIndex,
		SkippedChains,
		ClearedAttachmentBuffers,
		ClearedInputInventories,
		InitialConveyorItems,
		InitialInventoryItems,
		RemovedConveyorItems,
		RemovedInventoryItems,
		RemovedConveyorItems + RemovedInventoryItems,
		elapsedMilliseconds);
}

void UBeltPurgeService::GatherNetworkRecursive(
	AFGBuildableConveyorBelt* belt,
	FBeltPurgeNetwork& network)
{
	if (belt == nullptr || network.Belts.Contains(belt))
	{
		return;
	}

	AFGConveyorChainActor* chain = GetBeltChain(belt);
	if (chain == nullptr)
	{
		return;
	}

	network.Belts.Add(belt);

	auto visitChainConnection =
		[belt, &network](UFGFactoryConnectionComponent* beltConnection)
		{
			if (UFGInventoryComponent* inputInventory =
				GetInputInventory(beltConnection, belt))
			{
				network.InputInventories.Add(inputInventory);
				return;
			}

			const FBeltPurgeAttachmentLink link =
				GetAttachmentLink(beltConnection, belt);
			if (link.Attachment == nullptr ||
				network.Attachments.Contains(link.Attachment))
			{
				return;
			}

			network.Attachments.Add(link.Attachment);
			const TArray<AFGBuildableConveyorBelt*> linkedBelts =
				GetLinkedBelts(link.Attachment, link.ConnectionOnAttachment);
			for (AFGBuildableConveyorBelt* linkedBelt : linkedBelts)
			{
				GatherNetworkRecursive(linkedBelt, network);
			}
		};

	visitChainConnection(chain->mConnection0);
	visitChainConnection(chain->mConnection1);
}

int32 UBeltPurgeService::CountInventoryItems(UFGInventoryComponent* inventory)
{
	if (inventory == nullptr)
	{
		return 0;
	}

	TArray<FInventoryStack> stacks;
	inventory->GetInventoryStacks(stacks);
	int32 itemCount = 0;
	for (const FInventoryStack& stack : stacks)
	{
		itemCount += stack.NumItems;
	}
	return itemCount;
}

int32 UBeltPurgeService::EmptyAttachment(AFGBuildableConveyorAttachment* attachment)
{
	if (attachment == nullptr)
	{
		return 0;
	}

	if (UFGInventoryComponent* bufferInventory = attachment->GetBufferInventory())
	{
		const int32 itemCount = CountInventoryItems(bufferInventory);
		bufferInventory->Empty();
		return itemCount;
	}
	return 0;
}

int32 UBeltPurgeService::EmptyChainBatch(
	AFGConveyorChainActor* chain,
	int32 maximumItems)
{
	if (chain == nullptr || maximumItems <= 0)
	{
		return 0;
	}

	int32 removedItems = 0;
	while (removedItems < maximumItems && chain->mLeadItemIndex != INDEX_NONE)
	{
		const int32 beforeCount = chain->GetNumActualItems();
		const int32 beforeLeadIndex = chain->mLeadItemIndex;
		chain->RemoveItemAt_Internal(chain->mLeadItemIndex);
		const int32 afterCount = chain->GetNumActualItems();
		if (afterCount >= beforeCount && chain->mLeadItemIndex == beforeLeadIndex)
		{
			break;
		}
		++removedItems;
	}
	return removedItems;
}

void UBeltPurgeService::FinalizeChain(AFGConveyorChainActor* chain)
{
	if (chain == nullptr)
	{
		return;
	}

	chain->mCachedAvailableBeltSpace = chain->GetAvailableSpace();
	for (const FConveyorChainSplineSegment& segment : chain->GetChainSegments())
	{
		if (segment.ConveyorBase != nullptr)
		{
			segment.ConveyorBase->mCachedAvailableBeltSpace =
				segment.ConveyorBase->GetAvailableSpace();
		}
	}
	chain->ForceNetUpdate();
}

bool UBeltPurgeService::RequestPurgeFromAim(AFGPlayerController* controller)
{
	if (controller == nullptr)
	{
		return false;
	}

	UE_LOG(
		LogBeltPurge,
		Display,
		TEXT("[BeltPurge] Start request controller=%s authority=%s."),
		*GetNameSafe(controller),
		controller->HasAuthority() ? TEXT("true") : TEXT("false"));

	FVector viewLocation;
	FRotator viewRotation;
	controller->GetPlayerViewPoint(viewLocation, viewRotation);

	FHitResult hitResult;
	FCollisionQueryParams queryParameters(SCENE_QUERY_STAT(BeltPurgeAim), true);
	queryParameters.AddIgnoredActor(controller->GetPawn());

	UWorld* world = controller->GetWorld();
	const FVector traceEnd = viewLocation + viewRotation.Vector() * 20000.0;
	const bool hit = world != nullptr &&
		world->LineTraceSingleByChannel(
			hitResult,
			viewLocation,
			traceEnd,
			ECC_Visibility,
			queryParameters);

	AFGBuildableConveyorBelt* belt =
		hit ? Cast<AFGBuildableConveyorBelt>(hitResult.GetActor()) : nullptr;
	if (belt == nullptr)
	{
		controller->ClientMessage(TEXT("Belt Purge: aim at a conveyor belt."));
		return false;
	}

	if (controller->HasAuthority())
	{
		PurgeNetwork(belt);
		controller->ClientMessage(TEXT("Belt Purge started."));
		return true;
	}

	UBeltPurgeRemoteCallObject* remoteCallObject =
		controller->GetRemoteCallObjectOfClass<UBeltPurgeRemoteCallObject>();
	if (remoteCallObject == nullptr)
	{
		controller->ClientMessage(TEXT("Belt Purge: server connection is not ready."));
		return false;
	}

	remoteCallObject->ServerPurgeNetwork(belt);
	controller->ClientMessage(TEXT("Belt Purge requested."));
	return true;
}

void UBeltPurgeService::PurgeNetwork(AFGBuildableConveyorBelt* startingBelt)
{
	if (startingBelt == nullptr || !startingBelt->HasAuthority())
	{
		UE_LOG(
			LogBeltPurge,
			Warning,
			TEXT("[BeltPurge] Start rejected belt=%s authority=%s."),
			*GetNameSafe(startingBelt),
			startingBelt != nullptr && startingBelt->HasAuthority()
				? TEXT("true")
				: TEXT("false"));
		return;
	}

	const uint64 operationId = NextOperationId++;
	UE_LOG(
		LogBeltPurge,
		Display,
		TEXT("[BeltPurge:%llu] Start belt=%s."),
		operationId,
		*GetNameSafe(startingBelt));

	const double operationStartTimeSeconds = FPlatformTime::Seconds();
	FBeltPurgeNetwork network;
	GatherNetworkRecursive(startingBelt, network);

	TSharedRef<FBeltPurgeOperation> operation = MakeShared<FBeltPurgeOperation>();
	operation->OperationId = operationId;
	operation->World = startingBelt->GetWorld();
	operation->StartTimeSeconds = operationStartTimeSeconds;

	TSet<UFGInventoryComponent*> clearedInventories;
	for (UFGInventoryComponent* inputInventory : network.InputInventories)
	{
		if (inputInventory != nullptr && !clearedInventories.Contains(inputInventory))
		{
			operation->RemovedInventoryItems += CountInventoryItems(inputInventory);
			inputInventory->Empty();
			clearedInventories.Add(inputInventory);
			++operation->ClearedInputInventories;
		}
	}

	for (AFGBuildableConveyorAttachment* attachment : network.Attachments)
	{
		if (attachment == nullptr)
		{
			continue;
		}

		UFGInventoryComponent* bufferInventory = attachment->GetBufferInventory();
		if (bufferInventory != nullptr && !clearedInventories.Contains(bufferInventory))
		{
			operation->RemovedInventoryItems += EmptyAttachment(attachment);
			clearedInventories.Add(bufferInventory);
			++operation->ClearedAttachmentBuffers;
		}
	}
	operation->InitialInventoryItems = operation->RemovedInventoryItems;

	TSet<AFGConveyorChainActor*> discoveredChains;
	int32 conveyorItemCount = 0;
	for (AFGBuildableConveyorBelt* belt : network.Belts)
	{
		AFGConveyorChainActor* chain = GetBeltChain(belt);
		if (chain != nullptr && !discoveredChains.Contains(chain))
		{
			discoveredChains.Add(chain);
			const int32 chainItemCount = chain->GetNumActualItems();
			conveyorItemCount += chainItemCount;
			operation->Chains.Add(chain);
			operation->InitialChainItemCounts.Add(chainItemCount);
		}
	}
	operation->InitialConveyorItems = conveyorItemCount;

	UE_LOG(
		LogBeltPurge,
		Display,
		TEXT("[BeltPurge:%llu] Discovery belts=%d chains=%d attachmentBuffers=%d inputInventories=%d conveyorItems=%d inventoryItems=%d totalItems=%d."),
		operationId,
		network.Belts.Num(),
		operation->Chains.Num(),
		operation->ClearedAttachmentBuffers,
		operation->ClearedInputInventories,
		conveyorItemCount,
		operation->RemovedInventoryItems,
		conveyorItemCount + operation->RemovedInventoryItems);

	if (operation->Chains.IsEmpty())
	{
		operation->Finish(true, TEXT("no conveyor chains to process"));
		return;
	}

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[operation](float deltaTime)
			{
				return operation->Tick(deltaTime);
			}));
}
