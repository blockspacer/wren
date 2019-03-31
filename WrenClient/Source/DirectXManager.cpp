#include "SocketManager.h"
#include <d3d11.h>
#include <d2d1_3.h>
#include <dwrite_3.h>
#include <iostream>
#include <sstream>
#include <string>
#include <locale>
#include <codecvt>
#include <tuple>
#include "DirectXManager.h"
#include <exception>
#include "Windows.h"
#include "Layer.h"	
#include "atlstr.h"
#include <fstream>  
#include "EventHandling/Events/ButtonPressEvent.h"
#include "EventHandling/Events/SelectCharacterListing.h"
#include "EventHandling/Events/ChangeActiveLayerEvent.h"
#include "EventHandling/Events/CreateAccountFailedEvent.h"
#include "EventHandling/Events/LoginSuccessEvent.h"
#include "EventHandling/Events/LoginFailedEvent.h"
#include "EventHandling/Events/CreateCharacterFailedEvent.h"
#include "EventHandling/Events/CreateCharacterSuccessEvent.h"

constexpr auto FAILED_TO_CREATE_DEVICE = "Failed to create device.";
constexpr auto FAILED_TO_GET_BACK_BUFFER = "Failed to get pointer to back buffer.";
constexpr auto FAILED_TO_SWAP_BUFFER = "Failed to swap buffer.";

DirectXManager::DirectXManager(GameTimer& timer, SocketManager& socketManager, EventHandler& eventHandler, ObjectManager& objectManager)
	: 
	Observer{ eventHandler },
	Publisher{ eventHandler },
	eventHandler{ eventHandler },
	objectManager{ objectManager },
	timer{ timer }, socketManager{ socketManager }
{
};

Layer activeLayer = Login;

std::string ws2s(const std::wstring& wstr)
{
    using convert_typeX = std::codecvt_utf8<wchar_t>;
    std::wstring_convert<convert_typeX, wchar_t> converterX;

    return converterX.to_bytes(wstr);
}

