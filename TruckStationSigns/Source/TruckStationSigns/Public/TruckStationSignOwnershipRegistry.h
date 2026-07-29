#pragma once

#include "CoreMinimal.h"

class TRUCKSTATIONSIGNS_API FTruckStationSignOwnershipRegistry final
{
public:
	UObject* Find(const UObject* station) const;
	bool TryAdd(UObject* station, UObject* sign);
	UObject* Remove(const UObject* station);
	int32 Num() const;
	void RemoveInvalidEntries();
	void Reset();

private:
	TMap<TWeakObjectPtr<UObject>, TWeakObjectPtr<UObject>> signsByStation;
};
