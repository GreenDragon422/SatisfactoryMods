#pragma once

#include "CoreMinimal.h"

class FPathFinderInputTrace
{
public:
	void SetEnabled(bool bNewEnabled);
	bool IsEnabled() const;
	void LogSnapshot() const;

private:
	bool bEnabled{false};
};
