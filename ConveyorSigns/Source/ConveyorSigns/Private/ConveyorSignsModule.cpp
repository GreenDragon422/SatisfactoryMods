#include "Modules/ModuleManager.h"

#include "ConveyorSignsAttachmentPointSet.h"
#include "ConveyorSignsConsoleCommands.h"
#include "ConveyorSignsLog.h"
#include "ConveyorSignsMirrorController.h"
#include "ConveyorSignsMirrorPolicy.h"
#include "Buildables/FGBuildable.h"
#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "FGSaveInterface.h"
#include "Patching/NativeHookManager.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY(LogConveyorSigns);

class FConveyorSignsModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
#if !WITH_EDITOR
		UClass* loadedAttachmentType = LoadClass<UFGAttachmentPointType>(
			nullptr,
			TEXT("/Game/FactoryGame/Buildable/-Shared/AttachmentPointTypes/Sign/APT_SignCenter.APT_SignCenter_C"));
		if (loadedAttachmentType == nullptr)
		{
			UE_LOG(
				LogConveyorSigns,
				Error,
				TEXT("Failed to load vanilla center sign attachment type; Conveyor Lift sign positions are disabled."));
			return;
		}

		signCenterAttachmentType.Reset(loadedAttachmentType);
		RegisterHooks();
		mirrorController = MakeUnique<FConveyorSignsMirrorController>();
		consoleCommands = MakeUnique<FConveyorSignsConsoleCommands>();
		consoleCommands->Register();
		postLoadMapHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddRaw(
			this,
			&FConveyorSignsModule::OnPostLoadMap);
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

		if (tickerHandle.IsValid())
		{
			FTSTicker::RemoveTicker(tickerHandle);
			tickerHandle.Reset();
		}

		if (mirrorController.IsValid())
		{
			mirrorController->Reset();
			mirrorController.Reset();
		}

		attachmentPointSets.Empty();
		signCenterAttachmentType.Reset();
	}

