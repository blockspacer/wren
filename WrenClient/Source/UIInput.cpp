#include "UIInput.h"
#include <wchar.h>

void UIInput::Draw()
{
    // Draw Label
    d2dDeviceContext->DrawTextLayout(D2D1::Point2F(locationX, locationY), labelTextLayout, labelBrush);

    // Draw Input
    float borderWeight = active ? 2.0f : 1.0f;
    d2dDeviceContext->FillGeometry(inputGeometry, inputBrush);
    d2dDeviceContext->DrawGeometry(inputGeometry, inputBorderBrush, borderWeight);
    
    // Draw Input Text
    std::wostringstream outInputValue;
    if (!secure)
        outInputValue << inputValue;
    else
    {
        for (auto i = 0; i < wcslen(inputValue); i++)
            outInputValue << "*";
    }
    if (active)
        outInputValue << "|";
    if (FAILED(writeFactory->CreateTextLayout(outInputValue.str().c_str(), (UINT32)outInputValue.str().size(), inputValueTextFormat, inputWidth, height - 2, &inputValueTextLayout))) // (height - 2) takes the border into account, and looks more natural
        throw std::exception("Critical error: Failed to create the text layout for FPS information!");
    d2dDeviceContext->DrawTextLayout(D2D1::Point2F(locationX + labelWidth + 14, locationY), inputValueTextLayout, inputValueBrush);
}

bool UIInput::IsActive()
{
    return active;
}

void UIInput::SetActive(bool isActive)
{
    active = isActive;
}

void UIInput::PushCharacter(TCHAR c)
{
    if (inputIndex > 30)
        return;

    inputValue[inputIndex] = c;
    inputIndex++;
}

void UIInput::PopCharacter()
{
    if (inputIndex == 0)    
        return;

    inputValue[inputIndex - 1] = 0;
    inputIndex--;
}

bool UIInput::DetectClick(FLOAT x, FLOAT y)
{
    return x >= locationX + labelWidth && x <= locationX + inputWidth + labelWidth && y >= locationY && y <= locationY + height;
}

const TCHAR* UIInput::GetInputValue()
{
    return inputValue;
}

void UIInput::Clear()
{
    inputIndex = 0;
    ZeroMemory(inputValue, sizeof(inputValue));
}