#pragma once

class Utility
{
public:
	static XMFLOAT3 XMFLOAT3Sum(const XMFLOAT3 l, const XMFLOAT3 r);
	static bool DetectClick(const float topLeftX, const float topLeftY, const float bottomRightX, const float bottomRightY, const float mousePosX, const float mousePosY);
	static std::string ws2s(const std::wstring& wstr);
	static void PrintXMFLOAT3(const XMFLOAT3 vec);
};