void DirectXManager::Initialize(HWND hWnd)
{
    HRESULT hr;
    D3D_FEATURE_LEVEL featureLevelsRequested[] =
    {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3,
        D3D_FEATURE_LEVEL_9_2,
        D3D_FEATURE_LEVEL_9_1
    };
    UINT numFeatureLevelsRequested = 1;
    D3D_FEATURE_LEVEL featureLevelsSupported;
    UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(DEBUG) || defined(_DEBUG)
    createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    hr = D3D11CreateDevice(
        0,
        D3D_DRIVER_TYPE_HARDWARE,
        0,
        createDeviceFlags,
        featureLevelsRequested,
        numFeatureLevelsRequested,
        D3D11_SDK_VERSION,
        &device,
        &featureLevelsSupported,
        &immediateContext
    );
    if (FAILED(hr))
        throw std::exception(FAILED_TO_CREATE_DEVICE);

    UINT msaaCount = 8;
    UINT m4xMsaaQuality = 0;
    hr = device->CheckMultisampleQualityLevels(DXGI_FORMAT_B8G8R8A8_UNORM, msaaCount, &m4xMsaaQuality);
    if (FAILED(hr))
        throw std::exception("Failed to check MSAA quality.");

    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 1;
    sd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
    sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = msaaCount;
    sd.SampleDesc.Quality = m4xMsaaQuality - 1;
    sd.Windowed = TRUE;
    sd.Flags = 0;

    RECT rect;
    GetClientRect(hWnd, &rect);
    clientWidth = rect.right - rect.left;
    clientHeight = rect.bottom - rect.top;

    // Get the dxgi device
    IDXGIDevice* dxgiDevice;
    hr = device->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgiDevice));
    if (FAILED(hr))
        throw std::exception("Failed to get DXGI Device.");

    // Get the dxgi adapter
    IDXGIAdapter* dxgiAdapter;
    hr = dxgiDevice->GetParent(__uuidof(IDXGIAdapter), (void**)&dxgiAdapter);
    if (FAILED(hr))
        throw std::exception("Failed to get DXGI Adapter.");

    // Get the dxgi factory interface
    IDXGIFactory* dxgiFactory;
    hr = dxgiAdapter->GetParent(__uuidof(IDXGIFactory), (void**)&dxgiFactory);
    if (FAILED(hr))
        throw std::exception("Failed to get DXGI Factory.");

    // Create swap chain
    hr = dxgiFactory->CreateSwapChain(device, &sd, &swapChain);
    if (FAILED(hr))
        throw std::exception("Failed to create swap chain.");

    // Get pointer to back buffer
    ID3D11Texture2D* backBuffer;
    hr = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&backBuffer);
    if (FAILED(hr))
        throw std::exception(FAILED_TO_GET_BACK_BUFFER);

    // Create a render-target view 
    device->CreateRenderTargetView(backBuffer, NULL, &renderTargetView);

    // Create Depth/Stencil Buffer and View
    D3D11_TEXTURE2D_DESC depthStencilDesc;
    depthStencilDesc.Width = clientWidth;
    depthStencilDesc.Height = clientHeight;
    depthStencilDesc.MipLevels = 1;
    depthStencilDesc.ArraySize = 1;
    depthStencilDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    depthStencilDesc.SampleDesc.Count = msaaCount;
    depthStencilDesc.SampleDesc.Quality = m4xMsaaQuality - 1;
    depthStencilDesc.Usage = D3D11_USAGE_DEFAULT;
    depthStencilDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    depthStencilDesc.CPUAccessFlags = 0;
    depthStencilDesc.MiscFlags = 0;

    ID3D11Texture2D* depthStencilBuffer;   
    hr = device->CreateTexture2D(&depthStencilDesc, 0, &depthStencilBuffer);
    if (FAILED(hr))
        throw std::exception("Failed to create texture 2d.");

    CD3D11_DEPTH_STENCIL_VIEW_DESC depthStencilViewDesc(D3D11_DSV_DIMENSION_TEXTURE2DMS);
    hr = device->CreateDepthStencilView(depthStencilBuffer, &depthStencilViewDesc, &depthStencilView);
    if (FAILED(hr))
        throw std::exception("Failed to create texture depth stencil view.");

    // Bind the view to output/merger stage
    immediateContext->OMSetRenderTargets(1, &renderTargetView, depthStencilView);

    CD3D11_RASTERIZER_DESC rasterDesc(D3D11_FILL_SOLID, D3D11_CULL_NONE, FALSE,
        D3D11_DEFAULT_DEPTH_BIAS, D3D11_DEFAULT_DEPTH_BIAS_CLAMP,
        D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, TRUE, FALSE);

    ID3D11RasterizerState* rasterState;
    device->CreateRasterizerState(&rasterDesc, &rasterState);
    immediateContext->RSSetState(rasterState);

    // Setup the viewport
    D3D11_VIEWPORT vp;
    vp.Width = (float)clientWidth;
    vp.Height = (float)clientHeight;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    immediateContext->RSSetViewports(1, &vp);

    // DWriteFactory
    hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&writeFactory));
    if (FAILED(hr))
        throw std::exception("Failed to initialize DirectWrite Factory.");

    // Direct2DFactory    
    D2D1_FACTORY_OPTIONS options;
    options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
    hr = D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, _uuidof(ID2D1Factory), &options, reinterpret_cast<void**>(&d2dFactory));
    if (FAILED(hr))
        throw std::exception("Failed to create Direct2D Factory.");

    // Create D2D Device
    ID2D1Device1* d2dDevice;
    hr = d2dFactory->CreateDevice(dxgiDevice, &d2dDevice);
    if (FAILED(hr))
        throw std::exception("Failed to get Direct2D Device.");

    // Create D2D Device Context   
    hr = d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_ENABLE_MULTITHREADED_OPTIMIZATIONS, &d2dDeviceContext);
    if (FAILED(hr))
        throw std::exception("Failed to get Direct2D Device Context.");

    // Set D2D Render Target to back buffer
    D2D1_BITMAP_PROPERTIES1 bp;
    bp.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    bp.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
    bp.dpiX = 96.0f;
    bp.dpiY = 96.0f;
    bp.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
    bp.colorContext = nullptr;

    // Get reference to DXGI back-buffer
    IDXGISurface1* dxgiBuffer;
    hr = swapChain->GetBuffer(0, __uuidof(IDXGISurface), reinterpret_cast<void**>(&dxgiBuffer));
    if (FAILED(hr))
        throw std::exception("Failed to retrieve the back buffer.");

    // create the bitmap
    ID2D1Bitmap1* targetBitmap;
    hr = d2dDeviceContext->CreateBitmapFromDxgiSurface(dxgiBuffer, &bp, &targetBitmap);
    if (FAILED(hr))
        throw std::exception("Failed to create the Direct2D bitmap from the DXGI surface.");

    // set the newly created bitmap as render target
    d2dDeviceContext->SetTarget(targetBitmap);

    InitializeBrushes();
    InitializeTextFormats();
    InitializeInputs();
    InitializeButtons();
    InitializeLabels();
    InitializePanels();
    InitializeGameWorld();

	eventHandler.PublishEvent(ChangeActiveLayerEvent{ Login });
}

