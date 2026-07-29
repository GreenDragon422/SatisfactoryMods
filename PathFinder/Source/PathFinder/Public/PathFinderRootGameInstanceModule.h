#pragma once

#include "CoreMinimal.h"
#include "Module/GameInstanceModule.h"
#include "PathFinderRootGameInstanceModule.generated.h"

UCLASS()
class PATHFINDER_API UPathFinderRootGameInstanceModule : public UGameInstanceModule
{
	GENERATED_BODY()

public:
	UPathFinderRootGameInstanceModule();
};
