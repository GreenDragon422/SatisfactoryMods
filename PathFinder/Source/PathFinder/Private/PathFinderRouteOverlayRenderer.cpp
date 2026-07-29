#include "PathFinderRouteOverlayRenderer.h"

#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeLock.h"
#include "PathFinderRouteOverlayComponent.h"
#include "PathFinderRouteTypes.h"
#include "WheeledVehicles/FGVehiclePathNode.h"
#include "WheeledVehicles/FGVehiclePathSegment.h"

AFGVehiclePathSegment* FPathFinderRouteOverlayRenderer::ResolveVehiclePathSegmentActor(UWorld* World, const FPathFinderPaintSample& PaintSample)
{
	if (World == nullptr)
	{
		return nullptr;
	}

	AFGVehiclePathSegment* EndToStartMatch = nullptr;
	for (TActorIterator<AFGVehiclePathSegment> SegmentIterator(World); SegmentIterator; ++SegmentIterator)
	{
		AFGVehiclePathSegment* SegmentActor = *SegmentIterator;
		if (SegmentActor == nullptr || SegmentActor->IsActorBeingDestroyed())
		{
			continue;
		}

		AFGVehiclePathNode* EndNode = SegmentActor->GetEndNode();
		if (EndNode == nullptr)
		{
			continue;
		}

		const FGuid SegmentStartGuid = SegmentActor->GetStartPathNodeGuid();
		const FGuid SegmentEndGuid = EndNode->GetPathNodeGUID();
		if (SegmentStartGuid == PaintSample.FromNodeGuid && SegmentEndGuid == PaintSample.ToNodeGuid)
		{
			return SegmentActor;
		}

		if (SegmentStartGuid == PaintSample.ToNodeGuid && SegmentEndGuid == PaintSample.FromNodeGuid)
		{
			EndToStartMatch = SegmentActor;
		}
	}

	return EndToStartMatch;
}

bool FPathFinderRouteOverlayRenderer::DrawPaintSample(UWorld* World, const FString& SegmentKey, const FPathFinderPaintSample& PaintSample, const FLinearColor& Color, const bool bColorChanged, const FString& InitialLabelText)
{
	AFGVehiclePathSegment* SegmentActor = ResolveVehiclePathSegmentActor(World, PaintSample);
	return DrawPathSegment(SegmentActor, SegmentKey, Color, bColorChanged, InitialLabelText);
}

bool FPathFinderRouteOverlayRenderer::DrawPathSegment(AFGVehiclePathSegment* SegmentActor, const FString& SegmentKey, const FLinearColor& Color, const bool bColorChanged, const FString& InitialLabelText)
{
	AActor* MeshOwner = Cast<AActor>(SegmentActor);
	if (MeshOwner == nullptr || MeshOwner->GetRootComponent() == nullptr)
	{
		return false;
	}

	UPathFinderRouteOverlayComponent* OverlayComponent = nullptr;
	TWeakObjectPtr<UPathFinderRouteOverlayComponent>* ExistingOverlayComponent = OverlayComponentsBySegmentKey.Find(SegmentKey);
	if (ExistingOverlayComponent != nullptr)
	{
		OverlayComponent = ExistingOverlayComponent->Get();
		if (OverlayComponent != nullptr && OverlayComponent->GetOwner() != MeshOwner)
		{
			OverlayComponent->DestroyComponent();
			OverlayComponentsBySegmentKey.Remove(SegmentKey);
			OverlayComponent = nullptr;
		}
	}

	if (OverlayComponent == nullptr)
	{
		const FName ComponentName = MakeUniqueObjectName(MeshOwner, UPathFinderRouteOverlayComponent::StaticClass(), TEXT("PathFinderRouteOverlay"));
		OverlayComponent = NewObject<UPathFinderRouteOverlayComponent>(MeshOwner, UPathFinderRouteOverlayComponent::StaticClass(), ComponentName);
		if (OverlayComponent == nullptr)
		{
			return false;
		}

		MeshOwner->AddInstanceComponent(OverlayComponent);
		OverlayComponent->RegisterComponent();
		OverlayComponentsBySegmentKey.Add(SegmentKey, OverlayComponent);
	}

	return OverlayComponent->DrawPaintSample(SegmentActor, Color, bColorChanged, InitialLabelText);
}

void FPathFinderRouteOverlayRenderer::QueueLabelUpdate(const FString& SegmentKey, const FString& LabelText)
{
	FScopeLock Lock(&QueuedLabelUpdatesCriticalSection);
	QueuedLabelUpdates.Add({ SegmentKey, LabelText });
}

