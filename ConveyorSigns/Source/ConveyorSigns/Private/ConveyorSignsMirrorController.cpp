#include "ConveyorSignsMirrorController.h"

#include "ConveyorSignsEndpoints.h"
#include "ConveyorSignsLog.h"
#include "ConveyorSignsMirrorPolicy.h"

#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGSaveInterface.h"

void FConveyorSignsMirrorController::Reset()
{
	for (const TPair<
		TWeakObjectPtr<AFGBuildableWidgetSign>,
		TWeakObjectPtr<AFGBuildableWidgetSign>>& mirrorPair : mirrorsBySource)
	{
		AFGBuildableWidgetSign* mirror = mirrorPair.Value.Get();
		if (IsValid(mirror) && mirror->HasAuthority())
		{
			ignoredEndPlaySigns.Add(mirror);
			mirror->Destroy();
		}
	}

	mirrorsBySource.Reset();
	liftsBySource.Reset();
	ignoredEndPlaySigns.Reset();
	initializedWorlds.Reset();
}

int32 FConveyorSignsMirrorController::RefreshWorld(UWorld* world)
{
	if (world == nullptr || world->GetNetMode() == NM_Client)
	{
		return 0;
	}

	RemoveSavedMirrors(world);

	TArray<AFGBuildableConveyorLift*> lifts;
	for (TActorIterator<AFGBuildableConveyorLift> liftIterator(world); liftIterator; ++liftIterator)
	{
		AFGBuildableConveyorLift* lift = *liftIterator;
		if (IsValid(lift) && lift->HasAuthority())
		{
			lifts.Add(lift);
		}
	}

	TSet<TWeakObjectPtr<AFGBuildableWidgetSign>> activeSources;
	int32 activeMirrorCount = 0;
	for (TActorIterator<AFGBuildableWidgetSign> signIterator(world); signIterator; ++signIterator)
	{
		AFGBuildableWidgetSign* source = *signIterator;
		if (!IsValid(source) ||
			!source->HasAuthority() ||
			FConveyorSignsMirrorPolicy::IsMirror(source))
		{
			continue;
		}

		FTransform mirrorTransform;
		FTransform canonicalSourceTransform;
		AFGBuildableConveyorLift* matchedLift = nullptr;
		if (!TryGetMirrorTransform(
			source,
			lifts,
			mirrorTransform,
			canonicalSourceTransform,
			matchedLift))
		{
			continue;
		}
		if (!source->GetActorTransform().Equals(canonicalSourceTransform))
		{
			source->SetActorTransform(
				canonicalSourceTransform,
				false,
				nullptr,
				ETeleportType::TeleportPhysics);
		}

		activeSources.Add(source);
		if (EnsureMirror(source, mirrorTransform, matchedLift) != nullptr)
		{
			++activeMirrorCount;
		}
	}

	TMap<
		TWeakObjectPtr<AFGBuildableWidgetSign>,
		TWeakObjectPtr<AFGBuildableWidgetSign>>::TIterator mirrorIterator = mirrorsBySource.CreateIterator();
	while (mirrorIterator)
	{
		const TWeakObjectPtr<AFGBuildableWidgetSign> source = mirrorIterator.Key();
		AFGBuildableWidgetSign* mirror = mirrorIterator.Value().Get();
		if (!source.IsValid() || !activeSources.Contains(source))
		{
			liftsBySource.Remove(source);
			if (IsValid(mirror) && mirror->HasAuthority())
			{
				ignoredEndPlaySigns.Add(mirror);
				mirror->Destroy();
			}
			mirrorIterator.RemoveCurrent();
		}
		else
		{
			++mirrorIterator;
		}
	}

	for (TActorIterator<AFGBuildableWidgetSign> signIterator(world); signIterator; ++signIterator)
	{
		AFGBuildableWidgetSign* mirror = *signIterator;
		if (!IsValid(mirror) ||
			!mirror->HasAuthority() ||
			!FConveyorSignsMirrorPolicy::IsMirror(mirror))
		{
			continue;
		}

		AFGBuildableWidgetSign* source = Cast<AFGBuildableWidgetSign>(mirror->GetOwner());
		if (!IsValid(source) || !activeSources.Contains(source))
		{
			ignoredEndPlaySigns.Add(mirror);
			mirror->Destroy();
		}
	}

	return activeMirrorCount;
}

