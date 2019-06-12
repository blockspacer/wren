#pragma once

#include "GameTimer.h"
#include "PlayerUpdate.h"
#include "GameObject.h"
#include "EventHandling/Observer.h"
#include "EventHandling/Events/PlayerCorrectionEvent.h"
#include "EventHandling/Events/MouseEvent.h"

static const int BUFFER_SIZE = 120;

class PlayerController
{
	float clientWidth{ 0.0f };
	float clientHeight{ 0.0f };
	bool isMoving{ false };
	XMFLOAT3 destination{ 0.0f, 0.0f, 0.0f };
	GameObject& player;
	std::unique_ptr<PlayerUpdate> playerUpdates[BUFFER_SIZE];
	int playerUpdateIdCounter{ 0 };

	void SetDestination(const XMFLOAT3 playerPos);
	void UpdateCurrentMouseDirection(const float mousePosX, const float mousePosY);
public:
	PlayerController(GameObject& player);
	void Update();
	void SetClientDimensions(const int width, const int height) { clientWidth = (float)width; clientHeight = (float)height; }
	PlayerUpdate* GeneratePlayerUpdate();
	void OnPlayerCorrectionEvent(PlayerCorrectionEvent* event);
	void OnRightMouseDownEvent(MouseEvent* event);
	void OnRightMouseUpEvent(MouseEvent* event);
	void OnMouseMoveEvent(MouseEvent* event);

	bool isRightClickHeld{ false };
	XMFLOAT3 currentMouseDirection{ 0.0f, 0.0f, 0.0f };
};