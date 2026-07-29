#include "PathFinderRouteOverlayComponent.h"

#include "Components/SceneComponent.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "WheeledVehicles/FGVehiclePathSegment.h"

namespace
{
	constexpr float PathFinderOverlayStripWidthScale = 0.7f;
	constexpr float PathFinderOverlayStripHeightScale = 0.08f;
	constexpr float PathFinderOverlayLabelLiftCentimeters = 32.0f;
	constexpr float PathFinderOverlayLabelWorldSize = 90.0f;
	const TCHAR* PathFinderRouteTrafficMaterialPath = TEXT("/PathFinder/RouteTraffic/M_PathFinderRouteTraffic.M_PathFinderRouteTraffic");

	struct FPathFinderOverlayMaterialResolution
	{
		UMaterialInterface* Material{nullptr};
		const TCHAR* ObjectPath{TEXT("none")};
		FString ProbeSummary;
	};

	FVector GetPathFinderOverlaySplinePointDirection(USplineComponent* SplineComponent, const int32 SplinePointIndex)
	{
		FVector Direction = SplineComponent->GetTangentAtSplinePoint(SplinePointIndex, ESplineCoordinateSpace::World);
		Direction.Z = 0.0f;
		if (Direction.Normalize())
		{
			return Direction;
		}

		if (SplinePointIndex + 1 < SplineComponent->GetNumberOfSplinePoints())
		{
			Direction = SplineComponent->GetLocationAtSplinePoint(SplinePointIndex + 1, ESplineCoordinateSpace::World) - SplineComponent->GetLocationAtSplinePoint(SplinePointIndex, ESplineCoordinateSpace::World);
		}
		else if (SplinePointIndex > 0)
		{
			Direction = SplineComponent->GetLocationAtSplinePoint(SplinePointIndex, ESplineCoordinateSpace::World) - SplineComponent->GetLocationAtSplinePoint(SplinePointIndex - 1, ESplineCoordinateSpace::World);
		}

		Direction.Z = 0.0f;
		if (!Direction.Normalize())
		{
			return FVector::ZeroVector;
		}

		return Direction;
	}

	FPathFinderOverlayMaterialResolution ResolvePathFinderOverlaySplineMaterialWithStatus()
	{
		UMaterialInterface* PathFinderRouteTrafficMaterial = LoadObject<UMaterialInterface>(nullptr, PathFinderRouteTrafficMaterialPath);
		const FString ProbeSummary = FString::Printf(
			TEXT("%s=%s"),
			PathFinderRouteTrafficMaterialPath,
			PathFinderRouteTrafficMaterial != nullptr ? TEXT("loaded") : TEXT("missing"));

		if (PathFinderRouteTrafficMaterial == nullptr)
		{
			return { nullptr, PathFinderRouteTrafficMaterialPath, ProbeSummary };
		}

		return { PathFinderRouteTrafficMaterial, PathFinderRouteTrafficMaterialPath, ProbeSummary };
	}

	UMaterialInterface* ResolvePathFinderOverlaySplineMaterial()
	{
		return ResolvePathFinderOverlaySplineMaterialWithStatus().Material;
	}

	void ApplyPathFinderOverlayMaterialColor(UMaterialInstanceDynamic* DynamicMaterialInstance, const FLinearColor& Color)
	{
		if (DynamicMaterialInstance == nullptr)
		{
			return;
		}

		DynamicMaterialInstance->SetVectorParameterValue(TEXT("Color Override"), Color);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("VehiclePathColor"), Color);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("Legacy Path Color"), Color);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("BlockedVehiclePathColor"), Color);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("DeadEndVehiclePathColor"), Color);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("IntersectionVehiclePathColor"), Color);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("IrrelevantVehiclePathColor"), Color);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("PathFinderColor"), Color);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("Color"), Color);
		DynamicMaterialInstance->SetVectorParameterValue(TEXT("BaseColor"), Color);
		DynamicMaterialInstance->SetScalarParameterValue(TEXT("PathFinderOpacity"), 0.9f);
		DynamicMaterialInstance->SetScalarParameterValue(TEXT("LineOpacity"), 1.0f);
	}

	UStaticMesh* ResolvePathFinderOverlaySplineStaticMesh()
	{
		return LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	}

	bool ResolvePathFinderOverlayLabelPlacement(USplineComponent* OffsetSplineComponent, FVector& LabelLocation, FVector& LabelDirection)
	{
		if (OffsetSplineComponent == nullptr || OffsetSplineComponent->GetNumberOfSplinePoints() < 2)
		{
			return false;
		}

		const float SplineLength = OffsetSplineComponent->GetSplineLength();
		const float LabelDistance = SplineLength * 0.5f;
		LabelLocation = OffsetSplineComponent->GetLocationAtDistanceAlongSpline(LabelDistance, ESplineCoordinateSpace::World) + FVector(0.0f, 0.0f, PathFinderOverlayLabelLiftCentimeters);
		LabelDirection = OffsetSplineComponent->GetTangentAtDistanceAlongSpline(LabelDistance, ESplineCoordinateSpace::World);
		LabelDirection.Z = 0.0f;
		if (LabelDirection.Normalize())
		{
			return true;
		}

		LabelDirection = OffsetSplineComponent->GetLocationAtSplinePoint(1, ESplineCoordinateSpace::World) - OffsetSplineComponent->GetLocationAtSplinePoint(0, ESplineCoordinateSpace::World);
		LabelDirection.Z = 0.0f;
		return LabelDirection.Normalize();
	}
}

