#pragma once

#include "CoreMinimal.h"
#include "Module/GameWorldModule.h"
#include "PathFinderGameWorldModule.generated.h"

UCLASS()
class PATHFINDER_API UPathFinderGameWorldModule final : public UGameWorldModule
{
	GENERATED_BODY()

public:
	UPathFinderGameWorldModule();
	virtual void DispatchLifecycleEvent(ELifecyclePhase Phase) override;
};
