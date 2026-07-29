#include "ConveyorSignsConsoleCommands.h"

#include "ConveyorSignsAttachmentLayout.h"
#include "ConveyorSignsEndpoints.h"
#include "ConveyorSignsMirrorPolicy.h"
#include "ConveyorSignsTestSignPolicy.h"

#include "Buildables/FGBuildableConveyorLift.h"
#include "Buildables/FGBuildablePassthrough.h"
#include "Buildables/FGBuildableWidgetSign.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FGFactoryConnectionComponent.h"
#include "FGSaveInterface.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Misc/OutputDevice.h"

namespace
{
	const FName ConveyorSignsTestSourceTag(TEXT("ConveyorSigns.AngleTest.Source"));

	struct FConveyorSignsLiftSample
	{
		AFGBuildableConveyorLift* Lift = nullptr;
		double DistanceCentimeters = 0.0;
		int32 SnappedHoleCount = 0;
	};

	void WriteConveyorSignsLine(FOutputDevice& Output, const FString& Line)
	{
		Output.Logf(TEXT("%s"), *Line);
	}

	int32 CountSnappedHoles(AFGBuildableConveyorLift* Lift)
	{
		int32 Count = 0;
		for (AFGBuildablePassthrough* Passthrough : Lift->GetSnappedPassthroughs())
		{
			if (IsValid(Passthrough))
			{
				++Count;
			}
		}
		return Count;
	}

	bool MatchesCategory(const FConveyorSignsLiftSample& Sample, const FString& Category)
	{
		if (Category.Equals(TEXT("hole"), ESearchCase::IgnoreCase))
		{
			return Sample.SnappedHoleCount > 0;
		}
		if (Category.Equals(TEXT("standalone"), ESearchCase::IgnoreCase))
		{
			return Sample.SnappedHoleCount == 0;
		}
		return true;
	}

	TArray<FConveyorSignsLiftSample> FindLiftSamples(UWorld* World, const FVector& ReferenceLocation, const FString& Category)
	{
		TArray<FConveyorSignsLiftSample> Samples;
		if (World == nullptr)
		{
			return Samples;
		}

		for (TActorIterator<AFGBuildableConveyorLift> LiftIterator(World); LiftIterator; ++LiftIterator)
		{
			AFGBuildableConveyorLift* Lift = *LiftIterator;
			if (!IsValid(Lift))
			{
				continue;
			}

			FConveyorSignsLiftSample Sample;
			Sample.Lift = Lift;
			Sample.DistanceCentimeters = FVector::Distance(ReferenceLocation, Lift->GetActorLocation());
			Sample.SnappedHoleCount = CountSnappedHoles(Lift);
			if (MatchesCategory(Sample, Category))
			{
				Samples.Add(Sample);
			}
		}

		Samples.Sort([](const FConveyorSignsLiftSample& Left, const FConveyorSignsLiftSample& Right)
		{
			return Left.DistanceCentimeters < Right.DistanceCentimeters;
		});
		return Samples;
	}

	AFGBuildableConveyorLift* FindLiftByName(UWorld* World, const FString& LiftName)
	{
		if (World == nullptr || LiftName.IsEmpty())
		{
			return nullptr;
		}
		for (TActorIterator<AFGBuildableConveyorLift> LiftIterator(World); LiftIterator; ++LiftIterator)
		{
			AFGBuildableConveyorLift* Lift = *LiftIterator;
			if (IsValid(Lift) && Lift->GetName().Equals(LiftName, ESearchCase::CaseSensitive))
			{
				return Lift;
			}
		}
		return nullptr;
	}

	const TCHAR* FaceName(const EConveyorSignsFace Face)
	{
		switch (Face)
		{
		case EConveyorSignsFace::Left:
			return TEXT("Left");
		case EConveyorSignsFace::Right:
			return TEXT("Right");
		case EConveyorSignsFace::Rear:
			return TEXT("Rear");
		default:
			return TEXT("Unknown");
		}
	}