void DirectXManager::InitializeBrushes()
{
    if (FAILED(d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.35f, 0.35f, 1.0f), &grayBrush)))
        throw std::exception("Critical error: Unable to create the gray brush!");

    if (FAILED(d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.22f, 0.22f, 1.0f), &blackBrush)))
        throw std::exception("Critical error: Unable to create the black brush!");

    if (FAILED(d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), &whiteBrush)))
        throw std::exception("Critical error: Unable to create the white brush!");

    if (FAILED(d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(0.619f, 0.854f, 1.0f, 1.0f), &blueBrush)))
        throw std::exception("Critical error: Unable to create the blue brush!");

    if (FAILED(d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(0.301f, 0.729f, 1.0f, 1.0f), &darkBlueBrush)))
        throw std::exception("Critical error: Unable to create the darkBlue brush!");

    if (FAILED(d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(0.137f, 0.98f, 0.117f, 1.0f), &successMessageBrush)))
        throw std::exception("Critical error: Unable to create the successMessage brush!");

    if (FAILED(d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.117f, 0.156f, 1.0f), &errorMessageBrush)))
        throw std::exception("Critical error: Unable to create the errorMessage brush!");

    if (FAILED(d2dDeviceContext->CreateSolidColorBrush(D2D1::ColorF(0.921f, 1.0f, 0.921f, 1.0f), &selectedCharacterBrush)))
        throw std::exception("Critical error: Unable to create the selectedCharacter brush!");
}

void DirectXManager::InitializeTextFormats()
{
    const WCHAR* arialFontFamily = L"Arial";
    const WCHAR* locale = L"en-US";

    // FPS / MousePos
    if (FAILED(writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, locale, &textFormatFPS)))
        throw std::exception("Critical error: Unable to create text format for FPS information!");
    if (FAILED(textFormatFPS->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING)))
        throw std::exception("Critical error: Unable to set text alignment!");
    if (FAILED(textFormatFPS->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR)))
        throw std::exception("Critical error: Unable to set paragraph alignment!");

    // Account Creds Input Values
    if (FAILED(writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, locale, &textFormatAccountCredsInputValue)))
        throw std::exception("Critical error: Unable to create text format for FPS information!");
    if (FAILED(textFormatAccountCredsInputValue->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING)))
        throw std::exception("Critical error: Unable to set text alignment!");
    if (FAILED(textFormatAccountCredsInputValue->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER)))
        throw std::exception("Critical error: Unable to set paragraph alignment!");

    // Account Creds Labels
    if (FAILED(writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, locale, &textFormatAccountCreds)))
        throw std::exception("Critical error: Unable to create text format for FPS information!");
    if (FAILED(textFormatAccountCreds->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING)))
        throw std::exception("Critical error: Unable to set text alignment!");
    if (FAILED(textFormatAccountCreds->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER)))
        throw std::exception("Critical error: Unable to set paragraph alignment!");

    // Headers
    if (FAILED(writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, locale, &textFormatHeaders)))
        throw std::exception("Critical error: Unable to create text format for FPS information!");
    if (FAILED(textFormatHeaders->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING)))
        throw std::exception("Critical error: Unable to set text alignment!");
    if (FAILED(textFormatHeaders->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR)))
        throw std::exception("Critical error: Unable to set paragraph alignment!");

    // Button Text
    if (FAILED(writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, locale, &textFormatButtonText)))
        throw std::exception("Critical error: Unable to create text format for FPS information!");
    if (FAILED(textFormatButtonText->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER)))
        throw std::exception("Critical error: Unable to set text alignment!");
    if (FAILED(textFormatButtonText->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER)))
        throw std::exception("Critical error: Unable to set paragraph alignment!");

    // SuccessMessage Text
    if (FAILED(writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, locale, &textFormatSuccessMessage)))
        throw std::exception("Critical error: Unable to create text format for FPS information!");
    if (FAILED(textFormatSuccessMessage->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING)))
        throw std::exception("Critical error: Unable to set text alignment!");
    if (FAILED(textFormatSuccessMessage->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR)))
        throw std::exception("Critical error: Unable to set paragraph alignment!");
    
    // ErrorMessage Message
    if (FAILED(writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, locale, &textFormatErrorMessage)))
        throw std::exception("Critical error: Unable to create text format for FPS information!");
    if (FAILED(textFormatErrorMessage->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING)))
        throw std::exception("Critical error: Unable to set text alignment!");
    if (FAILED(textFormatErrorMessage->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR)))
        throw std::exception("Critical error: Unable to set paragraph alignment!");
}

