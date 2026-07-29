#include "PathFinderGameWorldModule.h"

#include "FGRecipeManager.h"
#include "PathFinderControlToolRecipe.h"
#include "Registry/ModContentRegistry.h"

UPathFinderGameWorldModule::UPathFinderGameWorldModule()
{
	bRootModule = true;
}

void UPathFinderGameWorldModule::DispatchLifecycleEvent(const ELifecyclePhase Phase)
{
	Super::DispatchLifecycleEvent(Phase);

	if (Phase == ELifecyclePhase::INITIALIZATION)
	{
		UModContentRegistry* ModContentRegistry = UModContentRegistry::Get(this);
		if (ModContentRegistry != nullptr)
		{
			ModContentRegistry->RegisterRecipe(GetOwnerModReference(), UPathFinderControlToolRecipe::StaticClass());
		}
	}
	else if (Phase == ELifecyclePhase::POST_INITIALIZATION && GetWorld()->GetAuthGameMode() != nullptr)
	{
		AFGRecipeManager* RecipeManager = AFGRecipeManager::Get(this);
		if (RecipeManager != nullptr)
		{
			RecipeManager->AddAvailableRecipe(UPathFinderControlToolRecipe::StaticClass());
		}
	}
}
