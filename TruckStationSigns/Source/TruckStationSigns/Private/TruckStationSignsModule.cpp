#include "Modules/ModuleManager.h"

#include "TruckStationSignController.h"
#include "TruckStationSignConsoleCommands.h"
#include "TruckStationSignPolicy.h"
#include "TruckStationSignsLog.h"

#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableDockingStation.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "Engine/World.h"
#include "FGSaveInterface.h"
#include "Patching/NativeHookManager.h"
#include "UObject/UObjectGlobals.h"
#include "WheeledVehicles/FGDockingStationIdentifier.h"

DEFINE_LOG_CATEGORY(LogTruckStationSigns);

class FTruckStationSignsModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if !WITH_EDITOR
		RegisterHooks();
		postLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(
			this,
			&FTruckStationSignsModule::OnPostLoadMap);
#endif
	}

	virtual void ShutdownModule() override
	{
		if (consoleCommands.IsValid())
		{
			consoleCommands->Unregister();
			consoleCommands.Reset();
		}

		if (postLoadMapHandle.IsValid())
		{
			FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(postLoadMapHandle);
			postLoadMapHandle.Reset();
		}

		for (const TPair<TWeakObjectPtr<UWorld>, FDelegateHandle>& worldHandle : worldBeginPlayHandles)
		{
			UWorld* world = worldHandle.Key.Get();
			if (world != nullptr)
			{
				world->OnWorldBeginPlay.Remove(worldHandle.Value);
			}
		}
		worldBeginPlayHandles.Empty();

		if (controller.IsValid())
		{
			controller->Reset();
			controller.Reset();
		}
	}

private:
	void OnPostLoadMap(UWorld* world)
	{
		if (world == nullptr ||
			!world->IsGameWorld() ||
			!world->GetMapName().Contains(TEXT("Persistent_Level")))
		{
			return;
		}

		if (!controller.IsValid())
		{
			controller = MakeUnique<FTruckStationSignController>();
			if (!controller->Initialize())
			{
				UE_LOG(
					LogTruckStationSigns,
					Error,
					TEXT("Failed to load required vanilla Truck Station sign assets; automatic signs are disabled."));
				controller.Reset();
				return;
			}

			consoleCommands = MakeUnique<FTruckStationSignConsoleCommands>(controller.Get());
			consoleCommands->Register();
		}

		if (world->HasBegunPlay())
		{
			OnWorldBeginPlay(world);
			return;
		}

		const TWeakObjectPtr<UWorld> worldKey(world);
		if (worldBeginPlayHandles.Contains(worldKey))
		{
			return;
		}

		const FDelegateHandle worldBeginPlayHandle = world->OnWorldBeginPlay.AddRaw(
			this,
			&FTruckStationSignsModule::OnWorldBeginPlay,
			world);
		worldBeginPlayHandles.Add(worldKey, worldBeginPlayHandle);
	}

	void OnWorldBeginPlay(UWorld* world)
	{
		if (world == nullptr)
		{
			return;
		}

		const TWeakObjectPtr<UWorld> worldKey(world);
		const FDelegateHandle* worldBeginPlayHandle = worldBeginPlayHandles.Find(worldKey);
		if (worldBeginPlayHandle != nullptr)
		{
			world->OnWorldBeginPlay.Remove(*worldBeginPlayHandle);
			worldBeginPlayHandles.Remove(worldKey);
		}

		if (!controller.IsValid())
		{
			return;
		}

		const int32 activeSignCount = controller->RefreshWorld(world);
		UE_LOG(
			LogTruckStationSigns,
			Display,
			TEXT("Initialized %d transient truck station signs after world BeginPlay completed."),
			activeSignCount);
	}

	void RegisterHooks()
	{
		using FShouldSaveHookInvoker = HookInvoker<
			decltype(&IFGSaveInterface::Execute_ShouldSave),
			&IFGSaveInterface::Execute_ShouldSave>;
		SUBSCRIBE_METHOD(
			IFGSaveInterface::Execute_ShouldSave,
			[](
				FShouldSaveHookInvoker::ScopeType& scope,
				const UObject* object)
			{
				const AFGBuildableWidgetSign* sign = Cast<AFGBuildableWidgetSign>(object);
				if (FTruckStationSignPolicy::IsGeneratedSign(sign))
				{
					scope.Override(FTruckStationSignPolicy::ShouldSaveGeneratedSign());
				}
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(
			AFGBuildableDockingStation,
			BeginPlay,
			[this](AFGBuildableDockingStation* station)
			{
				const TWeakObjectPtr<UWorld> worldKey(station != nullptr ? station->GetWorld() : nullptr);
				if (controller.IsValid() && !worldBeginPlayHandles.Contains(worldKey))
				{
					controller->EnsureSign(station);
				}
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(
			AFGDockingStationIdentifier,
			Internal_SetStationName,
			[this](AFGDockingStationIdentifier* identifier, const FText&)
			{
				if (controller.IsValid())
				{
					controller->OnStationNameChanged(identifier);
				}
			});

		using FEndPlayHookInvoker = HookInvoker<
			decltype(&AFGBuildable::EndPlay),
			&AFGBuildable::EndPlay>;
		SUBSCRIBE_UOBJECT_METHOD(
			AFGBuildable,
			EndPlay,
			[this](
				FEndPlayHookInvoker::ScopeType&,
				AFGBuildable* buildable,
				EEndPlayReason::Type reason)
			{
				AFGBuildableDockingStation* station = Cast<AFGBuildableDockingStation>(buildable);
				if (station != nullptr && controller.IsValid())
				{
					controller->OnStationEndPlay(station, reason);
				}
			});
	}

	TUniquePtr<FTruckStationSignController> controller;
	TUniquePtr<FTruckStationSignConsoleCommands> consoleCommands;
	TMap<TWeakObjectPtr<UWorld>, FDelegateHandle> worldBeginPlayHandles;
	FDelegateHandle postLoadMapHandle;
};

IMPLEMENT_GAME_MODULE(FTruckStationSignsModule, TruckStationSigns)