void DirectXManager::InitializeInputs()
{
    // LoginScreen
    loginScreen_accountNameInput = new UIInput(DirectX::XMFLOAT3{ 15.0f, 20.0f, 0.0f }, objectManager, Login, eventHandler, false, 120.0f, 260.0f, 24.0f, blackBrush, whiteBrush, grayBrush, blackBrush, textFormatAccountCredsInputValue, d2dDeviceContext, "Account Name:", writeFactory, textFormatAccountCreds, d2dFactory);
    loginScreen_passwordInput = new UIInput(DirectX::XMFLOAT3{ 15.0f, 50.0f, 0.0f}, objectManager, Login, eventHandler, true, 120.0f, 260.0f, 24.0f, blackBrush, whiteBrush, grayBrush, blackBrush, textFormatAccountCredsInputValue, d2dDeviceContext, "Password:", writeFactory, textFormatAccountCreds, d2dFactory);
	loginScreen_inputGroup = new UIInputGroup(eventHandler, Login);
	loginScreen_inputGroup->AddInput(loginScreen_accountNameInput);
	loginScreen_inputGroup->AddInput(loginScreen_passwordInput);

    // CreateAccount
    createAccount_accountNameInput = new UIInput(DirectX::XMFLOAT3{ 15.0f, 20.0f, 0.0f }, objectManager, CreateAccount, eventHandler, false, 120.0f, 260.0f, 24.0f, blackBrush, whiteBrush, grayBrush, blackBrush, textFormatAccountCredsInputValue, d2dDeviceContext, "Account Name:", writeFactory, textFormatAccountCreds, d2dFactory);
    createAccount_passwordInput = new UIInput(DirectX::XMFLOAT3{ 15.0f, 50.0f, 0.0f }, objectManager, CreateAccount, eventHandler, true, 120.0f, 260.0f, 24.0f, blackBrush, whiteBrush, grayBrush, blackBrush, textFormatAccountCredsInputValue, d2dDeviceContext, "Password:", writeFactory, textFormatAccountCreds, d2dFactory);
	createAccount_inputGroup = new UIInputGroup(eventHandler, CreateAccount);
	createAccount_inputGroup->AddInput(createAccount_accountNameInput);
	createAccount_inputGroup->AddInput(createAccount_passwordInput);

    // CreateCharacter
    createCharacter_characterNameInput = new UIInput(DirectX::XMFLOAT3{ 15.0f, 20.0f, 0.0f }, objectManager, CreateCharacter, eventHandler, false, 140.0f, 260.0f, 24.0f, blackBrush, whiteBrush, grayBrush, blackBrush, textFormatAccountCredsInputValue, d2dDeviceContext, "Character Name:", writeFactory, textFormatAccountCreds, d2dFactory);
	createCharacter_inputGroup = new UIInputGroup(eventHandler, CreateCharacter);
	createCharacter_inputGroup->AddInput(createCharacter_characterNameInput);
}

