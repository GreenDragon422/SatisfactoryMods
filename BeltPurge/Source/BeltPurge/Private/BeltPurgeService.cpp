#include "BeltPurgeService.h"

#include "BeltPurgeLog.h"
#include "BeltPurgeRemoteCallObject.h"
#include "AbstractInstanceManager.h"
#include "Buildables/FGBuildableConveyorAttachment.h"
#include "Buildables/FGBuildableConveyorBase.h"
#include "Buildables/FGBuildableGeneratorFuel.h"
#include "Buildables/FGBuildableManufacturer.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "FGConveyorChainActor.h"
#include "FGFactoryConnectionComponent.h"
#include "FGInventoryComponent.h"
#include "FGResourceSinkSubsystem.h"
#include "FGPlayerController.h"
#include "Resources/FGItemDescriptor.h"
#include "HAL/PlatformTime.h"

struct FBeltPurgeNetwork
{
	TSet<AFGBuildableConveyorBase*> Conveyors;
	TSet<AFGBuildableConveyorAttachment*> Attachments;
	TSet<UFGInventoryComponent*> InputInventories;
};

struct FBeltPurgeRemovalResult
{
	int32 DiscoveredItems = 0;
	int32 RemovedItems = 0;
	int32 ProtectedItems = 0;
	int32 RemainingItems = 0;
	bool bStoppedAtProtected = false;
	bool bMutationFailed = false;
};

struct FBeltPurgeOperation
{
	static constexpr int32 MaximumItemsPerTick = 256;

	uint64 OperationId = 0;
	TWeakObjectPtr<UWorld> World;
	TWeakObjectPtr<AFGResourceSinkSubsystem> SinkSubsystem;
	TArray<TWeakObjectPtr<AFGConveyorChainActor>> Chains;
	TArray<TWeakObjectPtr<UFGInventoryComponent>> Inventories;
	TSet<int32> StoppedChains;
	TSet<int32> VisitedChains;
	int32 CurrentInventoryIndex = 0;
	int32 CurrentChainIndex = 0;
	int32 RemovedThisPass = 0;
	int32 InitialConveyorItems = 0;
	int32 InitialInventoryItems = 0;
	int32 RemovedConveyorItems = 0;
	int32 RemovedInventoryItems = 0;
	int32 ProcessedAttachmentBuffers = 0;
	int32 ProcessedInputInventories = 0;
	int32 ProtectedConveyorItems = 0;
	int32 SkippedChains = 0;
	double StartTimeSeconds = 0.0;

	bool Tick(float deltaTime);
	void Finish(bool succeeded, const TCHAR* reason) const;
	static int32 CountInventoryItems(UFGInventoryComponent* inventory);
};

namespace
{
	uint64 NextOperationId = 1;

	struct FBeltPurgeAttachmentLink
	{
		AFGBuildableConveyorAttachment* Attachment = nullptr;
		UFGFactoryConnectionComponent* ConnectionOnAttachment = nullptr;
	};

	AFGConveyorChainActor* GetConveyorChain(AFGBuildableConveyorBase* conveyor)
	{
		if (!IsValid(conveyor))
		{
			return nullptr;
		}

		AFGConveyorChainActor* chain = conveyor->GetConveyorChainActor();
		return IsValid(chain) ? chain : nullptr;
	}

	FBeltPurgeAttachmentLink GetAttachmentLink(
		UFGFactoryConnectionComponent* beltConnection,
		AFGBuildableConveyorBase* sourceConveyor)
	{
		FBeltPurgeAttachmentLink link;
		if (!IsValid(beltConnection))
		{
			return link;
		}

		UFGFactoryConnectionComponent* connectedConnection = beltConnection->GetConnection();
		if (!IsValid(connectedConnection))
		{
			return link;
		}

		AActor* owner = connectedConnection->GetOwner();
		if (!IsValid(owner) || owner == sourceConveyor)
		{
			return link;
		}

		link.Attachment = Cast<AFGBuildableConveyorAttachment>(owner);
		if (!IsValid(link.Attachment))
		{
			return FBeltPurgeAttachmentLink();
		}
		link.ConnectionOnAttachment = connectedConnection;
		return link;
	}