void FPathFinderRouteOverlayRenderer::FlushQueuedLabelUpdates()
{
	TArray<FQueuedLabelUpdate> LabelUpdatesToApply;
	{
		FScopeLock Lock(&QueuedLabelUpdatesCriticalSection);
		LabelUpdatesToApply = MoveTemp(QueuedLabelUpdates);
		QueuedLabelUpdates.Empty();
	}

	for (const FQueuedLabelUpdate& LabelUpdate : LabelUpdatesToApply)
	{
		TWeakObjectPtr<UPathFinderRouteOverlayComponent>* OverlayComponentPointer = OverlayComponentsBySegmentKey.Find(LabelUpdate.SegmentKey);
		if (OverlayComponentPointer == nullptr)
		{
			continue;
		}

		UPathFinderRouteOverlayComponent* OverlayComponent = OverlayComponentPointer->Get();
		if (OverlayComponent != nullptr)
		{
			OverlayComponent->SetLabelText(LabelUpdate.LabelText);
		}
	}
}

bool FPathFinderRouteOverlayRenderer::UpdateSegmentColor(const FString& SegmentKey, const FLinearColor& Color)
{
	TWeakObjectPtr<UPathFinderRouteOverlayComponent>* OverlayComponentPointer = OverlayComponentsBySegmentKey.Find(SegmentKey);
	if (OverlayComponentPointer == nullptr)
	{
		return false;
	}

	UPathFinderRouteOverlayComponent* OverlayComponent = OverlayComponentPointer->Get();
	if (OverlayComponent == nullptr)
	{
		return false;
	}

	OverlayComponent->SetColor(Color);
	return true;
}

FString FPathFinderRouteOverlayRenderer::DescribeMaterialProbe() const
{
	return DescribeRouteOverlayMaterialProbe();
}

FString FPathFinderRouteOverlayRenderer::DescribeRouteOverlayMaterialProbe()
{
	return UPathFinderRouteOverlayComponent::DescribeRouteOverlayMaterialProbe();
}

int32 FPathFinderRouteOverlayRenderer::GetRenderStateCount() const
{
	return OverlayComponentsBySegmentKey.Num();
}

bool FPathFinderRouteOverlayRenderer::GetNearestLabelGeometry(
	const FVector& ReferenceLocation,
	FLabelGeometry& LabelGeometry) const
{
	bool FoundLabel = false;
	double ClosestDistanceSquared = TNumericLimits<double>::Max();

	for (const TPair<FString, TWeakObjectPtr<UPathFinderRouteOverlayComponent>>& OverlayEntry : OverlayComponentsBySegmentKey)
	{
		const UPathFinderRouteOverlayComponent* OverlayComponent = OverlayEntry.Value.Get();
		if (OverlayComponent == nullptr)
		{
			continue;
		}

		FLabelGeometry CandidateGeometry;
		if (!OverlayComponent->GetLabelGeometry(
			CandidateGeometry.Location,
			CandidateGeometry.Rotation,
			CandidateGeometry.TextPlaneNormal,
			CandidateGeometry.TextDirection,
			CandidateGeometry.StripDirection))
		{
			continue;
		}

		const double DistanceSquared = FVector::DistSquared(ReferenceLocation, CandidateGeometry.Location);
		if (DistanceSquared >= ClosestDistanceSquared)
		{
			continue;
		}

		ClosestDistanceSquared = DistanceSquared;
		CandidateGeometry.SegmentKey = OverlayEntry.Key;
		CandidateGeometry.DistanceCentimeters = FMath::Sqrt(DistanceSquared);
		LabelGeometry = CandidateGeometry;
		FoundLabel = true;
	}

	return FoundLabel;
}

void FPathFinderRouteOverlayRenderer::RemoveStaleSegments(const TSet<FString>& CurrentSegmentKeys)
{
	TArray<FString> StaleSegmentKeys;
	for (const TPair<FString, TWeakObjectPtr<UPathFinderRouteOverlayComponent>>& OverlayComponentEntry : OverlayComponentsBySegmentKey)
	{
		if (!CurrentSegmentKeys.Contains(OverlayComponentEntry.Key))
		{
			StaleSegmentKeys.Add(OverlayComponentEntry.Key);
		}
	}

	for (const FString& StaleSegmentKey : StaleSegmentKeys)
	{
		TWeakObjectPtr<UPathFinderRouteOverlayComponent>* OverlayComponentPointer = OverlayComponentsBySegmentKey.Find(StaleSegmentKey);
		if (OverlayComponentPointer != nullptr)
		{
			UPathFinderRouteOverlayComponent* OverlayComponent = OverlayComponentPointer->Get();
			if (OverlayComponent != nullptr)
			{
				OverlayComponent->DestroyComponent();
			}
		}

		OverlayComponentsBySegmentKey.Remove(StaleSegmentKey);
	}
}

void FPathFinderRouteOverlayRenderer::Clear()
{
	{
		FScopeLock Lock(&QueuedLabelUpdatesCriticalSection);
		QueuedLabelUpdates.Empty();
	}

	for (TPair<FString, TWeakObjectPtr<UPathFinderRouteOverlayComponent>>& OverlayComponentEntry : OverlayComponentsBySegmentKey)
	{
		UPathFinderRouteOverlayComponent* OverlayComponent = OverlayComponentEntry.Value.Get();
		if (OverlayComponent != nullptr)
		{
			OverlayComponent->DestroyComponent();
		}
	}

	OverlayComponentsBySegmentKey.Empty();
}