void DirectXManager::InitializeButtons()
{
	const auto onClickLoginButton = [this]()
	{
		loginScreen_successMessageLabel->SetText("");
		loginScreen_errorMessageLabel->SetText("");
		
		const auto accountName = ws2s(std::wstring(loginScreen_accountNameInput->GetInputValue()));
		const auto password = ws2s(std::wstring(loginScreen_passwordInput->GetInputValue()));

		if (accountName.length() == 0)
		{
			loginScreen_errorMessageLabel->SetText("Username field can't be empty.");
			return;
		}
		if (password.length() == 0)
		{
			loginScreen_errorMessageLabel->SetText("Password field can't be empty.");
			return;
		}

		socketManager.SendPacket(OPCODE_CONNECT, 2, accountName, password);
		SetActiveLayer(Connecting);
	};

	const auto onClickLoginScreenCreateAccountButton = [this]()
	{
		loginScreen_successMessageLabel->SetText("");
		loginScreen_errorMessageLabel->SetText("");
		SetActiveLayer(CreateAccount);
	};

    // LoginScreen
	loginScreen_loginButton = new UIButton(DirectX::XMFLOAT3{ 145.0f, 96.0f, 0.0f }, objectManager, Login, eventHandler, 80.0f, 24.0f, onClickLoginButton, blueBrush, darkBlueBrush, grayBrush, blackBrush, d2dDeviceContext, "LOGIN", writeFactory, textFormatButtonText, d2dFactory);
    loginScreen_createAccountButton = new UIButton(DirectX::XMFLOAT3{ 15.0f, 522.0f, 0.0f }, objectManager, Login, eventHandler, 160.0f, 24.0f, onClickLoginScreenCreateAccountButton, blueBrush, darkBlueBrush, grayBrush, blackBrush, d2dDeviceContext, "CREATE ACCOUNT", writeFactory, textFormatButtonText, d2dFactory);

	//const auto onClickCreateAccountCreateAccountButton = [this]()
	//{
	//	createAccount_errorMessageLabel->SetText("");

	//	const auto accountName = ws2s(std::wstring(createAccount_accountNameInput->GetInputValue()));
	//	const auto password = ws2s(std::wstring(createAccount_passwordInput->GetInputValue()));

	//	if (accountName.length() == 0)
	//	{
	//		createAccount_errorMessageLabel->SetText("Username field can't be empty.");
	//		return;
	//	}
	//	if (password.length() == 0)
	//	{
	//		createAccount_errorMessageLabel->SetText("Password field can't be empty.");
	//		return;
	//	}

	//	socketManager.SendPacket(OPCODE_CREATE_ACCOUNT, 2, accountName, password);
	//};

	//const auto onClickCreateAccountCancelButton = [this]()
	//{
	//	createAccount_errorMessageLabel->SetText("");
	//	SetActiveLayer(Login);
	//};

 //   // CreateAccount
 //   createAccount_createAccountButton = new UIButton(DirectX::XMFLOAT3{ 145.0f, 96.0f, 0.0f }, objectManager, CreateAccount, eventHandler, 80.0f, 24.0f, onClickCreateAccountCreateAccountButton, blueBrush, darkBlueBrush, grayBrush, blackBrush, d2dDeviceContext, "CREATE", writeFactory, textFormatButtonText, d2dFactory);
 //   createAccount_cancelButton = new UIButton(DirectX::XMFLOAT3{ 15.0f, 522.0f, 0.0f }, objectManager, CreateAccount, eventHandler, 80.0f, 24.0f, onClickCreateAccountCancelButton, blueBrush, darkBlueBrush, grayBrush, blackBrush, d2dDeviceContext, "CANCEL", writeFactory, textFormatButtonText, d2dFactory);

	//const auto onClickCharacterSelectNewCharacterButton = [this]()
	//{
	//	characterSelect_successMessageLabel->SetText("");
	//	SetActiveLayer(CreateCharacter);
	//};

	//const auto onClickCharacterSelectEnterWorldButton = [this]()
	//{
	//	characterSelect_successMessageLabel->SetText("");
	//	characterSelect_errorMessageLabel->SetText("");

	//	if (currentlySelectedCharacterName == nullptr)
	//		characterSelect_errorMessageLabel->SetText("You must select a character before entering the game.");
	//	else
	//	{
	//		socketManager.SendPacket(OPCODE_ENTER_WORLD, 2, token, currentlySelectedCharacterName);
	//		SetActiveLayer(EnteringWorld);
	//	}
	//};

	//const auto onClickCharacterSelectLogoutButton = [this]()
	//{
	//	token = "";
	//	SetActiveLayer(Login);
	//};

 //   // CharacterSelect
 //   characterSelect_newCharacterButton = new UIButton(DirectX::XMFLOAT3{ 15.0f, 20.0f, 0.0f }, objectManager, CharacterSelect, eventHandler, 140.0f, 24.0f, onClickCharacterSelectNewCharacterButton, blueBrush, darkBlueBrush, grayBrush, blackBrush, d2dDeviceContext, "NEW CHARACTER", writeFactory, textFormatButtonText, d2dFactory);
 //   characterSelect_enterWorldButton = new UIButton(DirectX::XMFLOAT3{ 170.0f, 20.0f, 0.0f }, objectManager, CharacterSelect, eventHandler, 120.0f, 24.0f, onClickCharacterSelectEnterWorldButton, blueBrush, darkBlueBrush, grayBrush, blackBrush, d2dDeviceContext, "ENTER WORLD", writeFactory, textFormatButtonText, d2dFactory);
 //   characterSelect_logoutButton = new UIButton(DirectX::XMFLOAT3{ 15.0f, 522.0f, 0.0f }, objectManager, CharacterSelect, eventHandler, 80.0f, 24.0f, onClickCharacterSelectLogoutButton, blueBrush, darkBlueBrush, grayBrush, blackBrush, d2dDeviceContext, "LOGOUT", writeFactory, textFormatButtonText, d2dFactory);
 //   
	//const auto onClickCreateCharacterCreateCharacterButton = [this]()
	//{
	//	createCharacter_errorMessageLabel->SetText("");

	//	const auto characterName = ws2s(std::wstring(createCharacter_characterNameInput->GetInputValue()));

	//	if (characterName.length() == 0)
	//	{
	//		createCharacter_errorMessageLabel->SetText("Character name can't be empty.");
	//		return;
	//	}

	//	socketManager.SendPacket(OPCODE_CREATE_CHARACTER, 2, token, characterName);
	//};

	//const auto onClickCreateCharacterBackButton = [this]()
	//{
	//	createCharacter_errorMessageLabel->SetText("");
	//	SetActiveLayer(CharacterSelect);
	//};

 //   // CreateCharacter
 //   createCharacter_createCharacterButton = new UIButton(DirectX::XMFLOAT3{ 165.0f, 64.0f, 0.0f }, objectManager, CreateCharacter, eventHandler, 160.0f, 24.0f, onClickCreateCharacterCreateCharacterButton, blueBrush, darkBlueBrush, grayBrush, blackBrush, d2dDeviceContext, "CREATE CHARACTER", writeFactory, textFormatButtonText, d2dFactory);
 //   createCharacter_backButton = new UIButton(DirectX::XMFLOAT3{ 15.0f, 522.0f, 0.0f }, objectManager, CreateCharacter, eventHandler, 80.0f, 24.0f, onClickCreateCharacterBackButton, blueBrush, darkBlueBrush, grayBrush, blackBrush, d2dDeviceContext, "BACK", writeFactory, textFormatButtonText, d2dFactory);
}