	UFGInventoryComponent* GetInputInventory(
		UFGFactoryConnectionComponent* beltConnection,
		AFGBuildableConveyorBase* sourceConveyor)
	{
		if (!IsValid(beltConnection))
		{
			return nullptr;
		}

		UFGFactoryConnectionComponent* connectedConnection = beltConnection->GetConnection();
		if (!IsValid(connectedConnection) ||
			connectedConnection->GetDirection() != EFactoryConnectionDirection::FCD_INPUT)
		{
			return nullptr;
		}

		AActor* owner = connectedConnection->GetOwner();
		if (!IsValid(owner) || owner == sourceConveyor)
		{
			return nullptr;
		}

		if (AFGBuildableManufacturer* manufacturer = Cast<AFGBuildableManufacturer>(owner))
		{
			UFGInventoryComponent* inventory = manufacturer->GetInputInventory();
			return IsValid(inventory) ? inventory : nullptr;
		}

		if (AFGBuildableGeneratorFuel* generator = Cast<AFGBuildableGeneratorFuel>(owner))
		{
			UFGInventoryComponent* inventory = generator->GetFuelInventory();
			return IsValid(inventory) ? inventory : nullptr;
		}

		return nullptr;
	}

	TArray<AFGBuildableConveyorBase*> GetLinkedConveyors(
		AFGBuildableConveyorAttachment* attachment,
		UFGFactoryConnectionComponent* connectionToSkip)
	{
		TArray<AFGBuildableConveyorBase*> linkedConveyors;
		if (!IsValid(attachment))
		{
			return linkedConveyors;
		}

		TInlineComponentArray<UFGFactoryConnectionComponent*> attachmentConnections;
		attachment->GetComponents(attachmentConnections);

		for (UFGFactoryConnectionComponent* attachmentConnection : attachmentConnections)
		{
			if (!IsValid(attachmentConnection) || attachmentConnection == connectionToSkip)
			{
				continue;
			}

			UFGFactoryConnectionComponent* connectedConnection =
				attachmentConnection->GetConnection();
			if (!IsValid(connectedConnection))
			{
				continue;
			}

			AFGBuildableConveyorBase* linkedConveyor =
				Cast<AFGBuildableConveyorBase>(connectedConnection->GetOwner());
			if (IsValid(linkedConveyor))
			{
				linkedConveyors.Add(linkedConveyor);
			}
		}

	return linkedConveyors;
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
	if (!SinkSubsystem.IsValid())
	{
		Finish(false, TEXT("resource sink subsystem became invalid"));
		return false;
	}

	// Drain several nodes in the same core-ticker callback. Empty visits also
	// consume work so large empty networks cannot monopolize a frame.
	int32 work = 0;
	while (work < MaximumItemsPerTick)
	{
		const int32 remainingBudget = InitialConveyorItems + InitialInventoryItems -
			RemovedConveyorItems - RemovedInventoryItems;
		if (remainingBudget <= 0)
		{
			Finish(true, TEXT("initial item budget exhausted"));
			return false;
		}
		const int32 batchLimit = FMath::Min(MaximumItemsPerTick - work, remainingBudget);
		if (CurrentInventoryIndex < Inventories.Num())
		{
			const FBeltPurgeRemovalResult batch = UBeltPurgeService::RemoveEligibleInventoryItems(
				Inventories[CurrentInventoryIndex].Get(), batchLimit, SinkSubsystem.Get());
			RemovedInventoryItems += batch.RemovedItems;
			RemovedThisPass += batch.RemovedItems;
			work += FMath::Max(1, batch.RemovedItems);
			if (batch.bMutationFailed)
			{
				Finish(false, TEXT("inventory mutation failed"));
				return false;
			}
			if (batch.RemainingItems <= batch.ProtectedItems)
			{
				++CurrentInventoryIndex;
			}
			continue;
		}
		if (CurrentChainIndex < Chains.Num())
		{
			if (StoppedChains.Contains(CurrentChainIndex))
			{
				++CurrentChainIndex;
				++work;
				continue;
			}
			AFGConveyorChainActor* chain = Chains[CurrentChainIndex].Get();
			if (chain == nullptr)
			{
				StoppedChains.Add(CurrentChainIndex++);
				++SkippedChains;
				++work;
				continue;
			}
			VisitedChains.Add(CurrentChainIndex);
			const int32 beforeCount = chain->GetNumActualItems();
			const FBeltPurgeRemovalResult batch = UBeltPurgeService::EmptyChainBatch(
				chain, batchLimit, SinkSubsystem.Get());
			RemovedConveyorItems += batch.RemovedItems;
			RemovedThisPass += batch.RemovedItems;
			work += FMath::Max(1, batch.RemovedItems);
			if (batch.bMutationFailed)
			{
				Finish(false, TEXT("conveyor mutation failed"));
				return false;
			}
			if (batch.bStoppedAtProtected)
			{
				// Never revisit a protected output, even if normal factory flow later
				// moves that item away. Items behind it remain outside this purge.
				ProtectedConveyorItems += batch.ProtectedItems;
				StoppedChains.Add(CurrentChainIndex++);
			}
			else if (chain->GetNumActualItems() == 0)
			{
				++CurrentChainIndex;
			}
			else if (beforeCount > 0 && batch.RemovedItems == 0)
			{
				Finish(false, TEXT("conveyor removal made no progress"));
				return false;
			}
			continue;
		}

		if (RemovedThisPass == 0)
		{
			Finish(true, TEXT("complete"));
			return false;
		}
		// Factory flow may transfer items between callbacks. Recheck the same
		// bounded network, but never delete more than its initial item total.
		RemovedThisPass = 0;
		CurrentInventoryIndex = 0;
		CurrentChainIndex = 0;
	}
	return true;
}

int32 FBeltPurgeOperation::CountInventoryItems(UFGInventoryComponent* inventory)
{
	int32 count = 0;
	if (IsValid(inventory))
	{
		for (int32 index = 0; index < inventory->GetSizeLinear(); ++index)
		{
			FInventoryStack stack;
			if (inventory->GetStackFromIndex(index, stack) && stack.HasItems())
			{
				count += stack.NumItems;
			}
		}
	}
	return count;
}

void FBeltPurgeOperation::Finish(bool succeeded, const TCHAR* reason) const
{
	// Transfers between belts and buffers invalidate snapshot-minus-removed
	// arithmetic. Report the actual retained state instead.
	int32 remainingConveyorItems = 0;
	int32 remainingInventoryItems = 0;
	int32 protectedInventoryItems = 0;
	for (const TWeakObjectPtr<AFGConveyorChainActor>& weakChain : Chains)
	{
		if (AFGConveyorChainActor* chain = weakChain.Get())
		{
			remainingConveyorItems += chain->GetNumActualItems();
		}
	}
	for (const TWeakObjectPtr<UFGInventoryComponent>& weakInventory : Inventories)
	{
		UFGInventoryComponent* inventory = weakInventory.Get();
		if (!IsValid(inventory))
		{
			continue;
		}
		for (int32 index = 0; index < inventory->GetSizeLinear(); ++index)
		{
			FInventoryStack stack;
			if (inventory->GetStackFromIndex(index, stack) && stack.HasItems())
			{
				remainingInventoryItems += stack.NumItems;
				if (!UBeltPurgeService::IsEligibleForPurge(stack.Item.GetItemClass(), SinkSubsystem.Get()))
				{
					protectedInventoryItems += stack.NumItems;
				}
			}
		}
	}
	const double elapsedMilliseconds =
		(FPlatformTime::Seconds() - StartTimeSeconds) * 1000.0;
	if (succeeded)
	{
		UE_LOG(
			LogBeltPurge,
			Display,
			TEXT("[BeltPurge:%llu] End status=success reason=\"%s\" chains=%d processedChains=%d skippedChains=%d processedAttachmentBuffers=%d processedInputInventories=%d conveyorItemsDiscovered=%d inventoryItemsDiscovered=%d conveyorItemsRemoved=%d inventoryItemsRemoved=%d totalItemsRemoved=%d protectedConveyorItems=%d protectedInventoryItems=%d conveyorItemsRemaining=%d inventoryItemsRemaining=%d elapsedMs=%.2f."),
			OperationId,
			reason,
			Chains.Num(),
			VisitedChains.Num(),
			SkippedChains,
			ProcessedAttachmentBuffers,
			ProcessedInputInventories,
			InitialConveyorItems,
			InitialInventoryItems,
			RemovedConveyorItems,
			RemovedInventoryItems,
			RemovedConveyorItems + RemovedInventoryItems,
			ProtectedConveyorItems,
			protectedInventoryItems,
			remainingConveyorItems,
			remainingInventoryItems,
			elapsedMilliseconds);
		return;
	}

	UE_LOG(
		LogBeltPurge,
		Error,
		TEXT("[BeltPurge:%llu] End status=failed reason=\"%s\" chains=%d processedChains=%d skippedChains=%d processedAttachmentBuffers=%d processedInputInventories=%d conveyorItemsDiscovered=%d inventoryItemsDiscovered=%d conveyorItemsRemoved=%d inventoryItemsRemoved=%d totalItemsRemoved=%d protectedConveyorItems=%d protectedInventoryItems=%d conveyorItemsRemaining=%d inventoryItemsRemaining=%d elapsedMs=%.2f."),
		OperationId,
		reason,
		Chains.Num(),
		VisitedChains.Num(),
		SkippedChains,
			ProcessedAttachmentBuffers,
			ProcessedInputInventories,
		InitialConveyorItems,
		InitialInventoryItems,
		RemovedConveyorItems,
		RemovedInventoryItems,
		RemovedConveyorItems + RemovedInventoryItems,
		ProtectedConveyorItems,
		protectedInventoryItems,
		remainingConveyorItems,
		remainingInventoryItems,
		elapsedMilliseconds);
}

void UBeltPurgeService::GatherAttachmentRecursive(
	AFGBuildableConveyorAttachment* attachment,
	UFGFactoryConnectionComponent* entryConnection,
	FBeltPurgeNetwork& network)
{
	if (!IsValid(attachment) || network.Attachments.Contains(attachment))
	{
		return;
	}

	network.Attachments.Add(attachment);
	const TArray<AFGBuildableConveyorBase*> linkedConveyors =
		GetLinkedConveyors(attachment, entryConnection);
	for (AFGBuildableConveyorBase* linkedConveyor : linkedConveyors)
	{
		GatherNetworkRecursive(linkedConveyor, network);
	}
}

void UBeltPurgeService::GatherNetworkRecursive(
	AFGBuildableConveyorBase* conveyor,
	FBeltPurgeNetwork& network)
{
	if (!IsValid(conveyor) || network.Conveyors.Contains(conveyor))
	{
		return;
	}

	AFGConveyorChainActor* chain = GetConveyorChain(conveyor);
	if (!IsValid(chain))
	{
		return;
	}

	network.Conveyors.Add(conveyor);

	auto visitChainConnection =
		[conveyor, &network](UFGFactoryConnectionComponent* conveyorConnection)
		{
			if (!IsValid(conveyorConnection))
			{
				return;
			}

			if (UFGInventoryComponent* inputInventory =
				GetInputInventory(conveyorConnection, conveyor))
			{
				network.InputInventories.Add(inputInventory);
				return;
			}

			// Directly connected belts/lifts can retain distinct native chains.
			// Traverse that boundary as well as splitter/merger attachments.
			UFGFactoryConnectionComponent* connectedConnection = conveyorConnection->GetConnection();
			if (IsValid(connectedConnection))
			{
				AFGBuildableConveyorBase* linkedConveyor =
					Cast<AFGBuildableConveyorBase>(connectedConnection->GetOwner());
				if (IsValid(linkedConveyor))
				{
					GatherNetworkRecursive(linkedConveyor, network);
					return;
				}
			}

			const FBeltPurgeAttachmentLink link =
				GetAttachmentLink(conveyorConnection, conveyor);
			GatherAttachmentRecursive(link.Attachment, link.ConnectionOnAttachment, network);
		};

	visitChainConnection(chain->mConnection0);
	visitChainConnection(chain->mConnection1);
}

bool UBeltPurgeService::IsEligibleForPurge(
	TSubclassOf<UFGItemDescriptor> itemClass,
	AFGResourceSinkSubsystem* sinkSubsystem)
{
	return itemClass != nullptr && IsValid(sinkSubsystem) &&
		UFGItemDescriptor::CanBeDiscarded(itemClass) &&
		sinkSubsystem->GetResourceSinkPointsForItem(itemClass) > 0;
}

FBeltPurgeRemovalResult UBeltPurgeService::RemoveEligibleInventoryItems(
	UFGInventoryComponent* inventory,
	int32 maximumItems,
	AFGResourceSinkSubsystem* sinkSubsystem)
{
	FBeltPurgeRemovalResult result;
	if (!IsValid(inventory) || !IsValid(sinkSubsystem))
	{
		return result;
	}

	for (int32 index = inventory->GetSizeLinear() - 1; index >= 0; --index)
	{
		FInventoryStack stack;
		if (!inventory->GetStackFromIndex(index, stack) || !stack.HasItems())
		{
			continue;
		}

		result.DiscoveredItems += stack.NumItems;
		if (!IsEligibleForPurge(stack.Item.GetItemClass(), sinkSubsystem))
		{
			result.ProtectedItems += stack.NumItems;
			result.RemainingItems += stack.NumItems;
			continue;
		}

		const int32 expectedRemoved = FMath::Min(stack.NumItems, FMath::Max(0, maximumItems - result.RemovedItems));
		if (expectedRemoved == 0)
		{
			result.RemainingItems += stack.NumItems;
			continue;
		}
		inventory->RemoveFromIndex(index, expectedRemoved, nullptr);
		FInventoryStack remainingStack;
		const bool bReadRemainingStack = inventory->GetStackFromIndex(index, remainingStack);
		if (!bReadRemainingStack)
		{
			result.RemainingItems += stack.NumItems;
			result.bMutationFailed = true;
			continue;
		}

		const int32 remaining = remainingStack.HasItems() ? remainingStack.NumItems : 0;
		const int32 removed = FMath::Max(0, stack.NumItems - remaining);
		result.RemovedItems += removed;
		result.RemainingItems += remaining;
		if (removed != expectedRemoved)
		{
			result.bMutationFailed = true;
		}
	}
	return result;
}

FBeltPurgeRemovalResult UBeltPurgeService::EmptyChainBatch(
	AFGConveyorChainActor* chain,
	int32 maximumItems,
	AFGResourceSinkSubsystem* sinkSubsystem)
{
	FBeltPurgeRemovalResult result;
	if (!IsValid(chain) || maximumItems <= 0 || !IsValid(sinkSubsystem))
	{
		return result;
	}

	while (result.RemovedItems < maximumItems && chain->GetNumActualItems() > 0)
	{
		// Purge items in place; factory output is gated on reaching the belt end.
		const int32 itemIndex = chain->mLeadItemIndex;
		if (!chain->IsItemIndexValid(itemIndex))
		{
			result.bMutationFailed = true;
			return result;
		}
		FConveyorBeltItem* item = chain->GetItemForIndex(itemIndex);
		if (item == nullptr)
		{
			result.bMutationFailed = true;
			return result;
		}

		const TSubclassOf<UFGItemDescriptor> observedClass = item->Item.GetItemClass();
		++result.DiscoveredItems;
		if (!IsEligibleForPurge(observedClass, sinkSubsystem))
		{
			++result.ProtectedItems;
			++result.RemainingItems;
			result.bStoppedAtProtected = true;
			return result;
		}

		const int32 beforeCount = chain->GetNumActualItems();
		chain->Factory_RemoveItemAt(itemIndex, observedClass);
		if (chain->GetNumActualItems() != beforeCount - 1)
		{
			result.bMutationFailed = true;
			return result;
		}
		++result.RemovedItems;
	}
	return result;
}

bool UBeltPurgeService::RequestPurgeFromAim(AFGPlayerController* controller)
{
	if (!IsValid(controller))
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
	const bool hit = IsValid(world) &&
		world->LineTraceSingleByChannel(
			hitResult,
			viewLocation,
			traceEnd,
			ECC_Visibility,
			queryParameters);

	AActor* hitActor = hit ? hitResult.GetActor() : nullptr;
	if (AAbstractInstanceManager* instanceManager = Cast<AAbstractInstanceManager>(hitActor);
		IsValid(instanceManager))
	{
		FInstanceHandle instanceHandle;
		hitActor = instanceManager->ResolveHit(hitResult, instanceHandle)
			? AAbstractInstanceManager::GetOwnerByHandle(instanceHandle) : nullptr;
	}
	AFGBuildable* target = Cast<AFGBuildable>(hitActor);
	const bool isSupportedTarget =
		IsValid(target) &&
		(Cast<AFGBuildableConveyorBase>(target) != nullptr ||
			Cast<AFGBuildableConveyorAttachment>(target) != nullptr);
	if (!isSupportedTarget)
	{
		controller->ClientMessage(TEXT("Belt Purge: Aim at a conveyor belt, lift, Splitter, or Merger."));
		return false;
	}

	if (controller->HasAuthority())
	{
		PurgeNetwork(target);
		controller->ClientMessage(TEXT("Belt Purge started."));
		return true;
	}

	UBeltPurgeRemoteCallObject* remoteCallObject =
		controller->GetRemoteCallObjectOfClass<UBeltPurgeRemoteCallObject>();
	if (!IsValid(remoteCallObject))
	{
		controller->ClientMessage(TEXT("Belt Purge: server connection is not ready."));
		return false;
	}

	remoteCallObject->ServerPurgeNetwork(target);
	controller->ClientMessage(TEXT("Belt Purge requested."));
	return true;
}

void UBeltPurgeService::PurgeNetwork(AFGBuildable* startTarget)
{
	if (!IsValid(startTarget))
	{
		UE_LOG(LogBeltPurge, Warning, TEXT("[BeltPurge] Start rejected invalid target."));
		return;
	}

	AFGBuildableConveyorBase* startConveyor = Cast<AFGBuildableConveyorBase>(startTarget);
	AFGBuildableConveyorAttachment* startAttachment =
		Cast<AFGBuildableConveyorAttachment>(startTarget);
	if ((startConveyor == nullptr && startAttachment == nullptr) ||
		!startTarget->HasAuthority())
	{
		UE_LOG(
			LogBeltPurge,
			Warning,
			TEXT("[BeltPurge] Start rejected target=%s authority=%s."),
			*GetNameSafe(startTarget),
			startTarget->HasAuthority()
				? TEXT("true")
				: TEXT("false"));
		return;
	}

	const uint64 operationId = NextOperationId++;
	UE_LOG(
		LogBeltPurge,
		Display,
		TEXT("[BeltPurge:%llu] Start target=%s."),
		operationId,
		*GetNameSafe(startTarget));

	const double operationStartTimeSeconds = FPlatformTime::Seconds();
	AFGResourceSinkSubsystem* sinkSubsystem =
		AFGResourceSinkSubsystem::Get(startTarget->GetWorld());
	if (!IsValid(sinkSubsystem))
	{
		UE_LOG(LogBeltPurge, Error, TEXT("[BeltPurge:%llu] End status=failed reason=\"resource sink subsystem unavailable; no mutations\"."), operationId);
		return;
	}
	FBeltPurgeNetwork network;
	if (startConveyor != nullptr)
	{
		GatherNetworkRecursive(startConveyor, network);
	}
	else
	{
		GatherAttachmentRecursive(startAttachment, nullptr, network);
	}

	TSharedRef<FBeltPurgeOperation> operation = MakeShared<FBeltPurgeOperation>();
	operation->OperationId = operationId;
	operation->World = startTarget->GetWorld();
	operation->SinkSubsystem = sinkSubsystem;
	operation->StartTimeSeconds = operationStartTimeSeconds;

	TSet<UFGInventoryComponent*> processedInventories;
	for (UFGInventoryComponent* inputInventory : network.InputInventories)
	{
		if (IsValid(inputInventory) && !processedInventories.Contains(inputInventory))
		{
			operation->Inventories.Add(inputInventory);
			operation->InitialInventoryItems += FBeltPurgeOperation::CountInventoryItems(inputInventory);
			processedInventories.Add(inputInventory);
			++operation->ProcessedInputInventories;
		}
	}

	for (AFGBuildableConveyorAttachment* attachment : network.Attachments)
	{
		if (!IsValid(attachment))
		{
			continue;
		}

		UFGInventoryComponent* bufferInventory = attachment->GetBufferInventory();
		if (IsValid(bufferInventory) && !processedInventories.Contains(bufferInventory))
		{
			operation->Inventories.Add(bufferInventory);
			operation->InitialInventoryItems += FBeltPurgeOperation::CountInventoryItems(bufferInventory);
			processedInventories.Add(bufferInventory);
			++operation->ProcessedAttachmentBuffers;
		}
	}

	TSet<AFGConveyorChainActor*> discoveredChains;
	int32 conveyorItemCount = 0;
	for (AFGBuildableConveyorBase* conveyor : network.Conveyors)
	{
		AFGConveyorChainActor* chain = GetConveyorChain(conveyor);
		if (IsValid(chain) && !discoveredChains.Contains(chain))
		{
			discoveredChains.Add(chain);
			const int32 chainItemCount = chain->GetNumActualItems();
			conveyorItemCount += chainItemCount;
			operation->Chains.Add(chain);
		}
	}
	operation->InitialConveyorItems = conveyorItemCount;

	UE_LOG(
		LogBeltPurge,
		Display,
		TEXT("[BeltPurge:%llu] Discovery conveyors=%d chains=%d processedAttachmentBuffers=%d processedInputInventories=%d conveyorItemsDiscovered=%d inventoryItemsDiscovered=%d totalItems=%d."),
		operationId,
		network.Conveyors.Num(),
		operation->Chains.Num(),
		operation->ProcessedAttachmentBuffers,
		operation->ProcessedInputInventories,
		conveyorItemCount,
		operation->InitialInventoryItems,
		conveyorItemCount + operation->InitialInventoryItems);

	if (operation->Chains.IsEmpty() && operation->Inventories.IsEmpty())
	{
		operation->Finish(true, TEXT("no items to process"));
		return;
	}

	FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda(
			[operation](float deltaTime)
			{
				return operation->Tick(deltaTime);
			}));
}
