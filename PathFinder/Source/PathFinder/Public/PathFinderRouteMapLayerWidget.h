#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PathFinderRouteMapLayerWidget.generated.h"

class UImage;

UCLASS()
class PATHFINDER_API UPathFinderRouteMapLayerWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleVisibilityChanged(bool bVisible);

	void RefreshLayer();

	UPROPERTY(Transient)
	TObjectPtr<UImage> RouteImage;
};