	bool TryParseFace(const FString& Value, EConveyorSignsFace& Face)
	{
		if (Value.Equals(TEXT("left"), ESearchCase::IgnoreCase))
		{
			Face = EConveyorSignsFace::Left;
			return true;
		}
		if (Value.Equals(TEXT("right"), ESearchCase::IgnoreCase))
		{
			Face = EConveyorSignsFace::Right;
			return true;
		}
		if (Value.Equals(TEXT("rear"), ESearchCase::IgnoreCase))
		{
			Face = EConveyorSignsFace::Rear;
			return true;
		}
		return false;
	}

	AFGBuildableWidgetSign* FindSignTemplate(UWorld* World)
	{
		for (TActorIterator<AFGBuildableWidgetSign> SignIterator(World); SignIterator; ++SignIterator)
		{
			AFGBuildableWidgetSign* Sign = *SignIterator;
			if (IsValid(Sign) &&
				!FConveyorSignsMirrorPolicy::IsMirror(Sign) &&
				!Sign->Tags.Contains(ConveyorSignsTestSourceTag))
			{
				return Sign;
			}
		}
		return nullptr;
	}

	void DescribeNearestLift(AFGBuildableConveyorLift* Lift, FOutputDevice& Output)
	{
		if (!IsValid(Lift))
		{
			WriteConveyorSignsLine(Output, TEXT("lift unavailable"));
			return;
		}

		UFGFactoryConnectionComponent* InputConnector = FConveyorSignsEndpoints::GetInputConnector(Lift);
		UFGFactoryConnectionComponent* OutputConnector = FConveyorSignsEndpoints::GetOutputConnector(Lift);
		const FTransform LiftTransform = Lift->GetActorTransform();
		const FConveyorSignsEndpointTransforms Endpoints = FConveyorSignsEndpoints::GetTransforms(Lift);
		const FTransform InputWorldTransform = Endpoints.Input * LiftTransform;
		const FTransform OutputWorldTransform = Endpoints.Output * LiftTransform;

		WriteConveyorSignsLine(Output, FString::Printf(
			TEXT("lift name=%s class=%s holes=%d actorLocation=%s actorRotation=%s topLocation=%s topRotation=%s inputLocation=%s inputRotation=%s outputLocation=%s outputRotation=%s inputConnector=%s inputConnected=%s inputDirection=%d outputConnector=%s outputConnected=%s outputDirection=%d"),
			*Lift->GetName(),
			*Lift->GetClass()->GetName(),
			CountSnappedHoles(Lift),
			*LiftTransform.GetTranslation().ToCompactString(),
			*LiftTransform.Rotator().ToCompactString(),
			*Lift->GetTopTransform().GetTranslation().ToCompactString(),
			*Lift->GetTopTransform().Rotator().ToCompactString(),
			*InputWorldTransform.GetTranslation().ToCompactString(),
			*InputWorldTransform.Rotator().ToCompactString(),
			*OutputWorldTransform.GetTranslation().ToCompactString(),
			*OutputWorldTransform.Rotator().ToCompactString(),
			InputConnector != nullptr ? TEXT("present") : TEXT("missing"),
			InputConnector != nullptr && InputConnector->IsConnected() ? TEXT("true") : TEXT("false"),
			InputConnector != nullptr ? static_cast<int32>(InputConnector->GetDirection()) : INDEX_NONE,
			OutputConnector != nullptr ? TEXT("present") : TEXT("missing"),
			OutputConnector != nullptr && OutputConnector->IsConnected() ? TEXT("true") : TEXT("false"),
			OutputConnector != nullptr ? static_cast<int32>(OutputConnector->GetDirection()) : INDEX_NONE));

		int32 HoleIndex = 0;
		for (AFGBuildablePassthrough* Passthrough : Lift->GetSnappedPassthroughs())
		{
			if (!IsValid(Passthrough))
			{
				continue;
			}
			WriteConveyorSignsLine(Output, FString::Printf(
				TEXT("hole index=%d name=%s class=%s location=%s rotation=%s thickness=%.3f"),
				HoleIndex,
				*Passthrough->GetName(),
				*Passthrough->GetClass()->GetName(),
				*Passthrough->GetActorLocation().ToCompactString(),
				*Passthrough->GetActorRotation().ToCompactString(),
				Passthrough->GetSnappedBuildingThickness()));
			++HoleIndex;
		}

		const EConveyorSignsFace Faces[] = {
			EConveyorSignsFace::Left,
			EConveyorSignsFace::Right,
			EConveyorSignsFace::Rear};
		int32 MatchingSignCount = 0;
		for (TActorIterator<AFGBuildableWidgetSign> SignIterator(Lift->GetWorld()); SignIterator; ++SignIterator)
		{
			AFGBuildableWidgetSign* Sign = *SignIterator;
			if (!IsValid(Sign))
			{
				continue;
			}

			const bool IsMirror = FConveyorSignsMirrorPolicy::IsMirror(Sign);
			const FTransform EndpointTransform = IsMirror ? Endpoints.Output : Endpoints.Input;
			double ClosestDistance = TNumericLimits<double>::Max();
			EConveyorSignsFace ClosestFace = EConveyorSignsFace::Left;
			FTransform ExpectedTransform;
			for (const EConveyorSignsFace Face : Faces)
			{
				const FTransform CandidateTransform =
					FConveyorSignsAttachmentLayout::CreateFaceTransform(EndpointTransform, Face)
					* LiftTransform;
				const double CandidateDistance = FVector::Distance(
					Sign->GetActorLocation(),
					CandidateTransform.GetTranslation());
				if (CandidateDistance < ClosestDistance)
				{
					ClosestDistance = CandidateDistance;
					ClosestFace = Face;
					ExpectedTransform = CandidateTransform;
				}
			}

			if (ClosestDistance > FConveyorSignsMirrorPolicy::MaxSourceDistance)
			{
				continue;
			}

			const double AngleErrorDegrees = FMath::RadiansToDegrees(
				Sign->GetActorQuat().AngularDistance(ExpectedTransform.GetRotation()));
			WriteConveyorSignsLine(Output, FString::Printf(
				TEXT("sign index=%d name=%s role=%s face=%s locationErrorCm=%.6f angleErrorDeg=%.6f rotation=%s expectedRotation=%s transient=%s shouldSave=%s"),
				MatchingSignCount,
				*Sign->GetName(),
				IsMirror ? TEXT("mirror") : TEXT("source"),
				FaceName(ClosestFace),
				ClosestDistance,
				AngleErrorDegrees,
				*Sign->GetActorRotation().ToCompactString(),
				*ExpectedTransform.Rotator().ToCompactString(),
				Sign->HasAnyFlags(RF_Transient) ? TEXT("true") : TEXT("false"),
				IFGSaveInterface::Execute_ShouldSave(Sign) ? TEXT("true") : TEXT("false")));
			++MatchingSignCount;
		}

		WriteConveyorSignsLine(Output, FString::Printf(TEXT("matchingSigns=%d"), MatchingSignCount));
	}