bool UPathFinderRouteOverlayComponent::DrawPaintSample(AFGVehiclePathSegment* SegmentActor, const FLinearColor& Color, const bool bColorChanged, const FString& InitialLabelText)
{
	if (!EnsureOffsetSpline(SegmentActor))
	{
		return false;
	}

	UpdateSplineMeshes(Color, bColorChanged);
	if (InitialLabelText.IsEmpty())
	{
		if (LabelComponent != nullptr)
		{
			LabelComponent->SetVisibility(false);
		}
	}
	else
	{
		EnsureLabel(InitialLabelText);
		if (LabelComponent != nullptr)
		{
			LabelComponent->SetVisibility(true);
		}
	}
	return OffsetSplineComponent != nullptr && OffsetSplineComponent->GetNumberOfSplinePoints() > 1;
}

void UPathFinderRouteOverlayComponent::SetLabelText(const FString& LabelText)
{
	if (LabelComponent != nullptr)
	{
		LabelComponent->SetText(FText::FromString(LabelText));
	}
}

void UPathFinderRouteOverlayComponent::SetColor(const FLinearColor& Color)
{
	for (UMaterialInstanceDynamic* DynamicMaterialInstance : DynamicMaterialInstances)
	{
		ApplyPathFinderOverlayMaterialColor(DynamicMaterialInstance, Color);
	}
}

bool UPathFinderRouteOverlayComponent::GetLabelGeometry(
	FVector& LabelLocation,
	FRotator& LabelRotation,
	FVector& TextPlaneNormal,
	FVector& TextDirection,
	FVector& StripDirection) const
{
	if (LabelComponent == nullptr || OffsetSplineComponent == nullptr)
	{
		return false;
	}

	FVector ResolvedLabelLocation = FVector::ZeroVector;
	if (!ResolvePathFinderOverlayLabelPlacement(
		OffsetSplineComponent,
		ResolvedLabelLocation,
		StripDirection))
	{
		return false;
	}

	LabelLocation = LabelComponent->GetComponentLocation();
	LabelRotation = LabelComponent->GetComponentRotation();
	const FRotationMatrix RotationMatrix(LabelRotation);
	TextPlaneNormal = RotationMatrix.GetUnitAxis(EAxis::X);
	TextDirection = RotationMatrix.GetUnitAxis(EAxis::Y);
	return true;
}

FRotator UPathFinderRouteOverlayComponent::MakeHorizontalLabelRotation(const FVector& LabelDirection)
{
	return FRotationMatrix::MakeFromXY(FVector::UpVector, LabelDirection).Rotator();
}

FString UPathFinderRouteOverlayComponent::DescribeRouteOverlayMaterialProbe()
{
	const FPathFinderOverlayMaterialResolution Resolution = ResolvePathFinderOverlaySplineMaterialWithStatus();
	if (Resolution.Material == nullptr)
	{
		return FString::Printf(
			TEXT("PathFinder route overlay material: custom=missing path=%s resolved=none probes=%s"),
			Resolution.ObjectPath,
			*Resolution.ProbeSummary);
	}

	return FString::Printf(
		TEXT("PathFinder route overlay material: custom=loaded path=%s resolved=%s probes=%s"),
		Resolution.ObjectPath,
		*Resolution.Material->GetPathName(),
		*Resolution.ProbeSummary);
}

