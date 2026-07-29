#pragma once

#include "CoreMinimal.h"
#include "Equipment/FGEquipment.h"
#include "PathFinderControlTool.generated.h"

class UStaticMeshComponent;
class USceneComponent;
class UTextRenderComponent;

UENUM(BlueprintType)
enum class EPathFinderControlMode : uint8
{
	Off,
	TrafficLoad,
	NetworkUsage
};

UCLASS()
class PATHFINDER_API APathFinderControlTool final : public AFGEquipment
{
	GENERATED_BODY()

public:
	APathFinderControlTool();

protected:
	virtual void WasEquipped_Implementation() override;
	virtual void WasUnEquipped_Implementation() override;
	virtual void HandleDefaultEquipmentActionEvent(EDefaultEquipmentAction Action, EDefaultEquipmentActionEvent ActionEvent) override;

private:
	static constexpr int32 ControlModeCount = 3;

	void CycleControlMode();
	void ApplyControlMode();
	void ApplyOffMode();
	void SetWorldFilterMode(EPathFinderControlMode ControlMode) const;
	void ShowCurrentSelection() const;
	void ShowNotification(const FText& Message) const;
	FText GetControlModeName() const;
	FText GetControlModeDisplayText() const;

	UPROPERTY(VisibleAnywhere, Category = "PathFinder|Control Tool")
	TObjectPtr<USceneComponent> ToolRoot;

	UPROPERTY(VisibleAnywhere, Category = "PathFinder|Control Tool")
	TObjectPtr<UStaticMeshComponent> ToolBody;

	UPROPERTY(VisibleAnywhere, Category = "PathFinder|Control Tool")
	TObjectPtr<UTextRenderComponent> ModeText;

	EPathFinderControlMode SelectedControlMode{EPathFinderControlMode::Off};
};