private:
	void OnPostLoadMap(UWorld* world)
	{
		if (world == nullptr || !world->IsGameWorld())
		{
			return;
		}

		if (!tickerHandle.IsValid())
		{
			tickerHandle = FTSTicker::GetCoreTicker().AddTicker(
				TEXT("ConveyorSigns.InitializeMirrors"),
				0.1f,
				[this](float)
				{
					return RunInitialRefresh();
				});
		}
	}

	bool RunInitialRefresh()
	{
		if (!mirrorController.IsValid())
		{
			return false;
		}

		if (GEngine == nullptr)
		{
			return true;
		}

		bool refreshedWorld = false;
		for (const FWorldContext& worldContext : GEngine->GetWorldContexts())
		{
			UWorld* world = worldContext.World();
			if (world != nullptr && world->IsGameWorld() && world->HasBegunPlay())
			{
				mirrorController->RefreshWorld(world);
				refreshedWorld = true;
			}
		}

		if (refreshedWorld)
		{
			tickerHandle.Reset();
			return false;
		}

		return true;
	}

	void RegisterHooks()
	{
		SUBSCRIBE_UOBJECT_METHOD_AFTER(
			AFGBuildableWidgetSign,
			BeginPlay,
			[this](AFGBuildableWidgetSign* sign)
			{
				if (mirrorController.IsValid())
				{
					mirrorController->EnsureMirrorForSource(sign);
				}
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(
			AFGBuildableWidgetSign,
			SetPrefabSignData,
			[this](AFGBuildableWidgetSign* sign, FPrefabSignData&, bool)
			{
				if (mirrorController.IsValid())
				{
					mirrorController->SyncSource(sign);
				}
			});

		SUBSCRIBE_UOBJECT_METHOD_AFTER(
			AFGBuildable,
			GetAttachmentPoints,
			[this](const AFGBuildable* buildable, TArray<const FFGAttachmentPoint*>& outputPoints)
			{
				const AFGBuildableConveyorLift* constConveyorLift =
					Cast<AFGBuildableConveyorLift>(buildable);
				if (!IsValid(constConveyorLift))
				{
					return;
				}

				AFGBuildableConveyorLift* conveyorLift =
					const_cast<AFGBuildableConveyorLift*>(constConveyorLift);
				const TWeakObjectPtr<AFGBuildableConveyorLift> liftKey(conveyorLift);
				TUniquePtr<FConveyorSignsAttachmentPointSet>& attachmentPointSet =
					attachmentPointSets.FindOrAdd(liftKey);
				if (!attachmentPointSet.IsValid())
				{
					attachmentPointSet = MakeUnique<FConveyorSignsAttachmentPointSet>();
				}

				const TSubclassOf<UFGAttachmentPointType> attachmentType(
					signCenterAttachmentType.Get());
				attachmentPointSet->Update(conveyorLift, attachmentType);
				attachmentPointSet->AppendTo(outputPoints);
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
				AFGBuildableWidgetSign* sign = Cast<AFGBuildableWidgetSign>(buildable);
				if (sign != nullptr && mirrorController.IsValid())
				{
					mirrorController->OnSignEndPlay(sign, reason);
				}

				AFGBuildableConveyorLift* conveyorLift = Cast<AFGBuildableConveyorLift>(buildable);
				if (conveyorLift != nullptr)
				{
					if (mirrorController.IsValid())
					{
						mirrorController->OnLiftEndPlay(conveyorLift, reason);
					}
					attachmentPointSets.Remove(TWeakObjectPtr<AFGBuildableConveyorLift>(conveyorLift));
				}
			});

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
				if (FConveyorSignsMirrorPolicy::IsMirror(sign))
				{
					scope.Override(FConveyorSignsMirrorPolicy::ShouldSaveMirror());
				}
			});

		using FIsUseableHookInvoker = HookInvoker<
			decltype(&AFGBuildable::IsUseable_Implementation),
			&AFGBuildable::IsUseable_Implementation>;
		SUBSCRIBE_UOBJECT_METHOD(
			AFGBuildable,
			IsUseable_Implementation,
			[](
				FIsUseableHookInvoker::ScopeType& scope,
				const AFGBuildable* buildable)
			{
				const AFGBuildableWidgetSign* sign = Cast<AFGBuildableWidgetSign>(buildable);
				if (FConveyorSignsMirrorPolicy::IsMirror(sign))
				{
					scope.Override(FConveyorSignsMirrorPolicy::ShouldAllowMirrorUse());
				}
			});

		using FOnUseHookInvoker = HookInvoker<
			decltype(&AFGBuildable::OnUse_Implementation),
			&AFGBuildable::OnUse_Implementation>;
		SUBSCRIBE_UOBJECT_METHOD(
			AFGBuildable,
			OnUse_Implementation,
			[](
				FOnUseHookInvoker::ScopeType& scope,
				AFGBuildable* buildable,
				AFGCharacterPlayer*,
				const FUseState&)
			{
				AFGBuildableWidgetSign* sign = Cast<AFGBuildableWidgetSign>(buildable);
				if (FConveyorSignsMirrorPolicy::IsMirror(sign))
				{
					scope.Cancel();
				}
			});
	}

	TStrongObjectPtr<UClass> signCenterAttachmentType;
	TUniquePtr<FConveyorSignsMirrorController> mirrorController;
	TUniquePtr<FConveyorSignsConsoleCommands> consoleCommands;
	FTSTicker::FDelegateHandle tickerHandle;
	FDelegateHandle postLoadMapHandle;
	TMap<
		TWeakObjectPtr<AFGBuildableConveyorLift>,
		TUniquePtr<FConveyorSignsAttachmentPointSet>> attachmentPointSets;
};

IMPLEMENT_GAME_MODULE(FConveyorSignsModule, ConveyorSigns);