void UPathFinderRouteOverlayComponent::OnComponentDestroyed(const bool bDestroyingHierarchy)
{
	DestroyOverlayComponents();
	Super::OnComponentDestroyed(bDestroyingHierarchy);
}

bool UPathFinderRouteOverlayComponent::EnsureOffsetSpline(AFGVehiclePathSegment* SegmentActor)
{
	constexpr float CenterOffsetCentimeters = 250.0f;
	constexpr float LiftCentimeters = 12.0f;

	AActor* MeshOwner = GetOwner();
	if (SegmentActor == nullptr || MeshOwner == nullptr || MeshOwner->GetRootComponent() == nullptr)
	{
		return false;
	}

	USplineComponent* SourceSplineComponent = SegmentActor->GetSplineComponent();
	if (SourceSplineComponent == nullptr || SourceSplineComponent->GetNumberOfSplinePoints() < 2)
	{
		return false;
	}

	if (OffsetSplineComponent != nullptr)
	{
		return OffsetSplineComponent->GetNumberOfSplinePoints() > 1;
	}

	if (OffsetSplineComponent == nullptr)
	{
		const FName ComponentName = MakeUniqueObjectName(MeshOwner, USplineComponent::StaticClass(), TEXT("PathFinderRouteOverlayOffsetSpline"));
		OffsetSplineComponent = NewObject<USplineComponent>(MeshOwner, USplineComponent::StaticClass(), ComponentName);
		if (OffsetSplineComponent == nullptr)
		{
			return false;
		}

		OffsetSplineComponent->SetMobility(EComponentMobility::Movable);
		OffsetSplineComponent->SetupAttachment(MeshOwner->GetRootComponent());
		OffsetSplineComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		OffsetSplineComponent->SetGenerateOverlapEvents(false);
		OffsetSplineComponent->SetHiddenInGame(true);
		MeshOwner->AddInstanceComponent(OffsetSplineComponent);
		OffsetSplineComponent->RegisterComponent();
	}

	const FTransform OwnerTransform = MeshOwner->GetActorTransform();
	OffsetSplineComponent->ClearSplinePoints(false);

	const int32 SplinePointCount = SourceSplineComponent->GetNumberOfSplinePoints();
	for (int32 SplinePointIndex = 0; SplinePointIndex < SplinePointCount; ++SplinePointIndex)
	{
		const FVector Direction = GetPathFinderOverlaySplinePointDirection(SourceSplineComponent, SplinePointIndex);
		if (Direction.IsNearlyZero())
		{
			return false;
		}

		const FVector RightVector(-Direction.Y, Direction.X, 0.0f);
		const FVector SourceWorldLocation = SourceSplineComponent->GetLocationAtSplinePoint(SplinePointIndex, ESplineCoordinateSpace::World);
		const FVector OffsetWorldLocation = SourceWorldLocation + (RightVector * CenterOffsetCentimeters) + FVector(0.0f, 0.0f, LiftCentimeters);
		const FVector OffsetLocalLocation = OwnerTransform.InverseTransformPosition(OffsetWorldLocation);
		const FVector SourceWorldTangent = SourceSplineComponent->GetTangentAtSplinePoint(SplinePointIndex, ESplineCoordinateSpace::World);
		const FVector OffsetLocalTangent = OwnerTransform.InverseTransformVectorNoScale(SourceWorldTangent);

		OffsetSplineComponent->AddSplinePoint(OffsetLocalLocation, ESplineCoordinateSpace::Local, false);
		OffsetSplineComponent->SetTangentAtSplinePoint(SplinePointIndex, OffsetLocalTangent, ESplineCoordinateSpace::Local, false);
		OffsetSplineComponent->SetSplinePointType(SplinePointIndex, SourceSplineComponent->GetSplinePointType(SplinePointIndex), false);
	}

	OffsetSplineComponent->UpdateSpline();
	bOverlayGeometryDirty = true;
	return true;
}