	void MovePlayerToLift(
		APlayerController* PlayerController,
		APawn* PlayerPawn,
		AFGBuildableConveyorLift* Lift)
	{
		const FConveyorSignsEndpointTransforms Endpoints = FConveyorSignsEndpoints::GetTransforms(Lift);
		const FTransform InputWorldTransform = Endpoints.Input * Lift->GetActorTransform();
		const FVector TargetLocation = InputWorldTransform.GetTranslation();
		const FVector ViewLocation = TargetLocation
			- (InputWorldTransform.GetRotation().GetForwardVector() * 700.0)
			+ (FVector::UpVector * 250.0);
		const FRotator ViewRotation = (TargetLocation - ViewLocation).Rotation();
		PlayerPawn->SetActorLocationAndRotation(
			ViewLocation,
			ViewRotation,
			false,
			nullptr,
			ETeleportType::TeleportPhysics);
		PlayerController->SetControlRotation(ViewRotation);
	}
}

void FConveyorSignsConsoleCommands::Register()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("ConveyorSigns.Help"),
		TEXT("List ConveyorSigns live diagnostic commands."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FConveyorSignsConsoleCommands::HelpWithOutput),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("ConveyorSigns.ListLiftSamples"),
		TEXT("List nearby lift samples. Usage: ConveyorSigns.ListLiftSamples [any|standalone|hole] [limit]"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FConveyorSignsConsoleCommands::ListLiftSamplesWithOutput),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("ConveyorSigns.InspectNearestLift"),
		TEXT("Inspect endpoint, hole, connector, and sign alignment data. Usage: ConveyorSigns.InspectNearestLift [any|standalone|hole]"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FConveyorSignsConsoleCommands::InspectNearestLiftWithOutput),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("ConveyorSigns.InspectLift"),
		TEXT("Inspect endpoint, hole, connector, and sign alignment data for an exact actor. Usage: ConveyorSigns.InspectLift <liftActorName>"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FConveyorSignsConsoleCommands::InspectLiftWithOutput),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("ConveyorSigns.MoveToLiftSample"),
		TEXT("Move to a nearby lift sample. Usage: ConveyorSigns.MoveToLiftSample <any|standalone|hole> [index]"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FConveyorSignsConsoleCommands::MoveToLiftSampleWithOutput),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("ConveyorSigns.MoveToLift"),
		TEXT("Move to an exact lift actor. Usage: ConveyorSigns.MoveToLift <liftActorName>"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FConveyorSignsConsoleCommands::MoveToLiftWithOutput),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("ConveyorSigns.CreateTestSign"),
		TEXT("Create a real save/reload test sign on a lift input face. Usage: ConveyorSigns.CreateTestSign <any|standalone|hole> [left|right|rear] [liftActorName]"),
		FConsoleCommandWithArgsAndOutputDeviceDelegate::CreateRaw(this, &FConveyorSignsConsoleCommands::CreateTestSignWithOutput),
		ECVF_Default));
	RegisterCommand(ConsoleManager.RegisterConsoleCommand(
		TEXT("ConveyorSigns.DeleteTestSigns"),
		TEXT("Delete all signs created by ConveyorSigns.CreateTestSign and their mirrors."),
		FConsoleCommandWithOutputDeviceDelegate::CreateRaw(this, &FConveyorSignsConsoleCommands::DeleteTestSignsWithOutput),
		ECVF_Default));
}

