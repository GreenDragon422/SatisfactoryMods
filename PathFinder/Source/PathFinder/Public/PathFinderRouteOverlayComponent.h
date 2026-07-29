#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "PathFinderRouteOverlayComponent.generated.h"

class AFGVehiclePathSegment;
class UMaterialInstanceDynamic;
class USplineComponent;
class USplineMeshComponent;
class UTextRenderComponent;

UCLASS()
class UPathFinderRouteOverlayComponent final : public UActorComponent
{
	GENERATED_BODY()

public:
	bool DrawPaintSample(AFGVehiclePathSegment* SegmentActor, const FLinearColor& Color, bool bColorChanged, const FString& InitialLabelText);
	void SetLabelText(const FString& LabelText);
	void SetColor(const FLinearColor& Color);
	bool GetLabelGeometry(
		FVector& LabelLocation,
		FRotator& LabelRotation,
		FVector& TextPlaneNormal,
		FVector& TextDirection,
		FVector& StripDirection) const;
	static FString DescribeRouteOverlayMaterialProbe();
	static FRotator MakeHorizontalLabelRotation(const FVector& LabelDirection);

protected:
	virtual void OnComponentDestroyed(bool bDestroyingHierarchy) override;

private:
	bool EnsureOffsetSpline(AFGVehiclePathSegment* SegmentActor);
	bool EnsureLabel(const FString& InitialLabelText);
	void UpdateSplineMeshes(const FLinearColor& Color, bool bColorChanged);
	void DestroyOverlayComponents();

	UPROPERTY(Transient)
	TObjectPtr<USplineComponent> OffsetSplineComponent;

	UPROPERTY(Transient)
	TObjectPtr<UTextRenderComponent> LabelComponent;

	UPROPERTY(Transient)
	TArray<TObjectPtr<USplineMeshComponent>> SplineMeshComponents;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterialInstances;

	bool bOverlayGeometryDirty = true;
};
