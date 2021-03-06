#pragma once

#include "GameTimer.h"

constexpr auto ACCELERATION = 0.02f;
constexpr auto MIN_SPEED    = 0.02f;
constexpr auto MAX_SPEED    = 0.00005f;
constexpr auto OFFSET_X     = 500.0f;
constexpr auto OFFSET_Y     = 700.0f;
constexpr auto OFFSET_Z     = -500.0f;

class Camera
{
	float camX{ 0.0f };
	float camY{ 0.0f };
	float camZ{ 0.0f };
	float currentSpeed{ MIN_SPEED };
public:
	void Update(const XMFLOAT3 vec, const float deltaTime);
	void Translate(const XMFLOAT3 pos) { camX += pos.x; camY += pos.y; camZ += pos.z; }
	const XMFLOAT3 GetPosition() const { return XMFLOAT3{ camX + OFFSET_X, camY + OFFSET_Y, camZ + OFFSET_Z }; }
	const XMFLOAT3 GetPositionFromOrigin() const { return XMFLOAT3{ camX, camY, camZ }; }
};