void FConveyorSignsConsoleCommands::Unregister()
{
	IConsoleManager& ConsoleManager = IConsoleManager::Get();
	for (IConsoleObject* ConsoleObject : RegisteredCommands)
	{
		if (ConsoleObject != nullptr)
		{
			ConsoleManager.UnregisterConsoleObject(ConsoleObject);
		}
	}
	RegisteredCommands.Empty();
}

UWorld* FConveyorSignsConsoleCommands::ResolveWorld() const
{
	if (GEngine == nullptr)
	{
		return nullptr;
	}
	for (const FWorldContext& WorldContext : GEngine->GetWorldContexts())
	{
		UWorld* World = WorldContext.World();
		if (World != nullptr && World->IsGameWorld())
		{
			return World;
		}
	}
	return nullptr;
}

void FConveyorSignsConsoleCommands::RegisterCommand(IConsoleObject* ConsoleObject)
{
	if (ConsoleObject != nullptr)
	{
		RegisteredCommands.Add(ConsoleObject);
	}
}

void FConveyorSignsConsoleCommands::HelpWithOutput(FOutputDevice& Output) const
{
	WriteConveyorSignsLine(Output, TEXT("ConveyorSigns.ListLiftSamples [any|standalone|hole] [limit]"));
	WriteConveyorSignsLine(Output, TEXT("ConveyorSigns.InspectNearestLift [any|standalone|hole]"));
	WriteConveyorSignsLine(Output, TEXT("ConveyorSigns.InspectLift <liftActorName>"));
	WriteConveyorSignsLine(Output, TEXT("ConveyorSigns.MoveToLiftSample <any|standalone|hole> [index]"));
	WriteConveyorSignsLine(Output, TEXT("ConveyorSigns.MoveToLift <liftActorName>"));
	WriteConveyorSignsLine(Output, TEXT("ConveyorSigns.CreateTestSign <any|standalone|hole> [left|right|rear] [liftActorName]"));
	WriteConveyorSignsLine(Output, TEXT("ConveyorSigns.DeleteTestSigns"));
}

