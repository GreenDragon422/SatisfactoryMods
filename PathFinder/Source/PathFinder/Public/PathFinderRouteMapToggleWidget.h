#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PathFinderRouteMapToggleWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS()
class PATHFINDER_API UPathFinderRouteMapToggleWidget : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	UFUNCTION()
	void HandleClicked();

	UFUNCTION()
	void HandleVisibilityChanged(bool bVisible);

	void RefreshLabel();

	UPROPERTY(Transient)
	TObjectPtr<UButton> ToggleButton;

	UPROPERTY(Transient)
	TObjectPtr<UTextBlock> ToggleLabel;
};
