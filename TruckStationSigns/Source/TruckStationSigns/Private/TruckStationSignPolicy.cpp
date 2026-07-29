#include "TruckStationSignPolicy.h"

#include "GameFramework/Actor.h"

namespace TruckStationSignPolicyConstants
{
	constexpr float FrontSignLocalYCentimeters = -500.0f;
	constexpr float FrontSignHeightCentimeters = 286.0f;
	const FName CurrentGeneratedSignTag(TEXT("TruckStationSigns.Automatic.V4"));
	const FName PreviousGeneratedSignTag(TEXT("TruckStationSigns.Automatic.V3"));
	const FName OlderGeneratedSignTag(TEXT("TruckStationSigns.Automatic.V2"));
	const FName LegacyGeneratedSignTag(TEXT("TruckStationSigns.Automatic"));
}

FTransform FTruckStationSignPolicy::GetFrontSignRelativeTransform()
{
	return FTransform(
		FRotator::ZeroRotator,
		FVector(
			0.0f,
			TruckStationSignPolicyConstants::FrontSignLocalYCentimeters,
			TruckStationSignPolicyConstants::FrontSignHeightCentimeters));
}

FName FTruckStationSignPolicy::GetCurrentGeneratedSignTag()
{
	return TruckStationSignPolicyConstants::CurrentGeneratedSignTag;
}

FName FTruckStationSignPolicy::GetLegacyGeneratedSignTag()
{
	return TruckStationSignPolicyConstants::LegacyGeneratedSignTag;
}

bool FTruckStationSignPolicy::IsCurrentGeneratedSign(const AActor* actor)
{
	return IsValid(actor) && actor->Tags.Contains(GetCurrentGeneratedSignTag());
}

bool FTruckStationSignPolicy::IsGeneratedSign(const AActor* actor)
{
	return IsValid(actor) &&
		(IsCurrentGeneratedSign(actor) ||
			actor->Tags.Contains(TruckStationSignPolicyConstants::PreviousGeneratedSignTag) ||
			actor->Tags.Contains(TruckStationSignPolicyConstants::OlderGeneratedSignTag) ||
			actor->Tags.Contains(GetLegacyGeneratedSignTag()));
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
