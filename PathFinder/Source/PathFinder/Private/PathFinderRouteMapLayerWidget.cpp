#include "PathFinderRouteMapLayerWidget.h"

#include "Blueprint/WidgetTree.h"
#include "Components/Image.h"
#include "Engine/CanvasRenderTarget2D.h"
#include "PathFinderRouteMapSubsystem.h"

TSharedRef<SWidget> UPathFinderRouteMapLayerWidget::RebuildWidget()
{
	if (WidgetTree->RootWidget == nullptr)
	{
		RouteImage = WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(), TEXT("PathFinderRouteMapImage"));
		RouteImage->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		WidgetTree->RootWidget = RouteImage;
	}
	return Super::RebuildWidget();
}

void UPathFinderRouteMapLayerWidget::NativeConstruct()
{
	Super::NativeConstruct();
	SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UPathFinderRouteMapSubsystem* Subsystem = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPathFinderRouteMapSubsystem>()
		: nullptr;
	if (Subsystem != nullptr && !Subsystem->OnVisibilityChanged.IsAlreadyBound(this, &UPathFinderRouteMapLayerWidget::HandleVisibilityChanged))
	{
		Subsystem->OnVisibilityChanged.AddDynamic(this, &UPathFinderRouteMapLayerWidget::HandleVisibilityChanged);
	}
	RefreshLayer();
}

void UPathFinderRouteMapLayerWidget::NativeDestruct()
{
	UPathFinderRouteMapSubsystem* Subsystem = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPathFinderRouteMapSubsystem>()
		: nullptr;
	if (Subsystem != nullptr)
	{
		Subsystem->OnVisibilityChanged.RemoveDynamic(this, &UPathFinderRouteMapLayerWidget::HandleVisibilityChanged);
	}
	Super::NativeDestruct();
}

void UPathFinderRouteMapLayerWidget::HandleVisibilityChanged(bool bVisible)
{
	RefreshLayer();
}

void UPathFinderRouteMapLayerWidget::RefreshLayer()
{
	UPathFinderRouteMapSubsystem* Subsystem = GetGameInstance() != nullptr
		? GetGameInstance()->GetSubsystem<UPathFinderRouteMapSubsystem>()
		: nullptr;
	const bool bVisible = Subsystem != nullptr && Subsystem->IsRouteLayerVisible();
	SetVisibility(bVisible ? ESlateVisibility::SelfHitTestInvisible : ESlateVisibility::Collapsed);
	if (!bVisible || RouteImage == nullptr)
	{
		return;
	}

	UCanvasRenderTarget2D* RenderTarget = Subsystem->AcquireRenderTarget();
	if (RenderTarget != nullptr)
	{
		FSlateBrush RouteBrush;
		RouteBrush.SetResourceObject(RenderTarget);
		RouteBrush.DrawAs = ESlateBrushDrawType::Image;
		RouteBrush.ImageSize = FVector2D(RenderTarget->SizeX, RenderTarget->SizeY);
		RouteImage->SetBrush(RouteBrush);
	}
}