void FConveyorSignsConsoleCommands::ListLiftSamplesWithOutput(
	const TArray<FString>& Arguments,
	FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	if (PlayerPawn == nullptr)
	{
		WriteConveyorSignsLine(Output, TEXT("liftSamples unavailable: player pawn missing"));
		return;
	}

	const FString Category = Arguments.Num() > 0 ? Arguments[0] : TEXT("any");
	const int32 RequestedLimit = Arguments.Num() > 1 ? FCString::Atoi(*Arguments[1]) : 20;
	const int32 Limit = FMath::Clamp(RequestedLimit, 1, 100);
	const TArray<FConveyorSignsLiftSample> Samples = FindLiftSamples(
		World,
		PlayerPawn->GetActorLocation(),
		Category);
	const int32 OutputCount = FMath::Min(Limit, Samples.Num());
	for (int32 SampleIndex = 0; SampleIndex < OutputCount; ++SampleIndex)
	{
		AFGBuildableConveyorLift* Lift = Samples[SampleIndex].Lift;
		WriteConveyorSignsLine(Output, FString::Printf(
			TEXT("liftSample index=%d name=%s class=%s distanceCm=%.3f holes=%d location=%s rotation=%s topLocation=%s topRotation=%s"),
			SampleIndex,
			*Lift->GetName(),
			*Lift->GetClass()->GetName(),
			Samples[SampleIndex].DistanceCentimeters,
			Samples[SampleIndex].SnappedHoleCount,
			*Lift->GetActorLocation().ToCompactString(),
			*Lift->GetActorRotation().ToCompactString(),
			*Lift->GetTopTransform().GetTranslation().ToCompactString(),
			*Lift->GetTopTransform().Rotator().ToCompactString()));
	}
	WriteConveyorSignsLine(Output, FString::Printf(
		TEXT("liftSamples category=%s total=%d returned=%d"),
		*Category,
		Samples.Num(),
		OutputCount));
}

void FConveyorSignsConsoleCommands::InspectNearestLiftWithOutput(
	const TArray<FString>& Arguments,
	FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	if (PlayerPawn == nullptr)
	{
		WriteConveyorSignsLine(Output, TEXT("nearestLift unavailable: player pawn missing"));
		return;
	}

	const FString Category = Arguments.Num() > 0 ? Arguments[0] : TEXT("any");
	const TArray<FConveyorSignsLiftSample> Samples = FindLiftSamples(
		World,
		PlayerPawn->GetActorLocation(),
		Category);
	if (Samples.Num() == 0)
	{
		WriteConveyorSignsLine(Output, FString::Printf(TEXT("nearestLift unavailable: category=%s"), *Category));
		return;
	}
	DescribeNearestLift(Samples[0].Lift, Output);
}

void FConveyorSignsConsoleCommands::InspectLiftWithOutput(
	const TArray<FString>& Arguments,
	FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	const FString LiftName = Arguments.Num() > 0 ? Arguments[0] : FString();
	AFGBuildableConveyorLift* Lift = FindLiftByName(World, LiftName);
	if (!IsValid(Lift))
	{
		WriteConveyorSignsLine(Output, FString::Printf(
			TEXT("inspectLift failed: lift=%s not found"),
			LiftName.IsEmpty() ? TEXT("<missing>") : *LiftName));
		return;
	}
	DescribeNearestLift(Lift, Output);
}

