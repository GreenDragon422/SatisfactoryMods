#include "PathFinderControlToolDescriptor.h"

#include "Engine/Texture2D.h"
#include "PathFinderControlTool.h"
#include "UObject/ConstructorHelpers.h"

UPathFinderControlToolDescriptor::UPathFinderControlToolDescriptor()
{
	mDisplayName = NSLOCTEXT("PathFinder", "ControlToolName", "PathFinder Route Patrol");
	mDescription = NSLOCTEXT("PathFinder", "ControlToolDescription", "A rugged field instrument for surveying PathFinder vehicle routes and traffic load.");
	mStackSize = EStackSize::SS_ONE;
	mCanBeDiscarded = true;
	mRememberPickUp = false;
	mForm = EResourceForm::RF_SOLID;
	mEquipmentClass = APathFinderControlTool::StaticClass();

	static ConstructorHelpers::FObjectFinder<UTexture2D> RoutePatrolIcon(
		TEXT("/PathFinder/RoutePatrol/T_RoutePatrolTool_Icon.T_RoutePatrolTool_Icon"));
	if (RoutePatrolIcon.Succeeded())
	{
		mSmallIcon = RoutePatrolIcon.Object;
		mPersistentBigIcon = RoutePatrolIcon.Object;
	}
}
