#pragma once

#include "CoreMinimal.h"

struct FPathFinderTrafficLoadSampleResult
{
	bool bLoadTextChanged{false};
	bool bTrafficBucketChanged{false};
};

class FPathFinderTrafficLoadWindow
{
public:
	static constexpr int32 WindowSampleCount = 120;

	FPathFinderTrafficLoadSampleResult AddVehicleCountSample(const int32 VehicleCount)
	{
		VehicleCountSamples.Add(FMath::Max(0, VehicleCount));
		if (VehicleCountSamples.Num() > WindowSampleCount)
		{
			VehicleCountSamples.RemoveAt(0, VehicleCountSamples.Num() - WindowSampleCount, EAllowShrinking::No);
		}

		FPathFinderTrafficLoadSampleResult Result;
		const float VehicleLoad = GetLoad();
		const FString PreviousLoadText = CachedLoadText;
		CachedLoadText = FormatVehicleCountPerSecondText(VehicleLoad);
		Result.bLoadTextChanged = CachedLoadText != PreviousLoadText;

		const int32 PreviousTrafficBucket = CachedTrafficBucket;
		CachedTrafficBucket = CalculateTrafficBucket(VehicleLoad);
		Result.bTrafficBucketChanged = CachedTrafficBucket != PreviousTrafficBucket;
		return Result;
	}

	float GetLoad() const
	{
		int32 VehicleCountSampleTotal = 0;
		for (const int32 VehicleCountSample : VehicleCountSamples)
		{
			VehicleCountSampleTotal += VehicleCountSample;
		}

		return static_cast<float>(VehicleCountSampleTotal) / static_cast<float>(WindowSampleCount);
	}

	const FString& GetLoadText() const
	{
		return CachedLoadText;
	}

	int32 GetTrafficBucket() const
	{
		return CachedTrafficBucket;
	}

private:
	static int32 CalculateTrafficBucket(const float VehicleLoad)
	{
		if (VehicleLoad <= 0.0f)
		{
			return 0;
		}

		if (VehicleLoad < 0.25f)
		{
			return 1;
		}

		if (VehicleLoad < 0.75f)
		{
			return 2;
		}

		return 3;
	}

	static FString FormatVehicleCountPerSecondText(const float VehicleCountPerSecond)
	{
		return FString::Printf(TEXT("%.1f/s"), VehicleCountPerSecond);
	}

	TArray<int32> VehicleCountSamples;
	int32 CachedTrafficBucket{0};
	FString CachedLoadText{TEXT("0.0/s")};
};