AFGBuildableWidgetSign* FConveyorSignsMirrorController::EnsureMirrorForSource(
	AFGBuildableWidgetSign* source)
{
	if (!IsValid(source) ||
		!source->HasAuthority() ||
		FConveyorSignsMirrorPolicy::IsMirror(source))
	{
		return nullptr;
	}

	UWorld* world = source->GetWorld();
	if (world == nullptr || !world->HasBegunPlay())
	{
		return nullptr;
	}

	TArray<AFGBuildableConveyorLift*> lifts;
	for (TActorIterator<AFGBuildableConveyorLift> liftIterator(world); liftIterator; ++liftIterator)
	{
		AFGBuildableConveyorLift* lift = *liftIterator;
		if (IsValid(lift) && lift->HasAuthority())
		{
			lifts.Add(lift);
		}
	}

	FTransform mirrorTransform;
	FTransform canonicalSourceTransform;
	AFGBuildableConveyorLift* matchedLift = nullptr;
	if (!TryGetMirrorTransform(
		source,
		lifts,
		mirrorTransform,
		canonicalSourceTransform,
		matchedLift))
	{
		return nullptr;
	}
	if (!source->GetActorTransform().Equals(canonicalSourceTransform))
	{
		source->SetActorTransform(
			canonicalSourceTransform,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
	}

	return EnsureMirror(source, mirrorTransform, matchedLift);
}

void FConveyorSignsMirrorController::SyncSource(AFGBuildableWidgetSign* source)
{
	if (!IsValid(source) || FConveyorSignsMirrorPolicy::IsMirror(source))
	{
		return;
	}

	const TWeakObjectPtr<AFGBuildableWidgetSign>* mirrorReference = mirrorsBySource.Find(source);
	AFGBuildableWidgetSign* mirror = mirrorReference != nullptr ? mirrorReference->Get() : nullptr;
	if (IsValid(mirror))
	{
		CopySignData(source, mirror);
	}
}

void FConveyorSignsMirrorController::OnSignEndPlay(
	AFGBuildableWidgetSign* sign,
	EEndPlayReason::Type reason)
{
	if (sign == nullptr || !FConveyorSignsMirrorPolicy::ShouldDeletePartner(reason))
	{
		return;
	}

	if (ignoredEndPlaySigns.Remove(sign) > 0)
	{
		return;
	}

	if (FConveyorSignsMirrorPolicy::IsMirror(sign))
	{
		AFGBuildableWidgetSign* source = Cast<AFGBuildableWidgetSign>(sign->GetOwner());
		if (source != nullptr)
		{
			mirrorsBySource.Remove(source);
			liftsBySource.Remove(source);
		}
		if (IsValid(source) && source->HasAuthority())
		{
			ignoredEndPlaySigns.Add(source);
			source->Destroy();
		}
		return;
	}

	TWeakObjectPtr<AFGBuildableWidgetSign> mirrorReference;
	liftsBySource.Remove(sign);
	if (!mirrorsBySource.RemoveAndCopyValue(sign, mirrorReference))
	{
		return;
	}

	AFGBuildableWidgetSign* mirror = mirrorReference.Get();
	if (IsValid(mirror) && mirror->HasAuthority())
	{
		ignoredEndPlaySigns.Add(mirror);
		mirror->Destroy();
	}
}

void FConveyorSignsMirrorController::OnLiftEndPlay(
	AFGBuildableConveyorLift* lift,
	EEndPlayReason::Type reason)
{
	if (!IsValid(lift) || !FConveyorSignsMirrorPolicy::ShouldDeleteSignsWithLift(reason))
	{
		return;
	}

	TArray<TWeakObjectPtr<AFGBuildableWidgetSign>> sourcesToDelete;
	for (const TPair<
		TWeakObjectPtr<AFGBuildableWidgetSign>,
		TWeakObjectPtr<AFGBuildableConveyorLift>>& sourceLiftPair : liftsBySource)
	{
		if (sourceLiftPair.Value.Get() == lift)
		{
			sourcesToDelete.Add(sourceLiftPair.Key);
		}
	}

	for (const TWeakObjectPtr<AFGBuildableWidgetSign>& sourceReference : sourcesToDelete)
	{
		AFGBuildableWidgetSign* source = sourceReference.Get();
		if (IsValid(source) && source->HasAuthority())
		{
			source->Destroy();
		}
		else
		{
			TWeakObjectPtr<AFGBuildableWidgetSign> mirrorReference;
			if (mirrorsBySource.RemoveAndCopyValue(sourceReference, mirrorReference))
			{
				AFGBuildableWidgetSign* mirror = mirrorReference.Get();
				if (IsValid(mirror) && mirror->HasAuthority())
				{
					ignoredEndPlaySigns.Add(mirror);
					mirror->Destroy();
				}
			}
			liftsBySource.Remove(sourceReference);
		}
	}
}

bool FConveyorSignsMirrorController::TryGetMirrorTransform(
	AFGBuildableWidgetSign* source,
	const TArray<AFGBuildableConveyorLift*>& lifts,
	FTransform& mirrorTransform,
	FTransform& canonicalSourceTransform,
	AFGBuildableConveyorLift*& matchedLift) const
{
	matchedLift = nullptr;
	for (AFGBuildableConveyorLift* lift : lifts)
	{
		if (!IsValid(lift))
		{
			continue;
		}

		const FConveyorSignsEndpointTransforms endpoints = FConveyorSignsEndpoints::GetTransforms(lift);
		if (FConveyorSignsMirrorPolicy::TryGetMirrorTransform(
			source->GetActorTransform(),
			lift->GetActorTransform(),
			endpoints,
			mirrorTransform,
			&canonicalSourceTransform))
		{
			matchedLift = lift;
			return true;
		}
	}

	return false;
}

AFGBuildableWidgetSign* FConveyorSignsMirrorController::EnsureMirror(
	AFGBuildableWidgetSign* source,
	const FTransform& mirrorTransform,
	AFGBuildableConveyorLift* lift)
{
	if (!IsValid(source) || !source->HasAuthority() || !IsValid(lift))
	{
		return nullptr;
	}

	TWeakObjectPtr<AFGBuildableWidgetSign>& mirrorReference = mirrorsBySource.FindOrAdd(source);
	AFGBuildableWidgetSign* mirror = mirrorReference.Get();
	if (!IsValid(mirror))
	{
		mirror = FindExistingMirror(source->GetWorld(), source);
	}

	if (!IsValid(mirror))
	{
		FActorSpawnParameters spawnParameters;
		spawnParameters.Owner = source;
		spawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		spawnParameters.ObjectFlags |= RF_Transient;
		spawnParameters.bDeferConstruction = true;
		mirror = source->GetWorld()->SpawnActor<AFGBuildableWidgetSign>(
			source->GetClass(),
			mirrorTransform,
			spawnParameters);
		if (!IsValid(mirror))
		{
			mirrorsBySource.Remove(source);
			return nullptr;
		}

		mirror->Tags.AddUnique(FConveyorSignsMirrorPolicy::GetMirrorTag());
		mirror->SetReplicates(true);
		mirror->SetReplicateMovement(true);
		mirrorReference = mirror;
		mirror->FinishSpawning(mirrorTransform);
		if (IFGSaveInterface::Execute_ShouldSave(mirror))
		{
			UE_LOG(
				LogConveyorSigns,
				Error,
				TEXT("Generated mirror %s still reports ShouldSave=true; destroying it to protect the save."),
				*mirror->GetName());
			ignoredEndPlaySigns.Add(mirror);
			mirror->Destroy();
			mirrorsBySource.Remove(source);
			return nullptr;
		}
	}

	if (!mirror->GetActorTransform().Equals(mirrorTransform))
	{
		mirror->SetActorTransform(mirrorTransform, false, nullptr, ETeleportType::TeleportPhysics);
	}

	liftsBySource.Add(source, lift);
	ConfigureMirrorInteraction(mirror);
	CopySignData(source, mirror);
	return mirror;
}

AFGBuildableWidgetSign* FConveyorSignsMirrorController::FindExistingMirror(
	UWorld* world,
	AFGBuildableWidgetSign* source) const
{
	if (world == nullptr)
	{
		return nullptr;
	}

	for (TActorIterator<AFGBuildableWidgetSign> signIterator(world); signIterator; ++signIterator)
	{
		AFGBuildableWidgetSign* sign = *signIterator;
		if (IsValid(sign) &&
			sign->GetOwner() == source &&
			FConveyorSignsMirrorPolicy::IsMirror(sign))
		{
			return sign;
		}
	}

	return nullptr;
}

void FConveyorSignsMirrorController::CopySignData(
	AFGBuildableWidgetSign* source,
	AFGBuildableWidgetSign* mirror)
{
	if (!IsValid(source) || !IsValid(mirror))
	{
		return;
	}

	FPrefabSignData sourceData;
	source->GetSignPrefabData(sourceData);
	mirror->SetPrefabSignData(sourceData, false);
}

void FConveyorSignsMirrorController::ConfigureMirrorInteraction(AFGBuildableWidgetSign* mirror) const
{
	if (!IsValid(mirror))
	{
		return;
	}

	mirror->SetActorEnableCollision(FConveyorSignsMirrorPolicy::ShouldKeepMirrorCollision());
	mirror->SetCanBeDamaged(false);
}

void FConveyorSignsMirrorController::RemoveSavedMirrors(UWorld* world)
{
	const TWeakObjectPtr<UWorld> worldReference(world);
	if (initializedWorlds.Contains(worldReference))
	{
		return;
	}
	initializedWorlds.Add(worldReference);

	TArray<AFGBuildableWidgetSign*> savedMirrors;
	for (TActorIterator<AFGBuildableWidgetSign> signIterator(world); signIterator; ++signIterator)
	{
		AFGBuildableWidgetSign* sign = *signIterator;
		if (FConveyorSignsMirrorPolicy::IsMirror(sign))
		{
			savedMirrors.Add(sign);
		}
	}

	for (AFGBuildableWidgetSign* savedMirror : savedMirrors)
	{
		if (IsValid(savedMirror) && savedMirror->HasAuthority())
		{
			ignoredEndPlaySigns.Add(savedMirror);
			savedMirror->Destroy();
		}
	}
}
