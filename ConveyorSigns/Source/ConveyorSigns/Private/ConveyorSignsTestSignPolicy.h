#pragma once

#include "CoreMinimal.h"

class FConveyorSignsTestSignPolicy
{
public:
	static FName GetSourceBaseName();
	static bool IsSourceName(const FName& actorName);
};