bool UPathFinderRouteOverlayComponent::EnsureLabel(const FString& InitialLabelText)
{
	if (LabelComponent != nullptr)
	{
		return true;
	}

	AActor* MeshOwner = GetOwner();
	if (MeshOwner == nullptr || MeshOwner->GetRootComponent() == nullptr || OffsetSplineComponent == nullptr)
	{
		return false;
	}

	FVector LabelLocation = FVector::ZeroVector;
	FVector LabelDirection = FVector::ZeroVector;
	if (!ResolvePathFinderOverlayLabelPlacement(OffsetSplineComponent, LabelLocation, LabelDirection))
	{
		return false;
	}

	const FName ComponentName = MakeUniqueObjectName(MeshOwner, UTextRenderComponent::StaticClass(), TEXT("PathFinderRouteOverlayLoadLabel"));
	LabelComponent = NewObject<UTextRenderComponent>(MeshOwner, UTextRenderComponent::StaticClass(), ComponentName);
	if (LabelComponent == nullptr)
	{
		return false;
	}

	LabelComponent->SetMobility(EComponentMobility::Movable);
	LabelComponent->SetupAttachment(MeshOwner->GetRootComponent());
	LabelComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	LabelComponent->SetGenerateOverlapEvents(false);
	LabelComponent->SetCastShadow(false);
	LabelComponent->SetHorizontalAlignment(EHTA_Center);
	LabelComponent->SetVerticalAlignment(EVRTA_TextCenter);
	LabelComponent->SetWorldSize(PathFinderOverlayLabelWorldSize);
	LabelComponent->SetTextRenderColor(FColor::White);
	LabelComponent->SetText(FText::FromString(InitialLabelText));
	MeshOwner->AddInstanceComponent(LabelComponent);
	LabelComponent->RegisterComponent();
	const FRotator LabelRotation = MakeHorizontalLabelRotation(LabelDirection);
	LabelComponent->SetWorldLocation(LabelLocation);
	LabelComponent->SetWorldRotation(LabelRotation);
	return true;
}

