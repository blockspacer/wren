#ifndef UIINPUT_H
#define UIINPUT_H

#include <d2d1_3.h>
#include <dwrite_3.h>
#include <sstream>

class UIInput
{
    int inputIndex;
    TCHAR inputValue[30];
    bool active;
    bool secure;
    FLOAT locationX;
    FLOAT locationY;
    FLOAT labelWidth;
    FLOAT inputWidth;
    FLOAT height;
    ID2D1SolidColorBrush* labelBrush;
    ID2D1SolidColorBrush* inputBrush;
    ID2D1SolidColorBrush* inputBorderBrush;
    ID2D1SolidColorBrush* inputValueBrush;
    IDWriteTextFormat* inputValueTextFormat;
    IDWriteFactory2* writeFactory;
    ID2D1RoundedRectangleGeometry* inputGeometry;
    ID2D1DeviceContext1* d2dDeviceContext;
    IDWriteTextLayout* labelTextLayout;
    IDWriteTextLayout* inputValueTextLayout;
public:
    UIInput(
        const bool secure,
        const FLOAT locationX,
        const FLOAT locationY,
        const FLOAT labelWidth,
        const FLOAT inputWidth,
        const FLOAT height,
        ID2D1SolidColorBrush* labelBrush,
        ID2D1SolidColorBrush* inputBrush,
        ID2D1SolidColorBrush* inputBorderBrush,
        ID2D1SolidColorBrush* inputValueBrush,
        IDWriteTextFormat* inputValueTextFormat,
        ID2D1DeviceContext1* d2dDeviceContext,
        const char* inLabelText,
        IDWriteFactory2* writeFactory,
        IDWriteTextFormat* labelTextFormat,
        ID2D1Factory2* d2dFactory) :
        secure{ secure },
        locationX{ locationX },
        locationY{ locationY },
        labelWidth{ labelWidth },
        inputWidth{ inputWidth },
        height{ height },
        labelBrush{ labelBrush },
        inputBrush{ inputBrush },
        inputBorderBrush{ inputBorderBrush },
        inputValueBrush{ inputValueBrush },
        inputValueTextFormat{ inputValueTextFormat },
        writeFactory{ writeFactory },
        d2dDeviceContext{ d2dDeviceContext }
    {
        ZeroMemory(inputValue, sizeof(inputValue));

        std::wostringstream outLabelText;
        outLabelText << inLabelText;
        if (FAILED(writeFactory->CreateTextLayout(outLabelText.str().c_str(), (UINT32)outLabelText.str().size(), labelTextFormat, labelWidth, height, &labelTextLayout)))
            throw std::exception("Failed to create text layout for UIInput.");

        d2dFactory->CreateRoundedRectangleGeometry(D2D1::RoundedRect(D2D1::RectF(locationX + labelWidth + 10, locationY, locationX + labelWidth + inputWidth, locationY + height), 3.0f, 3.0f), &inputGeometry);
    }
    void Draw();
    bool IsActive();
    void SetActive(bool isActive);
    void PushCharacter(TCHAR c);
    void PopCharacter();
    bool DetectClick(FLOAT x, FLOAT y);
    const TCHAR* GetInputValue();
    void Clear();
};

#endif