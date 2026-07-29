#include "ConveyorSignsTestSignPolicy.h"

namespace ConveyorSignsTestSignPolicyConstants
{
	const FName SourceBaseName(TEXT("ConveyorSignsAngleTestSource"));
}

FName FConveyorSignsTestSignPolicy::GetSourceBaseName()
{
	return ConveyorSignsTestSignPolicyConstants::SourceBaseName;
}

bool FConveyorSignsTestSignPolicy::IsSourceName(const FName& actorName)
{
	return actorName.ToString().StartsWith(GetSourceBaseName().ToString());
}
