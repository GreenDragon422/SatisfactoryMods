#include "PathFinderRootGameInstanceModule.h"

#include "Patching/WidgetBlueprintHookManager.h"
#include "PathFinderRouteMapLayerWidget.h"
#include "PathFinderRouteMapToggleWidget.h"

UPathFinderRootGameInstanceModule::UPathFinderRootGameInstanceModule()
{
	bRootModule = true;

	UWidgetBlueprintHookData* MapLayerHook = CreateDefaultSubobject<UWidgetBlueprintHookData>(TEXT("PathFinderRouteMapLayerHook"));
	MapLayerHook->DeveloperComment = TEXT("Adds the PathFinder vehicle-route render target to the vanilla world map so it inherits map pan and zoom");
	MapLayerHook->WidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/FactoryGame/Interface/UI/Minimap/Widget_Map.Widget_Map_C")));
	MapLayerHook->NewWidgetClass = UPathFinderRouteMapLayerWidget::StaticClass();
	MapLayerHook->NewWidgetName = TEXT("PathFinderRouteMapLayer");
	MapLayerHook->ParentWidgetType = EWidgetBlueprintHookParentType::Indirect_Child;
	MapLayerHook->ParentWidgetName = TEXT("mMap");
	MapLayerHook->ParentSlotIndex = 1;
	UWidgetBlueprintHookSlot_Generic* MapLayerSlot = CreateDefaultSubobject<UWidgetBlueprintHookSlot_Generic>(TEXT("PathFinderRouteMapLayerSlot"));
	MapLayerSlot->HorizontalAlignment = HAlign_Fill;
	MapLayerSlot->VerticalAlignment = VAlign_Fill;
	MapLayerHook->SlotConfiguration = MapLayerSlot;
	WidgetBlueprintHooks.Add(MapLayerHook);

	UWidgetBlueprintHookData* ToggleHook = CreateDefaultSubobject<UWidgetBlueprintHookData>(TEXT("PathFinderRouteMapToggleHook"));
	ToggleHook->DeveloperComment = TEXT("Adds a persistent vehicle-route visibility toggle beside the vanilla map menu controls");
	ToggleHook->WidgetClass = TSoftClassPtr<UUserWidget>(FSoftObjectPath(TEXT("/Game/FactoryGame/Interface/UI/Minimap/Widget_MapContainer.Widget_MapContainer_C")));
	ToggleHook->NewWidgetClass = UPathFinderRouteMapToggleWidget::StaticClass();
	ToggleHook->NewWidgetName = TEXT("PathFinderRouteMapToggle");
	ToggleHook->ParentWidgetType = EWidgetBlueprintHookParentType::Indirect_Child;
	ToggleHook->ParentWidgetName = TEXT("ShowHideButton");
	ToggleHook->ParentSlotIndex = 1;
	UWidgetBlueprintHookSlot_Canvas* ToggleSlot = CreateDefaultSubobject<UWidgetBlueprintHookSlot_Canvas>(TEXT("PathFinderRouteMapToggleSlot"));
	ToggleSlot->LayoutData.Anchors = FAnchors(0.0f, 0.0f);
	ToggleSlot->LayoutData.Offsets = FMargin(6.0f, 50.0f, 0.0f, 0.0f);
	ToggleSlot->LayoutData.Alignment = FVector2D::ZeroVector;
	ToggleSlot->bAutoSize = true;
	ToggleSlot->ZOrder = 10;
	ToggleHook->SlotConfiguration = ToggleSlot;
	WidgetBlueprintHooks.Add(ToggleHook);
}
