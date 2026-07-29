#include "TruckStationSignOwnershipRegistry.h"

UObject* FTruckStationSignOwnershipRegistry::Find(const UObject* station) const
{
	if (station == nullptr)
	{
		return nullptr;
	}

	const TWeakObjectPtr<UObject>* sign = signsByStation.Find(
		TWeakObjectPtr<UObject>(const_cast<UObject*>(station)));
	return sign != nullptr ? sign->Get() : nullptr;
}

bool FTruckStationSignOwnershipRegistry::TryAdd(UObject* station, UObject* sign)
{
	if (!IsValid(station) || !IsValid(sign))
	{
		return false;
	}

	const TWeakObjectPtr<UObject> stationKey(station);
	TWeakObjectPtr<UObject>* existingSign = signsByStation.Find(stationKey);
	if (existingSign != nullptr && existingSign->IsValid())
	{
		return false;
	}

	signsByStation.Add(stationKey, TWeakObjectPtr<UObject>(sign));
	return true;
}

UObject* FTruckStationSignOwnershipRegistry::Remove(const UObject* station)
{
	if (station == nullptr)
	{
		return nullptr;
	}

	const TWeakObjectPtr<UObject> stationKey(const_cast<UObject*>(station));
	TWeakObjectPtr<UObject> sign;
	if (!signsByStation.RemoveAndCopyValue(stationKey, sign))
	{
		return nullptr;
	}

	return sign.Get();
}

int32 FTruckStationSignOwnershipRegistry::Num() const
{
	return signsByStation.Num();
}

void FTruckStationSignOwnershipRegistry::RemoveInvalidEntries()
{
	TMap<TWeakObjectPtr<UObject>, TWeakObjectPtr<UObject>>::TIterator iterator = signsByStation.CreateIterator();
	while (iterator)
	{
		if (!iterator.Key().IsValid() || !iterator.Value().IsValid())
		{
			iterator.RemoveCurrent();
		}
		++iterator;
	}
}

void FTruckStationSignOwnershipRegistry::Reset()
{
	signsByStation.Reset();
}