void FConveyorSignsConsoleCommands::MoveToLiftSampleWithOutput(
	const TArray<FString>& Arguments,
	FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	if (PlayerPawn == nullptr)
	{
		WriteConveyorSignsLine(Output, TEXT("move failed: player pawn missing"));
		return;
	}

	const FString Category = Arguments.Num() > 0 ? Arguments[0] : TEXT("any");
	const int32 SampleIndex = Arguments.Num() > 1 ? FCString::Atoi(*Arguments[1]) : 0;
	const TArray<FConveyorSignsLiftSample> Samples = FindLiftSamples(
		World,
		PlayerPawn->GetActorLocation(),
		Category);
	if (!Samples.IsValidIndex(SampleIndex))
	{
		WriteConveyorSignsLine(Output, FString::Printf(
			TEXT("move failed: category=%s index=%d available=%d"),
			*Category,
			SampleIndex,
			Samples.Num()));
		return;
	}

	AFGBuildableConveyorLift* Lift = Samples[SampleIndex].Lift;
	MovePlayerToLift(PlayerController, PlayerPawn, Lift);
	WriteConveyorSignsLine(Output, FString::Printf(
		TEXT("player moved category=%s index=%d lift=%s holes=%d location=%s rotation=%s"),
		*Category,
		SampleIndex,
		*Lift->GetName(),
		Samples[SampleIndex].SnappedHoleCount,
		*PlayerPawn->GetActorLocation().ToCompactString(),
		*PlayerController->GetControlRotation().ToCompactString()));
}

void FConveyorSignsConsoleCommands::MoveToLiftWithOutput(
	const TArray<FString>& Arguments,
	FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	const FString LiftName = Arguments.Num() > 0 ? Arguments[0] : FString();
	AFGBuildableConveyorLift* Lift = FindLiftByName(World, LiftName);
	if (PlayerPawn == nullptr || !IsValid(Lift))
	{
		WriteConveyorSignsLine(Output, FString::Printf(
			TEXT("moveToLift failed: lift=%s player=%s"),
			LiftName.IsEmpty() ? TEXT("<missing>") : *LiftName,
			PlayerPawn != nullptr ? TEXT("present") : TEXT("missing")));
		return;
	}
	MovePlayerToLift(PlayerController, PlayerPawn, Lift);
	WriteConveyorSignsLine(Output, FString::Printf(
		TEXT("player moved lift=%s holes=%d location=%s rotation=%s"),
		*Lift->GetName(),
		CountSnappedHoles(Lift),
		*PlayerPawn->GetActorLocation().ToCompactString(),
		*PlayerController->GetControlRotation().ToCompactString()));
}