void UPathFinderRouteOverlayComponent::UpdateSplineMeshes(const FLinearColor& Color, const bool bColorChanged)
{
	AActor* MeshOwner = GetOwner();
	UStaticMesh* StripStaticMesh = ResolvePathFinderOverlaySplineStaticMesh();
	if (MeshOwner == nullptr || OffsetSplineComponent == nullptr || StripStaticMesh == nullptr)
	{
		return;
	}

	const int32 DesiredMeshComponentCount = FMath::Max(0, OffsetSplineComponent->GetNumberOfSplinePoints() - 1);
	const bool bMeshComponentCountChanged = SplineMeshComponents.Num() != DesiredMeshComponentCount;
	const bool bShouldUpdateGeometry = bOverlayGeometryDirty || bMeshComponentCountChanged;
	while (SplineMeshComponents.Num() > DesiredMeshComponentCount)
	{
		USplineMeshComponent* ExtraSplineMeshComponent = SplineMeshComponents.Last();
		if (ExtraSplineMeshComponent != nullptr)
		{
			ExtraSplineMeshComponent->DestroyComponent();
		}

		SplineMeshComponents.RemoveAt(SplineMeshComponents.Num() - 1);
		if (DynamicMaterialInstances.Num() > SplineMeshComponents.Num())
		{
			DynamicMaterialInstances.RemoveAt(DynamicMaterialInstances.Num() - 1);
		}
	}

	while (SplineMeshComponents.Num() < DesiredMeshComponentCount)
	{
		const FName ComponentName = MakeUniqueObjectName(MeshOwner, USplineMeshComponent::StaticClass(), TEXT("PathFinderRouteOverlaySplineMesh"));
		USplineMeshComponent* SplineMeshComponent = NewObject<USplineMeshComponent>(MeshOwner, USplineMeshComponent::StaticClass(), ComponentName);
		if (SplineMeshComponent == nullptr)
		{
			return;
		}

		SplineMeshComponent->SetMobility(EComponentMobility::Movable);
		SplineMeshComponent->SetupAttachment(OffsetSplineComponent);
		SplineMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		SplineMeshComponent->SetGenerateOverlapEvents(false);
		SplineMeshComponent->SetCastShadow(false);
		SplineMeshComponent->SetStaticMesh(StripStaticMesh);
		SplineMeshComponent->SetForwardAxis(ESplineMeshAxis::X, false);
		SplineMeshComponent->SetSplineUpDir(FVector::UpVector, false);
		SplineMeshComponent->SetStartScale(FVector2D(PathFinderOverlayStripWidthScale, PathFinderOverlayStripHeightScale), false);
		SplineMeshComponent->SetEndScale(FVector2D(PathFinderOverlayStripWidthScale, PathFinderOverlayStripHeightScale), false);
		UMaterialInstanceDynamic* DynamicMaterialInstance = UMaterialInstanceDynamic::Create(ResolvePathFinderOverlaySplineMaterial(), SplineMeshComponent);
		if (DynamicMaterialInstance != nullptr)
		{
			SplineMeshComponent->SetMaterial(0, DynamicMaterialInstance);
		}
		else
		{
			SplineMeshComponent->SetMaterial(0, ResolvePathFinderOverlaySplineMaterial());
		}

		MeshOwner->AddInstanceComponent(SplineMeshComponent);
		SplineMeshComponent->RegisterComponent();
		SplineMeshComponents.Add(SplineMeshComponent);
		DynamicMaterialInstances.Add(DynamicMaterialInstance);
	}

	for (int32 SplineMeshIndex = 0; SplineMeshIndex < DesiredMeshComponentCount; ++SplineMeshIndex)
	{
		USplineMeshComponent* SplineMeshComponent = SplineMeshComponents[SplineMeshIndex];
		if (SplineMeshComponent == nullptr)
		{
			continue;
		}

		if (bShouldUpdateGeometry)
		{
			const FVector StartLocation = OffsetSplineComponent->GetLocationAtSplinePoint(SplineMeshIndex, ESplineCoordinateSpace::Local);
			const FVector StartTangent = OffsetSplineComponent->GetTangentAtSplinePoint(SplineMeshIndex, ESplineCoordinateSpace::Local);
			const FVector EndLocation = OffsetSplineComponent->GetLocationAtSplinePoint(SplineMeshIndex + 1, ESplineCoordinateSpace::Local);
			const FVector EndTangent = OffsetSplineComponent->GetTangentAtSplinePoint(SplineMeshIndex + 1, ESplineCoordinateSpace::Local);

			SplineMeshComponent->SetStartAndEnd(StartLocation, StartTangent, EndLocation, EndTangent, false);
			SplineMeshComponent->SetStartScale(FVector2D(PathFinderOverlayStripWidthScale, PathFinderOverlayStripHeightScale), false);
			SplineMeshComponent->SetEndScale(FVector2D(PathFinderOverlayStripWidthScale, PathFinderOverlayStripHeightScale), false);
		}

		UMaterialInstanceDynamic* DynamicMaterialInstance = nullptr;
		bool bCreatedDynamicMaterialInstance = false;
		if (DynamicMaterialInstances.IsValidIndex(SplineMeshIndex))
		{
			DynamicMaterialInstance = DynamicMaterialInstances[SplineMeshIndex];
		}

		if (DynamicMaterialInstance == nullptr)
		{
			DynamicMaterialInstance = UMaterialInstanceDynamic::Create(ResolvePathFinderOverlaySplineMaterial(), SplineMeshComponent);
			if (DynamicMaterialInstance != nullptr)
			{
				SplineMeshComponent->SetMaterial(0, DynamicMaterialInstance);
				bCreatedDynamicMaterialInstance = true;
				if (DynamicMaterialInstances.IsValidIndex(SplineMeshIndex))
				{
					DynamicMaterialInstances[SplineMeshIndex] = DynamicMaterialInstance;
				}
			}
		}

		if (bColorChanged || bMeshComponentCountChanged || bCreatedDynamicMaterialInstance)
		{
			ApplyPathFinderOverlayMaterialColor(DynamicMaterialInstance, Color);
		}

		if (bShouldUpdateGeometry)
		{
			SplineMeshComponent->UpdateMesh();
		}
	}

	bOverlayGeometryDirty = false;
}

void UPathFinderRouteOverlayComponent::DestroyOverlayComponents()
{
	for (USplineMeshComponent* SplineMeshComponent : SplineMeshComponents)
	{
		if (SplineMeshComponent != nullptr)
		{
			SplineMeshComponent->DestroyComponent();
		}
	}

	SplineMeshComponents.Empty();
	DynamicMaterialInstances.Empty();

	if (LabelComponent != nullptr)
	{
		LabelComponent->DestroyComponent();
		LabelComponent = nullptr;
	}

	if (OffsetSplineComponent != nullptr)
	{
		OffsetSplineComponent->DestroyComponent();
		OffsetSplineComponent = nullptr;
	}
}
