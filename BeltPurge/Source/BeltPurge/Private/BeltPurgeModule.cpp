#include "BeltPurgeLog.h"
#include "BeltPurgeRemoteCallObject.h"
#include "BeltPurgeService.h"
#include "Components/InputComponent.h"
#include "FGCharacterPlayer.h"
#include "FGGameMode.h"
#include "FGPlayerController.h"
#include "InputCoreTypes.h"
#include "Modules/ModuleManager.h"
#include "Patching/NativeHookManager.h"

class FBeltPurgeModule final : public FDefaultGameModuleImpl
{
public:
	virtual void StartupModule() override
	{
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
		AFGCharacterPlayer::OnPlayerInputInitialized.RemoveAll(this);
	}

private:
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
};

DEFINE_LOG_CATEGORY(LogBeltPurge);

IMPLEMENT_GAME_MODULE(FBeltPurgeModule, BeltPurge);
