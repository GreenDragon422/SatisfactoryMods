#include "PathFinderControlToolRecipe.h"

#include "ItemAmount.h"
#include "PathFinderControlToolDescriptor.h"
#include "Resources/FGItemDescriptor.h"
#include "UObject/ConstructorHelpers.h"

UPathFinderControlToolRecipe::UPathFinderControlToolRecipe()
{
	mDisplayNameOverride = false;
	mManufacturingMenuPriority = 20.0f;
	mManufactoringDuration = 45.0f;
	mManualManufacturingMultiplier = 1.0f;

	static ConstructorHelpers::FClassFinder<UFGItemDescriptor> ReinforcedIronPlate(
		TEXT("/Game/FactoryGame/Resource/Parts/IronPlateReinforced/Desc_IronPlateReinforced"));
	static ConstructorHelpers::FClassFinder<UFGItemDescriptor> Cable(
		TEXT("/Game/FactoryGame/Resource/Parts/Cable/Desc_Cable"));
	static ConstructorHelpers::FClassFinder<UFGItemDescriptor> CircuitBoard(
		TEXT("/Game/FactoryGame/Resource/Parts/CircuitBoard/Desc_CircuitBoard"));

	if (ReinforcedIronPlate.Succeeded() && Cable.Succeeded() && CircuitBoard.Succeeded())
	{
		mIngredients.Emplace(ReinforcedIronPlate.Class, 6);
		mIngredients.Emplace(Cable.Class, 20);
		mIngredients.Emplace(CircuitBoard.Class, 4);
	}
	mProduct.Emplace(UPathFinderControlToolDescriptor::StaticClass(), 1);
	mProducedIn.Emplace(FSoftObjectPath(
		TEXT("/Game/FactoryGame/Buildable/-Shared/WorkBench/BP_WorkshopComponent.BP_WorkshopComponent_C")));
}
