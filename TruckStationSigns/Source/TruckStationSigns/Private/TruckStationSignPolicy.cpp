#include "TruckStationSignPolicy.h"

#include "GameFramework/Actor.h"

namespace TruckStationSignPolicyConstants
{
	const FVector StandardSignRelativeLocation(0.0f, -536.0f, 245.0f);
	const FVector FluidSignRelativeLocation(0.0f, -440.0f, 461.0f);
	const FName CurrentGeneratedSignTag(TEXT("TruckStationSigns.Automatic"));
}

FTransform FTruckStationSignPolicy::GetFrontSignRelativeTransform()
{
	return FTransform(
		FRotator::ZeroRotator,
		TruckStationSignPolicyConstants::StandardSignRelativeLocation);
}

FTransform FTruckStationSignPolicy::GetSignRelativeTransform(
	const UClass* stationClass,
	const UClass* standardTruckStationClass,
	const UClass* fluidTruckStationClass)
{
	if (stationClass != nullptr &&
		fluidTruckStationClass != nullptr &&
		stationClass->IsChildOf(fluidTruckStationClass))
	{
		return FTransform(
			FRotator::ZeroRotator,
			TruckStationSignPolicyConstants::FluidSignRelativeLocation);
	}
	if (stationClass != nullptr &&
		standardTruckStationClass != nullptr &&
		stationClass->IsChildOf(standardTruckStationClass))
	{
		return FTransform(
			FRotator::ZeroRotator,
			TruckStationSignPolicyConstants::StandardSignRelativeLocation);
	}

	return GetFrontSignRelativeTransform();
}

FName FTruckStationSignPolicy::GetCurrentGeneratedSignTag()
{
	return TruckStationSignPolicyConstants::CurrentGeneratedSignTag;
}

bool FTruckStationSignPolicy::IsCurrentGeneratedSign(const AActor* actor)
{
	return IsValid(actor) && actor->Tags.Contains(GetCurrentGeneratedSignTag());
}

bool FTruckStationSignPolicy::ShouldSaveGeneratedSign()
{
	return false;
}

bool FTruckStationSignPolicy::ShouldDeleteGeneratedSign(EEndPlayReason::Type reason)
{
	return reason == EEndPlayReason::Destroyed;
}

bool FTruckStationSignPolicy::IsSupportedStationClass(
	const UClass* candidateClass,
	const UClass* standardTruckStationClass,
	const UClass* fluidTruckStationClass)
{
	if (candidateClass == nullptr ||
		standardTruckStationClass == nullptr ||
		fluidTruckStationClass == nullptr)
	{
		return false;
	}

	return candidateClass->IsChildOf(standardTruckStationClass) ||
		candidateClass->IsChildOf(fluidTruckStationClass);
}

FPrefabSignData FTruckStationSignPolicy::CreateSignData(
	const FText& stationName,
	TSubclassOf<UFGSignPrefabWidget> prefabLayoutClass,
	TSubclassOf<UFGSignTypeDescriptor> signTypeDescriptorClass)
{
	FPrefabSignData signData;
	signData.PrefabLayout = prefabLayoutClass;
	signData.SignTypeDesc = signTypeDescriptorClass;
	signData.TextElementData.Add(TEXT("Name"), stationName.ToString());
	signData.Emissive = 1.0f;
	signData.Glossiness = 0.0f;

	const UFGSignTypeDescriptor* descriptor = signTypeDescriptorClass.GetDefaultObject();
	if (descriptor != nullptr)
	{
		signData.ForegroundColor = descriptor->GetDefaultForegroundColor();
		signData.BackgroundColor = descriptor->GetDefaultBackgroundColor();
		signData.AuxiliaryColor = descriptor->GetDefaultAuxiliaryColor();
	}

	return signData;
}
