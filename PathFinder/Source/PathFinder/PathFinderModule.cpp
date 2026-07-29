#include "Modules/ModuleManager.h"
#include "Patching/NativeHookManager.h"
#include "PathFinderConsoleCommands.h"
#include "PathFinderRouteMapSubsystem.h"
#include "PathFinderRouteOverlayRenderer.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "WheeledVehicles/FGVehiclePathSegment.h"
#include "WheeledVehicles/FGVehicleSubsystem.h"
#include "WheeledVehicles/FGWheeledVehicleIdentifier.h"

DEFINE_LOG_CATEGORY_STATIC(LogPathFinderModule, Log, All);

namespace
{
	UPathFinderRouteMapSubsystem* ResolveRouteMapSubsystem(const UObject* WorldContextObject)
	{
		UWorld* World = WorldContextObject != nullptr ? WorldContextObject->GetWorld() : nullptr;
		UGameInstance* GameInstance = World != nullptr ? World->GetGameInstance() : nullptr;
		return GameInstance != nullptr
			? GameInstance->GetSubsystem<UPathFinderRouteMapSubsystem>()
			: nullptr;
	}

	void RequestPathSegmentRefresh(AFGVehiclePathSegment* PathSegment)
	{
		UPathFinderRouteMapSubsystem* RouteMapSubsystem = ResolveRouteMapSubsystem(PathSegment);
		if (RouteMapSubsystem != nullptr)
		{
			RouteMapSubsystem->RequestPathSegmentRefresh(PathSegment);
		}
	}

	void RequestPathSegmentRemoval(AFGVehiclePathSegment* PathSegment)
	{
		UPathFinderRouteMapSubsystem* RouteMapSubsystem = ResolveRouteMapSubsystem(PathSegment);
		if (RouteMapSubsystem != nullptr)
		{
			RouteMapSubsystem->RequestPathSegmentRemoval(PathSegment);
		}
	}

	void RequestRouteUsageRefresh(const UObject* WorldContextObject)
	{
		UPathFinderRouteMapSubsystem* RouteMapSubsystem = ResolveRouteMapSubsystem(WorldContextObject);
		if (RouteMapSubsystem != nullptr)
		{
			RouteMapSubsystem->RequestRouteUsageRefresh();
		}
	}

	void RegisterRouteMapInvalidationHooks()
	{
		SUBSCRIBE_METHOD_AFTER(AFGVehicleSubsystem::AddPathSegment, [](AFGVehicleSubsystem* Self, AFGVehiclePathSegment* PathSegment)
		{
			(void)Self;
			RequestPathSegmentRefresh(PathSegment);
		});
		SUBSCRIBE_METHOD(AFGVehicleSubsystem::RemovePathSegment, [](auto& Scope, AFGVehicleSubsystem* Self, AFGVehiclePathSegment* PathSegment)
		{
			(void)Scope;
			(void)Self;
			RequestPathSegmentRemoval(PathSegment);
		});
		SUBSCRIBE_METHOD_AFTER(AFGVehiclePathSegment::SetSplinePointsAndValidatePath, [](AFGVehiclePathSegment* Self, const TArray<FSplinePointData>& SplinePointData)
		{
			(void)SplinePointData;
			RequestPathSegmentRefresh(Self);
		});
		SUBSCRIBE_METHOD_AFTER(AFGWheeledVehicleIdentifier::SetVehicleRoute, [](AFGWheeledVehicleIdentifier* Self, const TArray<FGuid>& NewVehicleRoute)
		{
			(void)NewVehicleRoute;
			RequestRouteUsageRefresh(Self);
		});
		SUBSCRIBE_METHOD_AFTER(AFGVehicleSubsystem::AddVehicle, [](AFGVehicleSubsystem* Self, AFGWheeledVehicleIdentifier* VehicleIdentifier)
		{
			(void)VehicleIdentifier;
			RequestRouteUsageRefresh(Self);
		});
		SUBSCRIBE_METHOD_AFTER(AFGVehicleSubsystem::RemoveVehicle, [](AFGVehicleSubsystem* Self, AFGWheeledVehicleIdentifier* VehicleIdentifier)
		{
			(void)VehicleIdentifier;
			RequestRouteUsageRefresh(Self);
		});
	}
}

class FPathFinderModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
#if !WITH_EDITOR
		RegisterRouteMapInvalidationHooks();
		FString PathFinderContentPath = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("Mods/PathFinder/Content/")));
		FPaths::MakeStandardFilename(PathFinderContentPath);
		if (!PathFinderContentPath.EndsWith(TEXT("/")))
		{
			PathFinderContentPath += TEXT("/");
		}
		FPackageName::RegisterMountPoint(TEXT("/PathFinder/"), PathFinderContentPath);
		UE_LOG(LogPathFinderModule, Log, TEXT("PathFinder content mount registered: root=/PathFinder/ path=%s"), *PathFinderContentPath);
		UE_LOG(LogPathFinderModule, Log, TEXT("%s"), *FPathFinderRouteOverlayRenderer::DescribeRouteOverlayMaterialProbe());
#endif
		ConsoleCommands = MakeUnique<FPathFinderConsoleCommands>();
		ConsoleCommands->Register();
	}

	virtual void ShutdownModule() override
	{
		if (ConsoleCommands.IsValid())
		{
			ConsoleCommands->Unregister();
			ConsoleCommands.Reset();
		}
	}

private:
	TUniquePtr<FPathFinderConsoleCommands> ConsoleCommands;
};

IMPLEMENT_MODULE(FPathFinderModule, PathFinder)
