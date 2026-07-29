#if WITH_DEV_AUTOMATION_TESTS

#include "Math/UnrealMathUtility.h"
#include "Misc/AutomationTest.h"
#include "PathFinderRouteOverlayComponent.h"
#include "PathFinderTrafficLoad.h"

namespace
{
	bool TestPathFinderNearlyEqual(FAutomationTestBase& Test, const TCHAR* What, const float Actual, const float Expected)
	{
		constexpr float Tolerance = 0.0001f;
		return Test.TestTrue(What, FMath::IsNearlyEqual(Actual, Expected, Tolerance));
	}

	bool TestPathFinderDirectionNearlyEqual(FAutomationTestBase& Test, const TCHAR* What, const FVector& Actual, const FVector& Expected)
	{
		constexpr float Tolerance = 0.0001f;
		return Test.TestTrue(What, Actual.GetSafeNormal().Equals(Expected.GetSafeNormal(), Tolerance));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFinderTrafficLoadLabelRotationFollowsTrafficStripTest, "PathFinder.TrafficLoad.LabelRotationFollowsTrafficStrip", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderTrafficLoadLabelRotationFollowsTrafficStripTest::RunTest(const FString& Parameters)
{
	const FVector LabelDirection = FVector::ForwardVector;
	const FRotator LabelRotation = UPathFinderRouteOverlayComponent::MakeHorizontalLabelRotation(LabelDirection);
	const FVector TextPlaneNormal = FRotationMatrix(LabelRotation).GetUnitAxis(EAxis::X);
	const FVector TextHorizontalDirection = FRotationMatrix(LabelRotation).GetUnitAxis(EAxis::Y);

	bool bPassed = true;
	bPassed &= TestPathFinderDirectionNearlyEqual(*this, TEXT("Label local X axis should face up so the text plane lies on the route strip."), TextPlaneNormal, FVector::UpVector);
	bPassed &= TestPathFinderDirectionNearlyEqual(*this, TEXT("Label local Y axis should follow the route strip direction."), TextHorizontalDirection, LabelDirection);
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFinderTrafficLoadSingleVehicleSamplesTest, "PathFinder.TrafficLoad.SingleVehicleSamples", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderTrafficLoadSingleVehicleSamplesTest::RunTest(const FString& Parameters)
{
	FPathFinderTrafficLoadWindow TrafficLoadWindow;

	for (int32 SampleIndex = 0; SampleIndex < 4; ++SampleIndex)
	{
		TrafficLoadWindow.AddVehicleCountSample(1);
	}

	return TestPathFinderNearlyEqual(*this, TEXT("One vehicle for four samples should be 4/120."), TrafficLoadWindow.GetLoad(), 4.0f / 120.0f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFinderTrafficLoadMultipleVehicleSamplesTest, "PathFinder.TrafficLoad.MultipleVehicleSamples", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderTrafficLoadMultipleVehicleSamplesTest::RunTest(const FString& Parameters)
{
	FPathFinderTrafficLoadWindow TrafficLoadWindow;

	for (int32 SampleIndex = 0; SampleIndex < 4; ++SampleIndex)
	{
		TrafficLoadWindow.AddVehicleCountSample(2);
	}

	return TestPathFinderNearlyEqual(*this, TEXT("Two vehicles for four samples should be 8/120."), TrafficLoadWindow.GetLoad(), 8.0f / 120.0f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFinderTrafficLoadSustainedOccupancyTest, "PathFinder.TrafficLoad.SustainedOccupancy", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderTrafficLoadSustainedOccupancyTest::RunTest(const FString& Parameters)
{
	FPathFinderTrafficLoadWindow OneVehicleWindow;
	FPathFinderTrafficLoadWindow TwoVehicleWindow;

	for (int32 SampleIndex = 0; SampleIndex < FPathFinderTrafficLoadWindow::WindowSampleCount; ++SampleIndex)
	{
		OneVehicleWindow.AddVehicleCountSample(1);
		TwoVehicleWindow.AddVehicleCountSample(2);
	}

	bool bPassed = true;
	bPassed &= TestPathFinderNearlyEqual(*this, TEXT("One sustained vehicle should have a load of 1.0."), OneVehicleWindow.GetLoad(), 1.0f);
	bPassed &= TestPathFinderNearlyEqual(*this, TEXT("Two sustained vehicles should have a load of 2.0."), TwoVehicleWindow.GetLoad(), 2.0f);
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFinderTrafficLoadRollingWindowTest, "PathFinder.TrafficLoad.RollingWindow", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderTrafficLoadRollingWindowTest::RunTest(const FString& Parameters)
{
	FPathFinderTrafficLoadWindow TrafficLoadWindow;

	for (int32 SampleIndex = 0; SampleIndex < FPathFinderTrafficLoadWindow::WindowSampleCount; ++SampleIndex)
	{
		TrafficLoadWindow.AddVehicleCountSample(1);
	}

	for (int32 SampleIndex = 0; SampleIndex < FPathFinderTrafficLoadWindow::WindowSampleCount / 2; ++SampleIndex)
	{
		TrafficLoadWindow.AddVehicleCountSample(0);
	}

	bool bPassed = true;
	bPassed &= TestPathFinderNearlyEqual(*this, TEXT("Half the busy samples should remain after half a quiet window."), TrafficLoadWindow.GetLoad(), 0.5f);

	for (int32 SampleIndex = 0; SampleIndex < FPathFinderTrafficLoadWindow::WindowSampleCount / 2; ++SampleIndex)
	{
		TrafficLoadWindow.AddVehicleCountSample(0);
	}

	bPassed &= TestPathFinderNearlyEqual(*this, TEXT("Busy samples should expire after one quiet window."), TrafficLoadWindow.GetLoad(), 0.0f);
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFinderTrafficLoadNegativeSamplesTest, "PathFinder.TrafficLoad.NegativeSamples", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderTrafficLoadNegativeSamplesTest::RunTest(const FString& Parameters)
{
	FPathFinderTrafficLoadWindow TrafficLoadWindow;

	TrafficLoadWindow.AddVehicleCountSample(-3);

	return TestPathFinderNearlyEqual(*this, TEXT("Negative vehicle samples should clamp to zero."), TrafficLoadWindow.GetLoad(), 0.0f);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFinderTrafficLoadTextTest, "PathFinder.TrafficLoad.Text", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderTrafficLoadTextTest::RunTest(const FString& Parameters)
{
	FPathFinderTrafficLoadWindow SparseOneVehicleWindow;
	FPathFinderTrafficLoadWindow SparseTwoVehicleWindow;
	FPathFinderTrafficLoadWindow SustainedOneVehicleWindow;
	FPathFinderTrafficLoadWindow SustainedTwoVehicleWindow;

	for (int32 SampleIndex = 0; SampleIndex < 4; ++SampleIndex)
	{
		SparseOneVehicleWindow.AddVehicleCountSample(1);
		SparseTwoVehicleWindow.AddVehicleCountSample(2);
	}

	for (int32 SampleIndex = 0; SampleIndex < FPathFinderTrafficLoadWindow::WindowSampleCount; ++SampleIndex)
	{
		SustainedOneVehicleWindow.AddVehicleCountSample(1);
		SustainedTwoVehicleWindow.AddVehicleCountSample(2);
	}

	bool bPassed = true;
	bPassed &= TestEqual(TEXT("One vehicle for four samples should show 0.0 vehicles per second."), SparseOneVehicleWindow.GetLoadText(), FString(TEXT("0.0/s")));
	bPassed &= TestEqual(TEXT("Two vehicles for four samples should show 0.1 vehicles per second."), SparseTwoVehicleWindow.GetLoadText(), FString(TEXT("0.1/s")));
	bPassed &= TestEqual(TEXT("One sustained vehicle should show 1.0 vehicles per second."), SustainedOneVehicleWindow.GetLoadText(), FString(TEXT("1.0/s")));
	bPassed &= TestEqual(TEXT("Two sustained vehicles should show 2.0 vehicles per second."), SustainedTwoVehicleWindow.GetLoadText(), FString(TEXT("2.0/s")));
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFinderTrafficLoadTextChangeTest, "PathFinder.TrafficLoad.TextChange", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderTrafficLoadTextChangeTest::RunTest(const FString& Parameters)
{
	FPathFinderTrafficLoadWindow TrafficLoadWindow;

	bool bPassed = true;
	for (int32 SampleIndex = 0; SampleIndex < 5; ++SampleIndex)
	{
		bPassed &= TestFalse(TEXT("Small one-vehicle samples should keep visible text at 0.0/s."), TrafficLoadWindow.AddVehicleCountSample(1).bLoadTextChanged);
	}

	bPassed &= TestEqual(TEXT("Text should remain 0.0/s while the one-decimal count has not changed."), TrafficLoadWindow.GetLoadText(), FString(TEXT("0.0/s")));
	bPassed &= TestTrue(TEXT("Sixth one-vehicle sample should cross to visible 0.1/s."), TrafficLoadWindow.AddVehicleCountSample(1).bLoadTextChanged);
	bPassed &= TestEqual(TEXT("Text should update after the one-decimal count changes."), TrafficLoadWindow.GetLoadText(), FString(TEXT("0.1/s")));
	bPassed &= TestFalse(TEXT("Adding a zero sample should keep the visible count unchanged at 0.1/s."), TrafficLoadWindow.AddVehicleCountSample(0).bLoadTextChanged);
	bPassed &= TestEqual(TEXT("Text should not change when the visible count is unchanged."), TrafficLoadWindow.GetLoadText(), FString(TEXT("0.1/s")));
	return bPassed;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPathFinderTrafficLoadBucketChangeTest, "PathFinder.TrafficLoad.BucketChange", EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderTrafficLoadBucketChangeTest::RunTest(const FString& Parameters)
{
	FPathFinderTrafficLoadWindow TrafficLoadWindow;

	bool bPassed = true;
	bPassed &= TestTrue(TEXT("First occupied sample should change the traffic bucket from empty to low load."), TrafficLoadWindow.AddVehicleCountSample(1).bTrafficBucketChanged);
	bPassed &= TestEqual(TEXT("One occupied sample should be in the low-load bucket."), TrafficLoadWindow.GetTrafficBucket(), 1);

	for (int32 SampleIndex = 0; SampleIndex < 28; ++SampleIndex)
	{
		bPassed &= TestFalse(TEXT("Additional low-load samples should keep the same traffic bucket."), TrafficLoadWindow.AddVehicleCountSample(1).bTrafficBucketChanged);
	}

	bPassed &= TestTrue(TEXT("The thirtieth occupied sample should cross into the medium-load bucket."), TrafficLoadWindow.AddVehicleCountSample(1).bTrafficBucketChanged);
	bPassed &= TestEqual(TEXT("Thirty occupied samples should be in the medium-load bucket."), TrafficLoadWindow.GetTrafficBucket(), 2);
	return bPassed;
}

#endif
