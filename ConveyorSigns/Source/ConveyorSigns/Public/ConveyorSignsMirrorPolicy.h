#pragma once

#include "CoreMinimal.h"
#include "ConveyorSignsEndpoints.h"

class AActor;

class CONVEYORSIGNS_API FConveyorSignsMirrorPolicy
{
public:
	static constexpr double MaxSourceDistance = 25.0;

	static FName GetMirrorTag();
	static bool IsMirror(const AActor* actor);
	static bool ShouldSaveMirror();
	static bool ShouldAllowMirrorUse();
	static bool ShouldKeepMirrorCollision();

	static bool TryGetMirrorTransform(
		const FTransform& sourceTransform,
		const FTransform& liftTransform,
		const FConveyorSignsEndpointTransforms& endpoints,
		FTransform& mirrorTransform,
		FTransform* canonicalSourceTransform = nullptr);
	static bool ShouldDeletePartner(EEndPlayReason::Type reason);
	static bool ShouldDeleteSignsWithLift(EEndPlayReason::Type reason);
};
