// This class is intended to provide support for Local Mixed play between a mouse and keyboard player
// and a VR player. It is not needed outside of that use.

#pragma once
#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "CoreMinimal.h"

#include "VRGameViewportClient.generated.h"

UENUM(Blueprintable)
enum class EVRGameInputMethod : uint8
{
	GameInput_Default,
	GameInput_SharedKeyboardAndMouse,
	GameInput_KeyboardAndMouseToPlayer2,
};


/**
* Subclass this in a blueprint to overwrite how default input is passed around in engine between local characters.
* Generally used for passing keyboard / mouse input to a secondary local player for local mixed gameplay in VR
*/
UCLASS(Blueprintable)
class VREXPANSIONPLUGIN_API UVRGameViewportClient : public UGameViewportClient
{
	GENERATED_BODY()

public:

	// Input Method for the viewport
	UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "VRExpansionPlugin")
		EVRGameInputMethod GameInputMethod;


	virtual bool InputKey(FViewport* tViewport, int32 ControllerId, FKey Key, EInputEvent EventType, float AmountDepressed = 1.f, bool bGamepad = false) override
	{

		const int32 NumLocalPlayers = World->GetGameInstance()->GetNumLocalPlayers();

		// Early out if a gamepad event or ignoring input or is default setup / no GEngine
		if(NumLocalPlayers < 2 || GameInputMethod == EVRGameInputMethod::GameInput_Default || IgnoreInput() || bGamepad)
			return Super::InputKey(tViewport, ControllerId, Key, EventType, AmountDepressed, bGamepad);

		if(GameInputMethod == EVRGameInputMethod::GameInput_KeyboardAndMouseToPlayer2)
		{
			// keyboard / mouse always go to player 0, so + 1 will be player 2
			++ControllerId;
			return Super::InputKey(tViewport, ControllerId, Key, EventType, AmountDepressed, bGamepad);
		}
		else // Shared keyboard and mouse
		{
			bool bRetVal = false;
			for (int32 i = 0; i < NumLocalPlayers; i++)
			{
				bRetVal = Super::InputKey(tViewport, i, Key, EventType, AmountDepressed, bGamepad) || bRetVal;
			}

			return bRetVal;
		}
	}

	virtual bool InputAxis(FViewport* tViewport, int32 ControllerId, FKey Key, float Delta, float DeltaTime, int32 NumSamples = 1, bool bGamepad = false) override
	{		
		
		const int32 NumLocalPlayers = World->GetGameInstance()->GetNumLocalPlayers();

		// Early out if a gamepad or not a mouse event (vr controller) or ignoring input or is default setup / no GEngine
		if (!Key.IsMouseButton() || NumLocalPlayers < 2 || GameInputMethod == EVRGameInputMethod::GameInput_Default || IgnoreInput() || bGamepad)
			return Super::InputAxis(tViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);

		if (GameInputMethod == EVRGameInputMethod::GameInput_KeyboardAndMouseToPlayer2)
		{
			// keyboard / mouse always go to player 0, so + 1 will be player 2
			++ControllerId;
			return Super::InputAxis(tViewport, ControllerId, Key, Delta, DeltaTime, NumSamples, bGamepad);
		}
		else // Shared keyboard and mouse
		{
			bool bRetVal = false;
			for (int32 i = 0; i < NumLocalPlayers; i++)
			{
				bRetVal = Super::InputAxis(tViewport, i, Key, Delta, DeltaTime, NumSamples, bGamepad) || bRetVal;
			}

			return bRetVal;
		}

	}


};