void DirectXManager::InitializeLabels()
{
    loginScreen_successMessageLabel = new UILabel(DirectX::XMFLOAT3{30.0f, 170.0f, 0.0f}, objectManager, Login, eventHandler, 400.0f, successMessageBrush, textFormatSuccessMessage, d2dDeviceContext, writeFactory, d2dFactory);
    loginScreen_errorMessageLabel = new UILabel(DirectX::XMFLOAT3{30.0f, 170.0f, 0.0f}, objectManager, Login, eventHandler, 400.0f, errorMessageBrush, textFormatErrorMessage, d2dDeviceContext, writeFactory, d2dFactory);

    createAccount_errorMessageLabel = new UILabel(DirectX::XMFLOAT3{30.0f, 170.0f, 0.0f}, objectManager, CreateAccount, eventHandler, 400.0f, errorMessageBrush, textFormatErrorMessage, d2dDeviceContext, writeFactory, d2dFactory);

    connecting_statusLabel = new UILabel(DirectX::XMFLOAT3{15.0f, 20.0f, 0.0f}, objectManager, Connecting, eventHandler, 400.0f,  blackBrush, textFormatAccountCreds, d2dDeviceContext, writeFactory, d2dFactory);
    connecting_statusLabel->SetText("Connecting...");

    characterSelect_successMessageLabel = new UILabel(DirectX::XMFLOAT3{30.0f, 400.0f, 0.0f}, objectManager, CharacterSelect, eventHandler, 400.0f, successMessageBrush, textFormatSuccessMessage, d2dDeviceContext, writeFactory, d2dFactory);
	characterSelect_errorMessageLabel = new UILabel(DirectX::XMFLOAT3{ 30.0f, 400.0f, 0.0f }, objectManager, CharacterSelect, eventHandler, 400.0f, errorMessageBrush, textFormatErrorMessage, d2dDeviceContext, writeFactory, d2dFactory);
	characterSelect_headerLabel = new UILabel(DirectX::XMFLOAT3{15.0f, 60.0f, 0.0f}, objectManager, CharacterSelect, eventHandler, 200.0f, blackBrush, textFormatHeaders, d2dDeviceContext, writeFactory, d2dFactory);
    characterSelect_headerLabel->SetText("Character List:");

    createCharacter_errorMessageLabel = new UILabel(DirectX::XMFLOAT3{30.0f, 170.0f, 0.0f}, objectManager, CreateCharacter, eventHandler, 400.0f, errorMessageBrush, textFormatErrorMessage, d2dDeviceContext, writeFactory, d2dFactory);

    enteringWorld_statusLabel = new UILabel(DirectX::XMFLOAT3{ 5.0f, 20.0f, 0.0f}, objectManager, EnteringWorld, eventHandler, 400.0f, blackBrush, textFormatAccountCreds, d2dDeviceContext, writeFactory, d2dFactory);
    enteringWorld_statusLabel->SetText("Entering World...");
}

void DirectXManager::InitializePanels()
{
    const auto gameSettingsPanelX = (clientWidth - 400.0f) / 2.0f;
    const auto gameSettingsPanelY = (clientHeight - 200.0f) / 2.0f;
    auto gameSettingsPanelHeader = new UILabel{DirectX::XMFLOAT3{2.0f, 2.0f, 0.0f}, objectManager, InGame, eventHandler, 200.0f, blackBrush, textFormatHeaders, d2dDeviceContext, writeFactory, d2dFactory};
    gameSettingsPanelHeader->SetText("Game Settings");
    gameSettingsPanel = new UIPanel(DirectX::XMFLOAT3{gameSettingsPanelX, gameSettingsPanelY, 0.0f}, objectManager, InGame, eventHandler, false, 400.0f, 200.0f, VK_ESCAPE, darkBlueBrush, whiteBrush, grayBrush, d2dDeviceContext, d2dFactory);
    gameSettingsPanel->AddChildComponent(*gameSettingsPanelHeader);

    const auto gameEditorPanelX = 580.0f;
    const auto gameEditorPanelY = 5.0f;
    auto gameEditorPanelHeader = new UILabel(DirectX::XMFLOAT3{2.0f, 2.0f, 0.0f}, objectManager, InGame, eventHandler, 200.0f, blackBrush, textFormatHeaders, d2dDeviceContext, writeFactory, d2dFactory);
    gameEditorPanelHeader->SetText("Game Editor");
    gameEditorPanel = new UIPanel(DirectX::XMFLOAT3{gameEditorPanelX, gameEditorPanelY, 0.0f}, objectManager, InGame, eventHandler, true, 200.0f, 400.0f, VK_F1, darkBlueBrush, whiteBrush, grayBrush, d2dDeviceContext, d2dFactory);
    gameEditorPanel->AddChildComponent(*gameEditorPanelHeader);
}

