#include "stdafx.h"
#include "Utility.h"

XMFLOAT3 XMFLOAT3Sum(XMFLOAT3 l, XMFLOAT3 r)
{
    return XMFLOAT3(l.x + r.x, l.y + r.y, l.z + r.z);
}

bool DetectClick(float topLeftX, float topLeftY, float bottomRightX, float bottomRightY, float mousePosX, float mousePosY)
{
	return mousePosX >= topLeftX && mousePosX <= bottomRightX && mousePosY >= topLeftY && mousePosY <= bottomRightY;
}

std::string ws2s(const std::wstring& wstr)
{
	using convert_typeX = std::codecvt_utf8<wchar_t>;
	std::wstring_convert<convert_typeX, wchar_t> converterX;

	return converterX.to_bytes(wstr);
}