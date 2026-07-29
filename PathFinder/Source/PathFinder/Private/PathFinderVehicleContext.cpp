#include "PathFinderVehicleContext.h"

#include "FGCharacterPlayer.h"
#include "GameFramework/PlayerController.h"
#include "WheeledVehicles/FGWheeledVehicle.h"
#include "WheeledVehicles/FGWheeledVehicleIdentifier.h"
#include "WheeledVehicles/FGVehiclePathPreset.h"

bool FPathFinderVehicleContext::IsValid() const
{
	return World != nullptr && PlayerController != nullptr && WheeledVehicle != nullptr && VehicleIdentifier != nullptr;
}

bool FPathFinderVehicleContext::IsRouteTraceValid() const
{
	return World != nullptr && VehicleIdentifier != nullptr && VehiclePathPreset != nullptr;
}

FString FPathFinderVehicleContext::ToDebugString() const
{
	TArray<FString> Parts;
	Parts.Add(FString::Printf(TEXT("world=%s"), World != nullptr ? TEXT("yes") : TEXT("no")));
	Parts.Add(FString::Printf(TEXT("controller=%s"), PlayerController != nullptr ? *PlayerController->GetName() : TEXT("<none>")));
	Parts.Add(FString::Printf(TEXT("pawn=%s"), Pawn != nullptr ? *Pawn->GetName() : TEXT("<none>")));
	Parts.Add(FString::Printf(TEXT("vehicle=%s"), WheeledVehicle != nullptr ? *WheeledVehicle->GetName() : TEXT("<none>")));
	Parts.Add(FString::Printf(TEXT("identifier=%s"), VehicleIdentifier != nullptr ? *VehicleIdentifier->GetName() : TEXT("<none>")));
	Parts.Add(FString::Printf(TEXT("preset=%s"), VehiclePathPreset != nullptr ? *VehiclePathPreset->GetName() : TEXT("<none>")));

	if (!FailureReason.IsEmpty())
	{
		Parts.Add(FString::Printf(TEXT("reason=%s"), *FailureReason));
	}

	return FString::Join(Parts, TEXT(", "));
}

FPathFinderVehicleContext FPathFinderVehicleContextResolver::Resolve(UWorld* World)
{
	FPathFinderVehicleContext Context;
	Context.World = World;

	if (World == nullptr)
	{
		Context.FailureReason = TEXT("no world");
		return Context;
	}

	Context.PlayerController = World->GetFirstPlayerController();
	if (Context.PlayerController == nullptr)
	{
		Context.FailureReason = TEXT("no first player controller");
		return Context;
	}

	Context.Pawn = Context.PlayerController->GetPawn();
	if (Context.Pawn == nullptr)
	{
		Context.FailureReason = TEXT("player controller has no pawn");
		return Context;
	}

	Context.WheeledVehicle = Cast<AFGWheeledVehicle>(Context.Pawn);

	if (Context.WheeledVehicle == nullptr)
	{
		const AFGCharacterPlayer* CharacterPlayer = Cast<AFGCharacterPlayer>(Context.Pawn);
		if (CharacterPlayer != nullptr)
		{
			Context.WheeledVehicle = Cast<AFGWheeledVehicle>(CharacterPlayer->GetDrivenVehicle());
		}
	}

	if (Context.WheeledVehicle == nullptr)
	{
		Context.FailureReason = TEXT("current pawn is not a wheeled vehicle and is not driving one");
		return Context;
	}

	Context.VehicleIdentifier = Context.WheeledVehicle->GetVehicleIdentifier();
	if (Context.VehicleIdentifier == nullptr)
	{
		Context.FailureReason = TEXT("wheeled vehicle has no identifier");
		return Context;
	}

	Context.VehiclePathPreset = Context.WheeledVehicle->GetVehiclePathPreset();
	return Context;
}