void DirectXManager::DrawScene()
{
    HRESULT hr;
    float color[4];
    color[3] = 1.0f;
    if (activeLayer == InGame)
    {
        color[0] = 0.5f;
        color[1] = 0.5f;
        color[2] = 0.5f;
    }
    else
    {
        color[0] = 1.0f;
        color[1] = 0.5f;
        color[2] = 0.3f;
    }
    immediateContext->ClearRenderTargetView(renderTargetView, color);
    immediateContext->ClearDepthStencilView(depthStencilView, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

    d2dDeviceContext->BeginDraw();

    // --- SHARED ---
    // draw FPS
    static int frameCnt = 0;
    static float timeElapsed = 0.0f;
    frameCnt++;

    if (timer.TotalTime() - timeElapsed >= 1)
    {
        float fps = (float)frameCnt;

        std::wostringstream outFPS;
        outFPS.precision(6);
        outFPS << "FPS: " << fps;

        if (FAILED(writeFactory->CreateTextLayout(outFPS.str().c_str(), (UINT32)outFPS.str().size(), textFormatFPS, (float)clientWidth, (float)clientHeight, &textLayoutFPS)))
            throw std::exception("Critical error: Failed to create the text layout for FPS information!");

        frameCnt = 0;
        timeElapsed += 1.0f;

        if (token != "")
            socketManager.SendPacket(OPCODE_HEARTBEAT, 1, token);
    }

    if (textLayoutFPS != nullptr)
        d2dDeviceContext->DrawTextLayout(D2D1::Point2F(540.0f, 540.0f), textLayoutFPS, blackBrush);

    // draw MousePos
    std::wostringstream outMousePos;
    outMousePos.precision(6);
    outMousePos << "MousePosX: " << mousePosX << ", MousePosY: " << mousePosY;
    writeFactory->CreateTextLayout(outMousePos.str().c_str(), (UINT32)outMousePos.str().size(), textFormatFPS, (float)clientWidth, (float)clientHeight, &textLayoutMousePos);
    d2dDeviceContext->DrawTextLayout(D2D1::Point2F(540.0f, 520.0f), textLayoutMousePos, blackBrush);

    UINT stride = sizeof(VERTEX);
    UINT offset = 0;

	// draw all GameObjects
	objectManager.Draw();

	// draw test triangle - TODO: move this into GameObjects
	if (activeLayer == InGame)
	{
		immediateContext->IASetVertexBuffers(0, 1, &buffer, &stride, &offset);
		immediateContext->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		immediateContext->Draw(3, 0);
	}
	
    d2dDeviceContext->EndDraw();
    
    hr = swapChain->Present(0, 0);
    if (FAILED(hr))
        throw std::exception("Present failed.");
}

void DirectXManager::RecreateCharacterListings(const std::vector<std::string>* characterNames)
{
    for (auto i = 0; i < characterList->size(); i++)
        delete(characterList->at(i));
    characterList->clear();

    for (auto i = 0; i < characterNames->size(); i++)
    {
        const float y = 100.0f + (i * 40.0f);
        characterList->push_back(new UICharacterListing(DirectX::XMFLOAT3{ 25.0f, y, 0.0f }, objectManager, CharacterSelect, eventHandler, 260.0f, 30.0f, whiteBrush, selectedCharacterBrush, grayBrush, blackBrush, d2dDeviceContext, characterNames->at(i).c_str(), writeFactory, textFormatAccountCredsInputValue, d2dFactory));
    }
}
void DirectXManager::InitializeGameWorld()
{
    VERTEX OurVertices[] =
    {
        { 0.0f, 0.5f, 0.5f }, { 0.5f, -0.5f, 0.5f }, { -0.5f, -0.5f, 0.5f }
    };

    D3D11_BUFFER_DESC bufferDesc;
    bufferDesc.Usage = D3D11_USAGE_DEFAULT;
    bufferDesc.ByteWidth = sizeof(OurVertices) * ARRAYSIZE(OurVertices);
    bufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bufferDesc.CPUAccessFlags = 0;
    bufferDesc.MiscFlags = 0;

    D3D11_SUBRESOURCE_DATA InitData;
    InitData.pSysMem = OurVertices;
    InitData.SysMemPitch = 0;
    InitData.SysMemSlicePitch = 0;

    device->CreateBuffer(&bufferDesc, &InitData, &buffer);

    ShaderBuffer vertexShaderBytes = LoadShader(L"VertexShader.cso");
    ID3D11VertexShader* vertexShader;
    device->CreateVertexShader(vertexShaderBytes.buffer, vertexShaderBytes.size, nullptr, &vertexShader);

    ShaderBuffer pixelShaderBytes = LoadShader(L"PixelShader.cso");
    ID3D11PixelShader* pixelShader;
    device->CreatePixelShader(pixelShaderBytes.buffer, pixelShaderBytes.size, nullptr, &pixelShader);

    immediateContext->VSSetShader(vertexShader, nullptr, 0);
    immediateContext->PSSetShader(pixelShader, nullptr, 0);

    D3D11_INPUT_ELEMENT_DESC ied[] =
    {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0}
    };

    ID3D11InputLayout* inputLayout;
    device->CreateInputLayout(ied, ARRAYSIZE(ied), vertexShaderBytes.buffer, vertexShaderBytes.size, &inputLayout);
    immediateContext->IASetInputLayout(inputLayout);
}

