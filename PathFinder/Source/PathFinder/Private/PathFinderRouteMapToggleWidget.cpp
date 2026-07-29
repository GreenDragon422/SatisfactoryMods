#include "PathFinderRouteMapToggleWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "PathFinderRouteMapSubsystem.h"

namespace
{
	const TCHAR* PathFinderRouteToggleWhiteTexturePath = TEXT("/Game/FactoryGame/Interface/UI/Assets/Shared/01_White.01_White");
	const FLinearColor PathFinderRouteToggleFicsitOrange = FLinearColor::FromSRGBColor(FColor(242, 101, 17));
	const FLinearColor PathFinderRouteToggleDarkPanelColor = FLinearColor::FromSRGBColor(FColor(28, 29, 31));
	const FLinearColor PathFinderRouteToggleHoverPanelColor = FLinearColor::FromSRGBColor(FColor(48, 49, 52));
}

TSharedRef<SWidget> UPathFinderRouteMapToggleWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget == nullptr)
	{
		USizeBox* RootSizeBox = WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(), TEXT("PathFinderRouteToggleSize"));
		RootSizeBox->SetMinDesiredHeight(38.0f);

		ToggleButton = WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(), TEXT("PathFinderRouteToggleButton"));
		RootSizeBox->AddChild(ToggleButton);

		UTexture2D* WhiteTexture = LoadObject<UTexture2D>(nullptr, PathFinderRouteToggleWhiteTexturePath);
		FSlateBrush PanelBrush;
		PanelBrush.SetResourceObject(WhiteTexture);
		PanelBrush.DrawAs = ESlateBrushDrawType::Image;
		FSlateBrush NormalBrush = PanelBrush;
		NormalBrush.TintColor = FSlateColor(PathFinderRouteToggleDarkPanelColor);
		FSlateBrush HoveredBrush = PanelBrush;
		HoveredBrush.TintColor = FSlateColor(PathFinderRouteToggleHoverPanelColor);
		FButtonStyle ButtonStyle;
		ButtonStyle.SetNormal(NormalBrush);
		ButtonStyle.SetHovered(HoveredBrush);
		ButtonStyle.SetPressed(HoveredBrush);
		ButtonStyle.SetNormalPadding(FMargin(0.0f));
		ButtonStyle.SetPressedPadding(FMargin(0.0f));
		ToggleButton->SetStyle(ButtonStyle);

		ToggleLabel = WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(), TEXT("PathFinderRouteToggleLabel"));
		ToggleLabel->SetJustification(ETextJustify::Center);
		FSlateFontInfo LabelFont = ToggleLabel->GetFont();
		LabelFont.Size = 15;
		ToggleLabel->SetFont(LabelFont);
		ToggleButton->AddChild(ToggleLabel);
		if (UButtonSlot* LabelSlot = Cast<UButtonSlot>(ToggleLabel->Slot))
		{
			LabelSlot->SetPadding(FMargin(12.0f, 6.0f));
			LabelSlot->SetHorizontalAlignment(HAlign_Fill);
			LabelSlot->SetVerticalAlignment(VAlign_Center);
		}

		WidgetTree->RootWidget = RootSizeBox;
	}
	return Super::RebuildWidget();
}

void UPathFinderRouteMapToggleWidget::NativeConstruct()
{
	Super::NativeConstruct();
	if (ToggleButton != nullptr && !ToggleButton->OnClicked.IsAlreadyBound(this, &UPathFinderRouteMapToggleWidget::HandleClicked))
	{
		ToggleButton->OnClicked.AddDynamic(this, &UPathFinderRouteMapToggleWidget::HandleClicked);
	}

	UPathFinderRouteMapSubsystem* Subsystem = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPathFinderRouteMapSubsystem>()
		: nullptr;
	if (Subsystem != nullptr && !Subsystem->OnVisibilityChanged.IsAlreadyBound(this, &UPathFinderRouteMapToggleWidget::HandleVisibilityChanged))
	{
		Subsystem->OnVisibilityChanged.AddDynamic(this, &UPathFinderRouteMapToggleWidget::HandleVisibilityChanged);
	}
	RefreshLabel();
}

void UPathFinderRouteMapToggleWidget::NativeDestruct()
{
	UPathFinderRouteMapSubsystem* Subsystem = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPathFinderRouteMapSubsystem>()
		: nullptr;
	if (Subsystem != nullptr)
	{
		Subsystem->OnVisibilityChanged.RemoveDynamic(this, &UPathFinderRouteMapToggleWidget::HandleVisibilityChanged);
	}
	Super::NativeDestruct();
}

void UPathFinderRouteMapToggleWidget::HandleClicked()
{
	UPathFinderRouteMapSubsystem* Subsystem = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPathFinderRouteMapSubsystem>()
		: nullptr;
	if (Subsystem != nullptr)
	{
		Subsystem->ToggleRouteLayerVisibility();
	}
}

void UPathFinderRouteMapToggleWidget::HandleVisibilityChanged(bool bVisible)
{
	RefreshLabel();
}

void UPathFinderRouteMapToggleWidget::RefreshLabel()
{
	if (ToggleLabel == nullptr)
	{
		return;
	}

	UPathFinderRouteMapSubsystem* Subsystem = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPathFinderRouteMapSubsystem>()
		: nullptr;
	const bool bVisible = Subsystem == nullptr || Subsystem->IsRouteLayerVisible();
	ToggleLabel->SetText(bVisible
		? NSLOCTEXT("PathFinder", "RouteMapVisible", "Vehicle Routes: ON")
		: NSLOCTEXT("PathFinder", "RouteMapHidden", "Vehicle Routes: OFF"));
	ToggleLabel->SetColorAndOpacity(FSlateColor(bVisible ? PathFinderRouteToggleFicsitOrange : FLinearColor::Gray));
	SetToolTipText(bVisible
		? NSLOCTEXT("PathFinder", "HideRouteMapTooltip", "Hide vehicle routes on the map")
		: NSLOCTEXT("PathFinder", "ShowRouteMapTooltip", "Show vehicle routes on the map"));
}
