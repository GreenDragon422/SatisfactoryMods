#if WITH_DEV_AUTOMATION_TESTS

#include "TruckStationSignOwnershipRegistry.h"
#include "TruckStationSignPolicy.h"

#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "Components/SceneComponent.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace TruckStationSignPolicyTests
{
	const TCHAR* StandardStationClassPath =
		TEXT("/Game/FactoryGame/Buildable/Factory/TruckStation/Build_TruckStation.Build_TruckStation_C");
	const TCHAR* FluidStationClassPath =
		TEXT("/Game/FactoryGame/Buildable/Factory/TruckStation/Build_FluidTruckStation.Build_FluidTruckStation_C");
	const TCHAR* SignClassPath =
		TEXT("/Game/FactoryGame/Buildable/Factory/SignDigital/Build_StandaloneWidgetSign_SmallVeryWide.Build_StandaloneWidgetSign_SmallVeryWide_C");
	const TCHAR* LayoutClassPath =
		TEXT("/Game/FactoryGame/Interface/UI/InGame/Signs/SignLayouts/BPW_Sign4x1_1.BPW_Sign4x1_1_C");
	const TCHAR* DescriptorClassPath =
		TEXT("/Game/FactoryGame/Buildable/Factory/-Shared/SignTypes/SignTypeDesc_8x1.SignTypeDesc_8x1_C");
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTruckStationSignsGeneratedLifecycleTest,
	"TruckStationSigns.Lifecycle.GeneratedSignsAreTransientAndDeleteWithStation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTruckStationSignsGeneratedLifecycleTest::RunTest(const FString& parameters)
{
	TStrongObjectPtr<AActor> currentSign(NewObject<AActor>());
	currentSign->Tags.Add(FTruckStationSignPolicy::GetCurrentGeneratedSignTag());
	TStrongObjectPtr<AActor> versionThreeSign(NewObject<AActor>());
	versionThreeSign->Tags.Add(TEXT("TruckStationSigns.Automatic.V3"));
	TStrongObjectPtr<AActor> previousSign(NewObject<AActor>());
	previousSign->Tags.Add(TEXT("TruckStationSigns.Automatic.V2"));
	TStrongObjectPtr<AActor> originalSign(NewObject<AActor>());
	originalSign->Tags.Add(FTruckStationSignPolicy::GetLegacyGeneratedSignTag());
	TStrongObjectPtr<AActor> playerSign(NewObject<AActor>());

	TestEqual(
		TEXT("The measured front-placement generation is version four"),
		FTruckStationSignPolicy::GetCurrentGeneratedSignTag(),
		FName(TEXT("TruckStationSigns.Automatic.V4")));
	TestTrue(TEXT("The current generated tag is recognized"), FTruckStationSignPolicy::IsCurrentGeneratedSign(currentSign.Get()));
	TestTrue(TEXT("The current generated sign is recognized"), FTruckStationSignPolicy::IsGeneratedSign(currentSign.Get()));
	TestTrue(TEXT("Version three is recognized for migration cleanup"), FTruckStationSignPolicy::IsGeneratedSign(versionThreeSign.Get()));
	TestTrue(TEXT("The previous generated sign is recognized for migration cleanup"), FTruckStationSignPolicy::IsGeneratedSign(previousSign.Get()));
	TestTrue(TEXT("The original generated sign is recognized for migration cleanup"), FTruckStationSignPolicy::IsGeneratedSign(originalSign.Get()));
	TestFalse(TEXT("A normal player sign is not generated"), FTruckStationSignPolicy::IsGeneratedSign(playerSign.Get()));
	TestFalse(TEXT("Generated signs are excluded from saves"), FTruckStationSignPolicy::ShouldSaveGeneratedSign());
	TestTrue(
		TEXT("Dismantling the station deletes its generated sign"),
		FTruckStationSignPolicy::ShouldDeleteGeneratedSign(EEndPlayReason::Destroyed));
	TestFalse(
		TEXT("World travel does not trigger a gameplay deletion cascade"),
		FTruckStationSignPolicy::ShouldDeleteGeneratedSign(EEndPlayReason::LevelTransition));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTruckStationSignsSupportsVanillaVariantsTest,
	"TruckStationSigns.Filtering.SupportsStandardAndFluidTruckStationsOnly",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTruckStationSignsSupportsVanillaVariantsTest::RunTest(const FString& parameters)
{
	UClass* standardClass = LoadClass<AFGBuildableDockingStation>(
		nullptr,
		TruckStationSignPolicyTests::StandardStationClassPath);
	UClass* fluidClass = LoadClass<AFGBuildableDockingStation>(
		nullptr,
		TruckStationSignPolicyTests::FluidStationClassPath);

	TestNotNull(TEXT("The standard Truck Station class loads"), standardClass);
	TestNotNull(TEXT("The Fluid Truck Station class loads"), fluidClass);
	if (standardClass == nullptr || fluidClass == nullptr)
	{
		return false;
	}

	TestTrue(
		TEXT("The standard Truck Station is supported"),
		FTruckStationSignPolicy::IsSupportedStationClass(standardClass, standardClass, fluidClass));
	TestTrue(
		TEXT("The Fluid Truck Station is supported"),
		FTruckStationSignPolicy::IsSupportedStationClass(fluidClass, standardClass, fluidClass));
	TestFalse(
		TEXT("The abstract docking station base is not treated as a supported Truck Station"),
		FTruckStationSignPolicy::IsSupportedStationClass(
			AFGBuildableDockingStation::StaticClass(),
			standardClass,
			fluidClass));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTruckStationSignsUsesVanillaAssetsTest,
	"TruckStationSigns.Assets.UsesVanillaEightByOneNameLayout",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTruckStationSignsUsesVanillaAssetsTest::RunTest(const FString& parameters)
{
	UClass* signClass = LoadClass<AFGBuildableWidgetSign>(
		nullptr,
		TruckStationSignPolicyTests::SignClassPath);
	UClass* layoutClass = LoadClass<UFGSignPrefabWidget>(
		nullptr,
		TruckStationSignPolicyTests::LayoutClassPath);
	UClass* descriptorClass = LoadClass<UFGSignTypeDescriptor>(
		nullptr,
		TruckStationSignPolicyTests::DescriptorClassPath);

	TestNotNull(TEXT("The vanilla 8x1 sign class loads"), signClass);
	TestNotNull(TEXT("The vanilla one-name layout loads"), layoutClass);
	TestNotNull(TEXT("The vanilla 8x1 descriptor loads"), descriptorClass);
	if (layoutClass == nullptr || descriptorClass == nullptr)
	{
		return false;
	}

	const UFGSignTypeDescriptor* descriptor =
		Cast<UFGSignTypeDescriptor>(descriptorClass->GetDefaultObject());
	TestNotNull(TEXT("The descriptor has a default object"), descriptor);
	if (descriptor == nullptr)
	{
		return false;
	}

	const TArray<TSoftClassPtr<UFGSignPrefabWidget>> prefabLayouts = descriptor->GetPrefabArray();
	const bool layoutIsCompatible = prefabLayouts.ContainsByPredicate(
		[layoutClass](const TSoftClassPtr<UFGSignPrefabWidget>& candidate)
		{
			return candidate.ToSoftObjectPath() == FSoftObjectPath(layoutClass);
		});
	TestTrue(TEXT("The selected layout belongs to the vanilla 8x1 descriptor"), layoutIsCompatible);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTruckStationSignsPreservesAcceptedNameTest,
	"TruckStationSigns.Naming.PreservesUnicodeAndEmptyAcceptedNames",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTruckStationSignsPreservesAcceptedNameTest::RunTest(const FString& parameters)
{
	const FText unicodeName = FText::FromString(TEXT("תחנת ברזל 🚚"));
	const FPrefabSignData unicodeData = FTruckStationSignPolicy::CreateSignData(
		unicodeName,
		TSubclassOf<UFGSignPrefabWidget>(),
		TSubclassOf<UFGSignTypeDescriptor>());
	const FString* unicodeValue = unicodeData.TextElementData.Find(TEXT("Name"));

	TestNotNull(TEXT("The Name element is populated"), unicodeValue);
	if (unicodeValue != nullptr)
	{
		TestEqual(TEXT("The accepted Unicode name is unchanged"), *unicodeValue, unicodeName.ToString());
	}

	const FPrefabSignData emptyData = FTruckStationSignPolicy::CreateSignData(
		FText::GetEmpty(),
		TSubclassOf<UFGSignPrefabWidget>(),
		TSubclassOf<UFGSignTypeDescriptor>());
	const FString* emptyValue = emptyData.TextElementData.Find(TEXT("Name"));
	TestNotNull(TEXT("An empty station still has a Name element"), emptyValue);
	if (emptyValue != nullptr)
	{
		TestTrue(TEXT("An empty station name remains empty"), emptyValue->IsEmpty());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FTruckStationSignsRegistryIsIdempotentTest,
	"TruckStationSigns.Ownership.IsIdempotentAndCleansUp",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FTruckStationSignsRegistryIsIdempotentTest::RunTest(const FString& parameters)
{
	TStrongObjectPtr<UObject> station(NewObject<USceneComponent>());
	TStrongObjectPtr<UObject> firstSign(NewObject<USceneComponent>());
	TStrongObjectPtr<UObject> duplicateSign(NewObject<USceneComponent>());
	FTruckStationSignOwnershipRegistry registry;

	TestTrue(TEXT("The first sign is accepted"), registry.TryAdd(station.Get(), firstSign.Get()));
	TestFalse(TEXT("A duplicate sign is rejected"), registry.TryAdd(station.Get(), duplicateSign.Get()));
	TestEqual(TEXT("The first sign remains owned"), registry.Find(station.Get()), firstSign.Get());
	TestEqual(TEXT("Only one ownership entry exists"), registry.Num(), 1);
	TestEqual(TEXT("Cleanup returns the owned sign"), registry.Remove(station.Get()), firstSign.Get());
	TestNull(TEXT("Cleanup removes the ownership entry"), registry.Find(station.Get()));
	TestEqual(TEXT("No ownership entry remains"), registry.Num(), 0);

	return true;
}

#endif