ShaderBuffer DirectXManager::LoadShader(std::wstring filename)
{
    // load precompiled shaders from .cso objects
    ShaderBuffer sb;
    byte* fileData = nullptr;

    // open the file
    std::ifstream csoFile(filename, std::ios::in | std::ios::binary | std::ios::ate);

    if (csoFile.is_open())
    {
        // get shader size
        sb.size = (unsigned int)csoFile.tellg();

        // collect shader data
        fileData = new byte[sb.size];
        csoFile.seekg(0, std::ios::beg);
        csoFile.read(reinterpret_cast<char*>(fileData), sb.size);
        csoFile.close();
        sb.buffer = fileData;
    }
    else
        throw std::exception("Critical error: Unable to open the compiled shader object!");

    return sb;
}

bool DirectXManager::HandleEvent(const Event& event)
{
	const auto type = event.type;
	switch (type)
	{
		case EventType::SelectCharacterListing:
		{
			const auto buttonPressEvent = (SelectCharacterListing&)event;

			currentlySelectedCharacterName = buttonPressEvent.characterName;

			break;
		}
		case EventType::DeselectCharacterListing:
		{
			currentlySelectedCharacterName = nullptr;

			break;
		}
		case EventType::CreateAccountFailed:
		{
			const auto derivedEvent = (CreateAccountFailedEvent&)event;

			createAccount_errorMessageLabel->SetText(("Failed to create account. Reason: " + derivedEvent.error).c_str());

			break;
		}
		case EventType::CreateAccountSuccess:
		{
			createAccount_errorMessageLabel->SetText("");
			loginScreen_successMessageLabel->SetText("Account created successfully.");
			SetActiveLayer(Login);

			break;
		}
		case EventType::LoginFailed:
		{
			const auto derivedEvent = (LoginFailedEvent&)event;

			loginScreen_errorMessageLabel->SetText(("Login failed. Reason: " + derivedEvent.error).c_str());
			SetActiveLayer(Login);

			break;
		}
		case EventType::LoginSuccess:
		{
			const auto derivedEvent = (LoginSuccessEvent&)event;

			token = derivedEvent.token;
			RecreateCharacterListings(derivedEvent.characterList);
			SetActiveLayer(CharacterSelect);

			break;
		}
		case EventType::CreateCharacterFailed:
		{
			const auto derivedEvent = (CreateCharacterFailedEvent&)event;

			createCharacter_errorMessageLabel->SetText(("Character creation failed. Reason: " + derivedEvent.error).c_str());

			break;
		}
		case EventType::CreateCharacterSuccess:
		{
			const auto derivedEvent = (CreateCharacterSuccessEvent&)event;

			RecreateCharacterListings(derivedEvent.characterList);
			createCharacter_errorMessageLabel->SetText("");
			characterSelect_successMessageLabel->SetText("Character created successfully.");
			SetActiveLayer(CharacterSelect);

			break;
		}
		case EventType::EnterWorldSuccess:
		{
			SetActiveLayer(InGame);

			break;
		}
	}

	return false;
}

void DirectXManager::SetActiveLayer(const Layer layer)
{
	activeLayer = layer;
	eventHandler.PublishEvent(ChangeActiveLayerEvent{ layer });
}