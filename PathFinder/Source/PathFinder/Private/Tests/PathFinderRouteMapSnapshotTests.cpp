#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "PathFinderRouteMapSnapshot.h"

namespace
{
	TArray<FVector> MakeWorldPoints(std::initializer_list<FVector> WorldPoints)
	{
		TArray<FVector> Result;
		for (const FVector& WorldPoint : WorldPoints)
		{
			Result.Add(WorldPoint);
		}
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPathFinderRouteMapSnapshotOnlyChangesForDifferentGeometryTest,
	"PathFinder.RouteMap.SnapshotOnlyChangesForDifferentGeometry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderRouteMapSnapshotOnlyChangesForDifferentGeometryTest::RunTest(const FString& Parameters)
{
	FPathFinderRouteMapSnapshot Snapshot;
	TMap<FString, TArray<FVector>> WorldPointsBySegmentKey;
	WorldPointsBySegmentKey.Add(TEXT("A->B"), MakeWorldPoints({FVector(100.0, 200.0, 0.0), FVector(300.0, 400.0, 0.0)}));

	TestTrue(TEXT("The first geometry creates a snapshot"), Snapshot.ReplaceAllSegments(WorldPointsBySegmentKey));
	TestFalse(TEXT("Identical geometry does not replace the snapshot"), Snapshot.ReplaceAllSegments(WorldPointsBySegmentKey));

	WorldPointsBySegmentKey[TEXT("A->B")][1] = FVector(301.0, 400.0, 0.0);
	TestTrue(TEXT("A changed route point creates a new snapshot"), Snapshot.ReplaceAllSegments(WorldPointsBySegmentKey));
	TestFalse(TEXT("The changed geometry stabilizes after one snapshot"), Snapshot.ReplaceAllSegments(WorldPointsBySegmentKey));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FPathFinderRouteMapSnapshotTracksAddedAndRemovedSegmentsTest,
	"PathFinder.RouteMap.SnapshotTracksAddedAndRemovedSegments",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPathFinderRouteMapSnapshotTracksAddedAndRemovedSegmentsTest::RunTest(const FString& Parameters)
{
	FPathFinderRouteMapSnapshot Snapshot;
	TestTrue(
		TEXT("Adding the first path segment changes the snapshot"),
		Snapshot.UpdateSegment(TEXT("A->B"), MakeWorldPoints({FVector::ZeroVector, FVector(100.0, 0.0, 0.0)})));
	TestTrue(
		TEXT("Adding an unassigned path segment changes the snapshot"),
		Snapshot.UpdateSegment(TEXT("B->C"), MakeWorldPoints({FVector(100.0, 0.0, 0.0), FVector(200.0, 0.0, 0.0)})));
	TestFalse(
		TEXT("Adding identical path geometry does not change the snapshot"),
		Snapshot.UpdateSegment(TEXT("B->C"), MakeWorldPoints({FVector(100.0, 0.0, 0.0), FVector(200.0, 0.0, 0.0)})));

	TestTrue(TEXT("Removing a path segment changes the snapshot"), Snapshot.RemoveSegment(TEXT("A->B")));
	TestFalse(TEXT("Removing a missing path segment does not change the snapshot"), Snapshot.RemoveSegment(TEXT("A->B")));
	TestEqual(TEXT("Only the remaining route segment is retained"), Snapshot.GetWorldPointsBySegmentKey().Num(), 1);
	return true;
}

#endif
