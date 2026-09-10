#include "BeltPurgeLog.h"
#include "BeltPurgeRemoteCallObject.h"
#include "BeltPurgeService.h"
#include "Components/InputComponent.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGPlayerController.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "Misc/OutputDevice.h"
#include "Modules/ModuleManager.h"
#include "Patching/NativeHookManager.h"

class FBeltPurgeModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
		PurgeFromAimCommand = IConsoleManager::Get().RegisterConsoleCommand(
			TEXT("BeltPurge.PurgeFromAim"),
			TEXT("Request a belt purge from the local player's aim target."),
			FConsoleCommandWithWorldArgsAndOutputDeviceDelegate::CreateRaw(
				this,
				&FBeltPurgeModule::PurgeFromAim),
			ECVF_Default);

#if !WITH_EDITOR
		AFGCharacterPlayer::OnPlayerInputInitialized.AddRaw(
			this,
			&FBeltPurgeModule::OnPlayerInputInitialized);

		AFGGameMode* gameModeDefault = GetMutableDefault<AFGGameMode>();
		SUBSCRIBE_METHOD_VIRTUAL(
			AFGGameMode::PostLogin,
			gameModeDefault,
			[](auto& scope, AFGGameMode* gameMode, APlayerController* playerController)
			{
				if (gameMode->HasAuthority() && !gameMode->IsMainMenuGameMode())
				{
					gameMode->RegisterRemoteCallObjectClass(
						UBeltPurgeRemoteCallObject::StaticClass());
				}
			});
#endif
	}

	virtual void ShutdownModule() override
	{
		if (PurgeFromAimCommand != nullptr)
		{
			IConsoleManager::Get().UnregisterConsoleObject(PurgeFromAimCommand);
			PurgeFromAimCommand = nullptr;
		}

		AFGCharacterPlayer::OnPlayerInputInitialized.RemoveAll(this);
	}

private:
	void PurgeFromAim(
		const TArray<FString>& arguments,
		UWorld* world,
		FOutputDevice& output)
	{
		if (!arguments.IsEmpty())
		{
			output.Log(TEXT("error: usage BeltPurge.PurgeFromAim"));
			UE_LOG(LogBeltPurge, Error, TEXT("Usage error for BeltPurge.PurgeFromAim console command."));
			return;
		}

		if (world == nullptr)
		{
			output.Log(TEXT("error: purge request was not accepted for the local aim target"));
			UE_LOG(LogBeltPurge, Error, TEXT("PurgeFromAim console command was invoked without a world."));
			return;
		}

		AFGPlayerController* controller =
			Cast<AFGPlayerController>(world->GetFirstPlayerController());
		if (controller == nullptr || !controller->IsLocalController())
		{
			output.Log(TEXT("error: purge request was not accepted for the local aim target"));
			UE_LOG(LogBeltPurge, Error, TEXT("PurgeFromAim console command could not resolve the local player controller."));
			return;
		}

		if (UBeltPurgeService::RequestPurgeFromAim(controller))
		{
			output.Log(TEXT("BeltPurge purge requested from local aim."));
			UE_LOG(LogBeltPurge, Display, TEXT("BeltPurge purge requested from local aim."));
			return;
		}

		output.Log(TEXT("error: purge request was not accepted for the local aim target"));
		UE_LOG(LogBeltPurge, Error, TEXT("Purge request was not accepted for the local aim target."));
	}

	void OnPlayerInputInitialized(
		AFGCharacterPlayer* character,
		UInputComponent* inputComponent)
	{
		if (character == nullptr ||
			inputComponent == nullptr ||
			!character->IsLocallyControlled())
		{
			return;
		}

		const TWeakObjectPtr<AFGCharacterPlayer> weakCharacter(character);
		FInputKeyBinding purgeBinding(
			FInputChord(EKeys::Delete, true, true, false, false),
			IE_Pressed);
		purgeBinding.bConsumeInput = true;
		purgeBinding.KeyDelegate.GetDelegateForManualSet().BindLambda(
			[weakCharacter]()
			{
				AFGCharacterPlayer* localCharacter = weakCharacter.Get();
				if (localCharacter == nullptr)
				{
					return;
				}

				AFGPlayerController* controller =
					Cast<AFGPlayerController>(localCharacter->GetController());
				UBeltPurgeService::RequestPurgeFromAim(controller);
			});
		inputComponent->KeyBindings.Add(MoveTemp(purgeBinding));

		UE_LOG(
			LogBeltPurge,
			Display,
			TEXT("Bound Ctrl+Shift+Delete destructive belt purge for local player."));
	}

	IConsoleObject* PurgeFromAimCommand = nullptr;
};

DEFINE_LOG_CATEGORY(LogBeltPurge);

IMPLEMENT_GAME_MODULE(FBeltPurgeModule, BeltPurge);
