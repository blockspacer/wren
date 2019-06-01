#pragma once

enum class EventType
{
	LeftMouseDown,
	LeftMouseUp,
	RightMouseDown,
	RightMouseUp,
	MiddleMouseDown,
	MiddleMouseUp,
	MouseMove,
	KeyDown,
	SystemKeyDown,
	SystemKeyUp,
	ChangeActiveLayer,
	CreateAccountFailed,
	CreateAccountSuccess,
	LoginFailed,
	LoginSuccess,
	CreateCharacterFailed,
	CreateCharacterSuccess,
	EnterWorldSuccess,
	WrongChecksum,
	OpcodeNotImplemented,
	DeleteCharacterSuccess,
	PlayerCorrection,
	DeleteGameObject,
	GameObjectUpdate,
	UIAbilityDropped,
	ActivateAbility,
	StartDraggingUIAbility
};