#include "PathFinderControlTool.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "FGPlayerController.h"
#include "UI/FGGameUI.h"
#include "UObject/ConstructorHelpers.h"

APathFinderControlTool::APathFinderControlTool()
{
	PrimaryActorTick.bCanEverTick = false;

	mEquipmentSlot = EEquipmentSlot::ES_ARMS;
	mAttachSocket = TEXT("hand_rSocket");
	mArmAnimation = EArmEquipment::AE_Generic1Hand;
	mNeedsDefaultEquipmentMappingContext = true;
	mDefaultEquipmentActions = static_cast<uint8>(EDefaultEquipmentAction::PrimaryFire)
		| static_cast<uint8>(EDefaultEquipmentAction::SecondaryFire);

	ToolRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ToolRoot"));
	SetRootComponent(ToolRoot);

	ToolBody = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ToolBody"));
	ToolBody->SetupAttachment(ToolRoot);
	ToolBody->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ToolBody->SetRelativeLocation(FVector(-3.0, -3.0, 0.75));
	ToolBody->SetRelativeScale3D(FVector::OneVector);
	ToolBody->SetRelativeRotation(FRotator(0.0, 125.743469, 0.0));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> RoutePatrolMesh(
		TEXT("/PathFinder/RoutePatrol/SM_RoutePatrolTool.SM_RoutePatrolTool"));
	if (RoutePatrolMesh.Succeeded())
	{
		ToolBody->SetStaticMesh(RoutePatrolMesh.Object);
	}

	ModeText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("ModeText"));
	ModeText->SetupAttachment(ToolBody);
	ModeText->SetHorizontalAlignment(EHTA_Center);
	ModeText->SetVerticalAlignment(EVRTA_TextCenter);
	ModeText->SetTextRenderColor(FColor(255, 98, 18));
	ModeText->SetWorldSize(0.55f);
	ModeText->SetRelativeLocation(FVector(0.0, 5.05, 10.4));
	ModeText->SetRelativeRotation(FRotator(0.0, 90.0, 0.0));
	ModeText->SetText(GetControlModeDisplayText());
}

void APathFinderControlTool::WasEquipped_Implementation()
{
	Super::WasEquipped_Implementation();

	SelectedControlMode = EPathFinderControlMode::Off;
	ApplyOffMode();
	ModeText->SetText(GetControlModeDisplayText());
	if (IsLocalInstigator())
	{
		ShowNotification(NSLOCTEXT("PathFinder", "ControlToolEquipped", "Route Filter: Secondary selects and activates a mode. Primary reapplies it."));
		ShowCurrentSelection();
	}
}

void APathFinderControlTool::WasUnEquipped_Implementation()
{
	ApplyOffMode();
	SelectedControlMode = EPathFinderControlMode::Off;
	ModeText->SetText(GetControlModeDisplayText());
	Super::WasUnEquipped_Implementation();
}

void APathFinderControlTool::HandleDefaultEquipmentActionEvent(const EDefaultEquipmentAction Action, const EDefaultEquipmentActionEvent ActionEvent)
{
	Super::HandleDefaultEquipmentActionEvent(Action, ActionEvent);

	if (!IsLocalInstigator() || ActionEvent != EDefaultEquipmentActionEvent::Pressed)
	{
		return;
	}

	if (Action == EDefaultEquipmentAction::PrimaryFire)
	{
		ApplyControlMode();
	}
	else if (Action == EDefaultEquipmentAction::SecondaryFire)
	{
		CycleControlMode();
	}
}

void APathFinderControlTool::CycleControlMode()
{
	const int32 NextModeIndex = (static_cast<int32>(SelectedControlMode) + 1) % ControlModeCount;
	SelectedControlMode = static_cast<EPathFinderControlMode>(NextModeIndex);
	ModeText->SetText(GetControlModeDisplayText());
	ApplyControlMode();
}

void APathFinderControlTool::ApplyControlMode()
{
	SetWorldFilterMode(SelectedControlMode);
	ShowCurrentSelection();
}

void APathFinderControlTool::ApplyOffMode()
{
	SetWorldFilterMode(EPathFinderControlMode::Off);
}

void APathFinderControlTool::SetWorldFilterMode(const EPathFinderControlMode ControlMode) const
{
	UWorld* World = GetWorld();
	if (GEngine != nullptr && World != nullptr)
	{
		const TCHAR* Command = ControlMode == EPathFinderControlMode::TrafficLoad
			? TEXT("PathFinder.VehicleTracing on")
			: ControlMode == EPathFinderControlMode::NetworkUsage
				? TEXT("PathFinder.VehicleTracing network")
				: TEXT("PathFinder.VehicleTracing off");
		GEngine->Exec(World, Command);
	}
}

void APathFinderControlTool::ShowCurrentSelection() const
{
	ShowNotification(FText::Format(
		NSLOCTEXT("PathFinder", "ControlToolSelection", "Route Filter: {0}"),
		GetControlModeName()));
}

void APathFinderControlTool::ShowNotification(const FText& Message) const
{
	AFGPlayerController* PlayerController = Cast<AFGPlayerController>(GetInstigatorController());
	UFGGameUI* GameUI = PlayerController != nullptr ? PlayerController->GetGameUI() : nullptr;
	if (GameUI != nullptr)
	{
		GameUI->ShowTextNotification(Message);
	}
}

FText APathFinderControlTool::GetControlModeName() const
{
	switch (SelectedControlMode)
	{
	case EPathFinderControlMode::Off:
		return NSLOCTEXT("PathFinder", "ControlToolOff", "Off");
	case EPathFinderControlMode::TrafficLoad:
		return NSLOCTEXT("PathFinder", "ControlToolTrafficLoad", "Traffic Load");
	case EPathFinderControlMode::NetworkUsage:
		return NSLOCTEXT("PathFinder", "ControlToolNetworkUsage", "Network Usage");
	default:
		return FText::GetEmpty();
	}
}

FText APathFinderControlTool::GetControlModeDisplayText() const
{
	switch (SelectedControlMode)
	{
	case EPathFinderControlMode::Off:
		return NSLOCTEXT("PathFinder", "ControlToolOffDisplay", "OFF");
	case EPathFinderControlMode::TrafficLoad:
		return NSLOCTEXT("PathFinder", "ControlToolTrafficLoadDisplay", "TRAFFIC\nLOAD");
	case EPathFinderControlMode::NetworkUsage:
		return NSLOCTEXT("PathFinder", "ControlToolNetworkUsageDisplay", "NETWORK\nUSAGE");
	default:
		return FText::GetEmpty();
	}
}