void FConveyorSignsConsoleCommands::CreateTestSignWithOutput(
	const TArray<FString>& Arguments,
	FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	APlayerController* PlayerController = World != nullptr ? World->GetFirstPlayerController() : nullptr;
	APawn* PlayerPawn = PlayerController != nullptr ? PlayerController->GetPawn() : nullptr;
	if (PlayerPawn == nullptr)
	{
		WriteConveyorSignsLine(Output, TEXT("createTestSign failed: player pawn missing"));
		return;
	}

	const FString Category = Arguments.Num() > 0 ? Arguments[0] : TEXT("any");
	EConveyorSignsFace Face = EConveyorSignsFace::Left;
	if (Arguments.Num() > 1 && !TryParseFace(Arguments[1], Face))
	{
		WriteConveyorSignsLine(Output, FString::Printf(
			TEXT("createTestSign failed: invalid face=%s expected=left|right|rear"),
			*Arguments[1]));
		return;
	}

	AFGBuildableConveyorLift* Lift = nullptr;
	int32 SnappedHoleCount = 0;
	if (Arguments.Num() > 2)
	{
		Lift = FindLiftByName(World, Arguments[2]);
		SnappedHoleCount = IsValid(Lift) ? CountSnappedHoles(Lift) : 0;
		FConveyorSignsLiftSample ExactSample;
		ExactSample.Lift = Lift;
		ExactSample.SnappedHoleCount = SnappedHoleCount;
		if (!IsValid(Lift) || !MatchesCategory(ExactSample, Category))
		{
			WriteConveyorSignsLine(Output, FString::Printf(
				TEXT("createTestSign failed: lift=%s not found or does not match category=%s"),
				*Arguments[2],
				*Category));
			return;
		}
	}
	else
	{
		const TArray<FConveyorSignsLiftSample> Samples = FindLiftSamples(
			World,
			PlayerPawn->GetActorLocation(),
			Category);
		if (Samples.Num() > 0)
		{
			Lift = Samples[0].Lift;
			SnappedHoleCount = Samples[0].SnappedHoleCount;
		}
	}
	if (!IsValid(Lift))
	{
		WriteConveyorSignsLine(Output, FString::Printf(
			TEXT("createTestSign failed: category=%s no lift found"),
			*Category));
		return;
	}

	AFGBuildableWidgetSign* Template = FindSignTemplate(World);
	if (!IsValid(Template))
	{
		WriteConveyorSignsLine(Output, TEXT("createTestSign failed: no existing player sign is available as a class/data template"));
		return;
	}

	const FConveyorSignsEndpointTransforms Endpoints = FConveyorSignsEndpoints::GetTransforms(Lift);
	const FTransform SourceTransform =
		FConveyorSignsAttachmentLayout::CreateFaceTransform(Endpoints.Input, Face)
		* Lift->GetActorTransform();

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParameters.bDeferConstruction = true;
	SpawnParameters.Name = MakeUniqueObjectName(
		World,
		Template->GetClass(),
		FConveyorSignsTestSignPolicy::GetSourceBaseName());
	AFGBuildableWidgetSign* Source = World->SpawnActor<AFGBuildableWidgetSign>(
		Template->GetClass(),
		SourceTransform,
		SpawnParameters);
	if (!IsValid(Source))
	{
		WriteConveyorSignsLine(Output, TEXT("createTestSign failed: source spawn failed"));
		return;
	}

	Source->Tags.AddUnique(ConveyorSignsTestSourceTag);
	Source->SetReplicates(true);
	Source->SetReplicateMovement(true);
	Source->FinishSpawning(SourceTransform);
	FPrefabSignData SignData;
	Template->GetSignPrefabData(SignData);
	Source->SetPrefabSignData(SignData, false);
	const bool ShouldSave = IFGSaveInterface::Execute_ShouldSave(Source);
	if (!ShouldSave)
	{
		Source->Destroy();
		WriteConveyorSignsLine(Output, TEXT("createTestSign failed: generated source reports shouldSave=false"));
		return;
	}

	WriteConveyorSignsLine(Output, FString::Printf(
		TEXT("createTestSign source=%s lift=%s category=%s holes=%d face=%s location=%s rotation=%s shouldSave=true"),
		*Source->GetName(),
		*Lift->GetName(),
		*Category,
		SnappedHoleCount,
		FaceName(Face),
		*Source->GetActorLocation().ToCompactString(),
		*Source->GetActorRotation().ToCompactString()));
}

void FConveyorSignsConsoleCommands::DeleteTestSignsWithOutput(FOutputDevice& Output) const
{
	UWorld* World = ResolveWorld();
	if (World == nullptr)
	{
		WriteConveyorSignsLine(Output, TEXT("deleteTestSigns failed: game world missing"));
		return;
	}

	TArray<AFGBuildableWidgetSign*> TestSources;
	for (TActorIterator<AFGBuildableWidgetSign> SignIterator(World); SignIterator; ++SignIterator)
	{
		AFGBuildableWidgetSign* Sign = *SignIterator;
		if (IsValid(Sign)
			&& (Sign->Tags.Contains(ConveyorSignsTestSourceTag)
				|| FConveyorSignsTestSignPolicy::IsSourceName(Sign->GetFName())))
		{
			TestSources.Add(Sign);
		}
	}

	int32 DeletedCount = 0;
	for (AFGBuildableWidgetSign* Source : TestSources)
	{
		if (IsValid(Source) && Source->HasAuthority() && Source->Destroy())
		{
			++DeletedCount;
		}
	}
	WriteConveyorSignsLine(Output, FString::Printf(
		TEXT("deleteTestSigns matched=%d deleted=%d mirrors=deleted-by-source-lifecycle"),
		TestSources.Num(),
		DeletedCount));
}
