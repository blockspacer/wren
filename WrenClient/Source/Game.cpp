#include "stdafx.h"
#include "Game.h"
#include "ConstantBufferOnce.h"
#include "Camera.h"
#include "RenderComponent.h"
#include <OpCodes.h>
#include "EventHandling/Events/ChangeActiveLayerEvent.h"
#include "EventHandling/Events/CreateAccountFailedEvent.h"
#include "EventHandling/Events/LoginSuccessEvent.h"
#include "EventHandling/Events/LoginFailedEvent.h"
#include "EventHandling/Events/CreateCharacterFailedEvent.h"
#include "EventHandling/Events/CreateCharacterSuccessEvent.h"
#include "EventHandling/Events/DeleteCharacterSuccessEvent.h"
#include "EventHandling/Events/KeyDownEvent.h"
#include "EventHandling/Events/MouseEvent.h"
#include "EventHandling/Events/EnterWorldSuccessEvent.h"
#include "EventHandling/Events/GameObjectUpdateEvent.h"

SocketManager g_socketManager;

extern EventHandler g_eventHandler;

Game::Game() noexcept(false)
{
	m_deviceResources = std::make_unique<DX::DeviceResources>();
	m_deviceResources->RegisterDeviceNotify(this);

	g_eventHandler.Subscribe(*this);
}

// General initialization that would likely be shared between any D3D application
//   should go in DeviceResources.cpp
// Game-specific initialization should go in Game.cpp
void Game::Initialize(HWND window, int width, int height)
{
	m_deviceResources->SetWindow(window, width, height);

	m_deviceResources->CreateDeviceResources();
	CreateDeviceDependentResources();

	m_deviceResources->CreateWindowSizeDependentResources();
	CreateWindowSizeDependentResources();

	m_timer.Reset();
	SetActiveLayer(Login);
}

#pragma region Frame Update
void Game::Tick()
{
	while (g_socketManager.TryRecieveMessage()) {}

	m_timer.Tick();

	updateTimer += m_timer.DeltaTime();
	if (updateTimer >= UPDATE_FREQUENCY)
	{
		if (m_activeLayer == InGame)
		{
			m_playerController->Update(UPDATE_FREQUENCY);
			m_camera.Update(m_player->GetWorldPosition(), UPDATE_FREQUENCY);
			SyncWithServer(UPDATE_FREQUENCY);
			m_objectManager.Update(UPDATE_FREQUENCY);
		}
		
		g_eventHandler.PublishEvents();

		updateTimer -= UPDATE_FREQUENCY;
	}
	
	Render(updateTimer);
}

void Game::Update()
{
	float elapsedTime = float(m_timer.DeltaTime());

	// TODO: Add your game logic here.
	elapsedTime;
}
#pragma endregion

#pragma region Frame Render
void Game::Render(const float updateTimer)
{
	// Don't try to render anything before the first Update.
	if (m_timer.TotalTime() == 0)
		return;

	Clear();

	auto d3dContext = m_deviceResources->GetD3DDeviceContext();
	auto d2dContext = m_deviceResources->GetD2DDeviceContext();

	d2dContext->BeginDraw();

	// used for FPS text
	static int frameCnt = 0;
	static float timeElapsed = 0.0f;
	frameCnt++;
	
	// update FPS text
	if (m_timer.TotalTime() - timeElapsed >= 1)
	{
		float mspf = (float)1000 / frameCnt;
		const auto fpsText = "FPS: " + std::to_string(frameCnt) + ", MSPF: " + std::to_string(mspf);
		fpsTextLabel->SetText(fpsText.c_str());

		frameCnt = 0;
		timeElapsed += 1.0f;

		if (g_socketManager.Connected())
			g_socketManager.SendPacket(OPCODE_HEARTBEAT);
	}

	// update MousePos text
	const auto mousePosText = "MousePosX: " + std::to_string((int)m_mousePosX) + ", MousePosY: " + std::to_string((int)m_mousePosY);
	mousePosLabel->SetText(mousePosText.c_str());

	if (m_activeLayer == InGame)
	{
		auto camPos = m_camera.GetPosition();
		XMVECTORF32 s_Eye = { camPos.x, camPos.y, camPos.z, 0.0f };
		XMVECTORF32 s_At = { camPos.x - 500.0f, 0.0f, camPos.z + 500.0f, 0.0f };
		XMVECTORF32 s_Up = { 0.0f, 1.0f, 0.0f, 0.0f };
		m_viewTransform = XMMatrixLookAtLH(s_Eye, s_At, s_Up);

		d3dContext->RSSetState(solidRasterState.Get());
		//d3dContext->RSSetState(wireframeRasterState);

		m_gameMap->Draw(d3dContext, m_viewTransform, m_projectionTransform);

		m_renderComponentManager.Render(d3dContext, m_viewTransform, m_projectionTransform, updateTimer);

		// sprites
		testSprite->Draw(d3dContext, m_projectionTransform);
	}

	// foreach RenderComponent -> Draw
	for (auto i = 0; i < uiComponents.size(); i++)
		uiComponents.at(i)->Draw();

	d2dContext->EndDraw();

	d3dContext->ResolveSubresource(m_deviceResources->GetBackBufferRenderTarget(), 0, m_deviceResources->GetOffscreenRenderTarget(), 0, DXGI_FORMAT_B8G8R8A8_UNORM);

	// Show the new frame.
	m_deviceResources->Present();
}

void Game::Clear()
{
	// Clear the views.
	auto context = m_deviceResources->GetD3DDeviceContext();
	auto renderTarget = m_deviceResources->GetOffscreenRenderTargetView();
	auto depthStencil = m_deviceResources->GetDepthStencilView();

	context->ClearRenderTargetView(renderTarget, Colors::CornflowerBlue);
	context->ClearDepthStencilView(depthStencil, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
	context->OMSetRenderTargets(1, &renderTarget, depthStencil);

	// Set the viewport.
	auto viewport = m_deviceResources->GetScreenViewport();
	context->RSSetViewports(1, &viewport);
}
#pragma endregion

#pragma region Message Handlers
void Game::OnActivated()
{
	// TODO: Game is becoming active window.
}

void Game::OnDeactivated()
{
	// TODO: Game is becoming background window.
}

void Game::OnSuspending()
{
	// TODO: Game is being power-suspended (or minimized).
}

void Game::OnResuming()
{
	m_timer.Reset();

	// TODO: Game is being power-resumed (or returning from minimize).
}

void Game::OnWindowMoved()
{
	auto r = m_deviceResources->GetOutputSize();
	m_deviceResources->WindowSizeChanged(r.right, r.bottom);
}

void Game::OnWindowSizeChanged(int width, int height)
{
	if (!m_deviceResources->WindowSizeChanged(width, height))
		return;

	CreateWindowSizeDependentResources();

	m_clientWidth = width;
	m_clientHeight = height;

	if (m_playerController)
		m_playerController->SetClientDimensions(width, height);
}
#pragma endregion

#pragma region Direct3D Resources
// These are the resources that depend on the device.
void Game::CreateDeviceDependentResources()
{
	InitializeBrushes();
	InitializeTextFormats();
	InitializeInputs();
	InitializeButtons();
	InitializeLabels();
	InitializePanels();
	InitializeShaders();
	InitializeBuffers();
	InitializeTextures();
	InitializeMeshes();
	InitializeRasterStates();
	InitializeSprites();
	
	auto d3dDevice = m_deviceResources->GetD3DDevice();
	auto d2dDeviceContext = m_deviceResources->GetD2DDeviceContext();
	auto d2dFactory = m_deviceResources->GetD2DFactory();
	m_gameMap = std::make_unique<GameMap>(d3dDevice, vertexShaderBuffer.buffer, vertexShaderBuffer.size, vertexShader.Get(), pixelShader.Get(), textures[2].Get());

	// initialize tree. TODO: game world needs to be stored on disk somewhere and initialized on startup
	GameObject& tree = m_objectManager.CreateGameObject(XMFLOAT3{ 90.0f, 0.0f, 90.0f }, XMFLOAT3{ 14.0f, 14.0f, 14.0f });
	auto treeRenderComponent = m_renderComponentManager.CreateRenderComponent(tree.GetId(), meshes[1].get(), vertexShader.Get(), pixelShader.Get(), textures[1].Get());
	tree.SetRenderComponentId(treeRenderComponent.GetId());

	// init hotbar
	hotbar = std::make_unique<UIHotbar>(uiComponents, XMFLOAT3{ 5.0f, m_clientHeight - 45.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, blackBrush.Get(), d2dDeviceContext, d2dFactory);
}

// Allocate all memory resources that change on a window SizeChanged event.
void Game::CreateWindowSizeDependentResources()
{
	// TODO: Initialize windows-size dependent objects here.
}

void Game::OnDeviceLost()
{
	// TODO: Add Direct3D resource cleanup here.
}

void Game::OnDeviceRestored()
{
	CreateDeviceDependentResources();

	CreateWindowSizeDependentResources();

	SetActiveLayer(m_activeLayer);
}
#pragma endregion

#pragma region Properties
void Game::GetDefaultSize(int& width, int& height) const
{
	// TODO: Change to desired default window size (note minimum size is 320x200).
	width = 800;
	height = 600;
}
#pragma endregion

ShaderBuffer Game::LoadShader(const std::wstring filename)
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

void Game::InitializeBrushes()
{
	auto d2dContext = m_deviceResources->GetD2DDeviceContext();

	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.35f, 0.35f, 0.35f, 1.0f), grayBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.22f, 0.22f, 0.22f, 1.0f), blackBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(1.0f, 1.0f, 1.0f, 1.0f), whiteBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.619f, 0.854f, 1.0f, 1.0f), blueBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.301f, 0.729f, 1.0f, 1.0f), darkBlueBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.137f, 0.98f, 0.117f, 1.0f), successMessageBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.98f, 0.117f, 0.156f, 1.0f), errorMessageBrush.ReleaseAndGetAddressOf());
	d2dContext->CreateSolidColorBrush(D2D1::ColorF(0.921f, 1.0f, 0.921f, 1.0f), selectedCharacterBrush.ReleaseAndGetAddressOf());
}

void Game::InitializeTextFormats()
{
	auto arialFontFamily = L"Arial";
	auto locale = L"en-US";

	auto writeFactory = m_deviceResources->GetWriteFactory();

	// FPS / MousePos
	writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 10.0f, locale, textFormatFPS.ReleaseAndGetAddressOf());
	textFormatFPS->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatFPS->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// Account Creds Input Values
	writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, locale, textFormatAccountCredsInputValue.ReleaseAndGetAddressOf());
	textFormatAccountCredsInputValue->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatAccountCredsInputValue->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	// Account Creds Labels
	writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, locale, textFormatAccountCreds.ReleaseAndGetAddressOf());
	textFormatAccountCreds->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
	textFormatAccountCreds->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	// Headers
	writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 14.0f, locale, textFormatHeaders.ReleaseAndGetAddressOf());
	textFormatHeaders->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatHeaders->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// Button Text
	writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, locale, textFormatButtonText.ReleaseAndGetAddressOf());
	textFormatButtonText->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
	textFormatButtonText->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);

	// SuccessMessage Text
	writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, locale, textFormatSuccessMessage.ReleaseAndGetAddressOf());
	textFormatSuccessMessage->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatSuccessMessage->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);

	// ErrorMessage Message
	writeFactory->CreateTextFormat(arialFontFamily, nullptr, DWRITE_FONT_WEIGHT_BOLD, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, 12.0f, locale, textFormatErrorMessage.ReleaseAndGetAddressOf());
	textFormatErrorMessage->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
	textFormatErrorMessage->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
}

void Game::InitializeInputs()
{
	auto writeFactory = m_deviceResources->GetWriteFactory();
	auto d2dFactory = m_deviceResources->GetD2DFactory();
	auto d2dContext = m_deviceResources->GetD2DDeviceContext();

	// LoginScreen
	loginScreen_accountNameInput = std::make_unique<UIInput>(uiComponents, XMFLOAT3{ 15.0f, 20.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, Login, false, 120.0f, 260.0f, 24.0f, "Account Name:", blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), d2dContext, writeFactory, textFormatAccountCreds.Get(), d2dFactory);
	loginScreen_passwordInput = std::make_unique<UIInput>(uiComponents, XMFLOAT3{ 15.0f, 50.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, Login, true, 120.0f, 260.0f, 24.0f, "Password:", blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), d2dContext, writeFactory, textFormatAccountCreds.Get(), d2dFactory);
	loginScreen_inputGroup = std::make_unique<UIInputGroup>(Login);
	loginScreen_inputGroup->AddInput(loginScreen_accountNameInput.get());
	loginScreen_inputGroup->AddInput(loginScreen_passwordInput.get());

	// CreateAccount
	createAccount_accountNameInput = std::make_unique<UIInput>(uiComponents, XMFLOAT3{ 15.0f, 20.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CreateAccount, false, 120.0f, 260.0f, 24.0f, "Account Name:", blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), d2dContext, writeFactory, textFormatAccountCreds.Get(), d2dFactory);
	createAccount_passwordInput = std::make_unique<UIInput>(uiComponents, XMFLOAT3{ 15.0f, 50.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CreateAccount, true, 120.0f, 260.0f, 24.0f, "Password:", blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), d2dContext, writeFactory, textFormatAccountCreds.Get(), d2dFactory);
	createAccount_inputGroup = std::make_unique<UIInputGroup>(CreateAccount);
	createAccount_inputGroup->AddInput(createAccount_accountNameInput.get());
	createAccount_inputGroup->AddInput(createAccount_passwordInput.get());

	// CreateCharacter
	createCharacter_characterNameInput = std::make_unique<UIInput>(uiComponents, XMFLOAT3{ 15.0f, 20.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CreateCharacter, false, 140.0f, 260.0f, 24.0f, "Character Name:", blackBrush.Get(), whiteBrush.Get(), grayBrush.Get(), blackBrush.Get(), textFormatAccountCredsInputValue.Get(), d2dContext, writeFactory, textFormatAccountCreds.Get(), d2dFactory);
	createCharacter_inputGroup = std::make_unique<UIInputGroup>(CreateCharacter);
	createCharacter_inputGroup->AddInput(createCharacter_characterNameInput.get());
}

void Game::InitializeButtons()
{
	auto writeFactory = m_deviceResources->GetWriteFactory();
	auto d2dFactory = m_deviceResources->GetD2DFactory();
	auto d2dContext = m_deviceResources->GetD2DDeviceContext();
	
	const auto onClickLoginButton = [this]()
	{
		loginScreen_successMessageLabel->SetText("");
		loginScreen_errorMessageLabel->SetText("");

		const auto accountName = Utility::ws2s(std::wstring(loginScreen_accountNameInput->GetInputValue()));
		const auto password = Utility::ws2s(std::wstring(loginScreen_passwordInput->GetInputValue()));

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

		g_socketManager.SendPacket(OPCODE_CONNECT, 2, accountName, password);
		SetActiveLayer(Connecting);
	};

	const auto onClickLoginScreenCreateAccountButton = [this]()
	{
		loginScreen_successMessageLabel->SetText("");
		loginScreen_errorMessageLabel->SetText("");
		SetActiveLayer(CreateAccount);
	};

	// LoginScreen
	loginScreen_loginButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 145.0f, 96.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, Login, 80.0f, 24.0f, "LOGIN", onClickLoginButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);
	loginScreen_createAccountButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 15.0f, 522.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, Login, 160.0f, 24.0f, "CREATE ACCOUNT", onClickLoginScreenCreateAccountButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);

	const auto onClickCreateAccountCreateAccountButton = [this]()
	{
		createAccount_errorMessageLabel->SetText("");

		const auto accountName = Utility::ws2s(std::wstring(createAccount_accountNameInput->GetInputValue()));
		const auto password = Utility::ws2s(std::wstring(createAccount_passwordInput->GetInputValue()));

		if (accountName.length() == 0)
		{
			createAccount_errorMessageLabel->SetText("Username field can't be empty.");
			return;
		}
		if (password.length() == 0)
		{
			createAccount_errorMessageLabel->SetText("Password field can't be empty.");
			return;
		}

		g_socketManager.SendPacket(OPCODE_CREATE_ACCOUNT, 2, accountName, password);
	};

	const auto onClickCreateAccountCancelButton = [this]()
	{
		createAccount_errorMessageLabel->SetText("");
		SetActiveLayer(Login);
	};

	// CreateAccount
	createAccount_createAccountButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 145.0f, 96.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CreateAccount, 80.0f, 24.0f, "CREATE", onClickCreateAccountCreateAccountButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);
	createAccount_cancelButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 15.0f, 522.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CreateAccount, 80.0f, 24.0f, "CANCEL", onClickCreateAccountCancelButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);

	const auto onClickCharacterSelectNewCharacterButton = [this]()
	{
		characterSelect_successMessageLabel->SetText("");

		if (m_characterList.size() == 5)
			characterSelect_errorMessageLabel->SetText("You can not have more than 5 characters.");
		else
			SetActiveLayer(CreateCharacter);
	};

	const auto onClickCharacterSelectEnterWorldButton = [this]()
	{
		characterSelect_successMessageLabel->SetText("");
		characterSelect_errorMessageLabel->SetText("");

		auto characterInput = GetCurrentlySelectedCharacterListing();
		if (characterInput == nullptr)
			characterSelect_errorMessageLabel->SetText("You must select a character before entering the game.");
		else
		{
			g_socketManager.SendPacket(OPCODE_ENTER_WORLD, 1, characterInput->GetName());
			SetActiveLayer(EnteringWorld);
		}
	};

	const auto onClickCharacterSelectDeleteCharacterButton = [this]()
	{
		characterSelect_successMessageLabel->SetText("");
		characterSelect_errorMessageLabel->SetText("");

		auto characterInput = GetCurrentlySelectedCharacterListing();
		if (characterInput == nullptr)
			characterSelect_errorMessageLabel->SetText("You must select a character to delete.");
		else
		{
			m_characterNamePendingDeletion = characterInput->GetName();
			SetActiveLayer(DeleteCharacter);
		}
	};

	const auto onClickCharacterSelectLogoutButton = [this]()
	{
		SetActiveLayer(Login);
	};

	// CharacterSelect
	characterSelect_newCharacterButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 15.0f, 20.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CharacterSelect, 140.0f, 24.0f, "NEW CHARACTER", onClickCharacterSelectNewCharacterButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);
	characterSelect_enterWorldButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 170.0f, 20.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CharacterSelect, 120.0f, 24.0f, "ENTER WORLD", onClickCharacterSelectEnterWorldButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);
	characterSelect_deleteCharacterButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 305.0f, 20.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CharacterSelect, 160.0f, 24.0f, "DELETE CHARACTER", onClickCharacterSelectDeleteCharacterButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);
	characterSelect_logoutButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 15.0f, 522.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CharacterSelect, 80.0f, 24.0f, "LOGOUT", onClickCharacterSelectLogoutButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext,  writeFactory, textFormatButtonText.Get(), d2dFactory);

	const auto onClickCreateCharacterCreateCharacterButton = [this]()
	{
		createCharacter_errorMessageLabel->SetText("");

		const auto characterName = Utility::ws2s(std::wstring(createCharacter_characterNameInput->GetInputValue()));

		if (characterName.length() == 0)
		{
			createCharacter_errorMessageLabel->SetText("Character name can't be empty.");
			return;
		}

		g_socketManager.SendPacket(OPCODE_CREATE_CHARACTER, 1, characterName);
	};

	const auto onClickCreateCharacterBackButton = [this]()
	{
		createCharacter_errorMessageLabel->SetText("");
		SetActiveLayer(CharacterSelect);
	};

	// CreateCharacter
	createCharacter_createCharacterButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 165.0f, 64.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CreateCharacter, 160.0f, 24.0f, "CREATE CHARACTER", onClickCreateCharacterCreateCharacterButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);
	createCharacter_backButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 15.0f, 522.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CreateCharacter, 80.0f, 24.0f, "BACK", onClickCreateCharacterBackButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);

	// DeleteCharacter

	const auto onClickDeleteCharacterConfirm = [this]()
	{
		g_socketManager.SendPacket(OPCODE_DELETE_CHARACTER, 1, m_characterNamePendingDeletion);
	};

	const auto onClickDeleteCharacterCancel = [this]()
	{
		m_characterNamePendingDeletion = "";
		SetActiveLayer(CharacterSelect);
	};

	deleteCharacter_confirmButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 10.0f, 30.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, DeleteCharacter, 100.0f, 24.0f, "CONFIRM", onClickDeleteCharacterConfirm, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext,  writeFactory, textFormatButtonText.Get(), d2dFactory);
	deleteCharacter_cancelButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 120.0f, 30.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, DeleteCharacter, 100.0f, 24.0f, "CANCEL", onClickDeleteCharacterCancel, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);
}

void Game::InitializeLabels()
{
	auto writeFactory = m_deviceResources->GetWriteFactory();
	auto d2dFactory = m_deviceResources->GetD2DFactory();
	auto d2dContext = m_deviceResources->GetD2DDeviceContext();

	loginScreen_successMessageLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 30.0f, 170.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, Login, 400.0f, successMessageBrush.Get(), textFormatSuccessMessage.Get(), d2dContext, writeFactory, d2dFactory);
	loginScreen_errorMessageLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 30.0f, 170.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, Login, 400.0f, errorMessageBrush.Get(), textFormatErrorMessage.Get(), d2dContext, writeFactory, d2dFactory);

	createAccount_errorMessageLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 30.0f, 170.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CreateAccount, 400.0f, errorMessageBrush.Get(), textFormatErrorMessage.Get(), d2dContext, writeFactory, d2dFactory);

	connecting_statusLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 15.0f, 20.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, Connecting, 400.0f, blackBrush.Get(), textFormatAccountCreds.Get(), d2dContext, writeFactory, d2dFactory);
	connecting_statusLabel->SetText("Connecting...");

	characterSelect_successMessageLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 30.0f, 400.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CharacterSelect, 400.0f, successMessageBrush.Get(), textFormatSuccessMessage.Get(), d2dContext, writeFactory, d2dFactory);
	characterSelect_errorMessageLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 30.0f, 400.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CharacterSelect, 400.0f, errorMessageBrush.Get(), textFormatErrorMessage.Get(), d2dContext, writeFactory, d2dFactory);
	characterSelect_headerLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 15.0f, 60.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CharacterSelect, 200.0f, blackBrush.Get(), textFormatHeaders.Get(), d2dContext, writeFactory, d2dFactory);
	characterSelect_headerLabel->SetText("Character List:");

	createCharacter_errorMessageLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 30.0f, 170.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CreateCharacter, 400.0f, errorMessageBrush.Get(), textFormatErrorMessage.Get(), d2dContext, writeFactory, d2dFactory);

	deleteCharacter_headerLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 10.0f, 10.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, DeleteCharacter, 400.0f, errorMessageBrush.Get(), textFormatErrorMessage.Get(), d2dContext, writeFactory, d2dFactory);
	deleteCharacter_headerLabel->SetText("Are you sure you want to delete this character?");

	enteringWorld_statusLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 5.0f, 20.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, EnteringWorld, 400.0f, blackBrush.Get(), textFormatAccountCreds.Get(), d2dContext, writeFactory, d2dFactory);
	enteringWorld_statusLabel->SetText("Entering World...");
}

void Game::InitializePanels()
{
	auto writeFactory = m_deviceResources->GetWriteFactory();
	auto d2dFactory = m_deviceResources->GetD2DFactory();
	auto d2dContext = m_deviceResources->GetD2DDeviceContext();

	// Game Settings
	const auto gameSettingsPanelX = (m_clientWidth - 400.0f) / 2.0f;
	const auto gameSettingsPanelY = (m_clientHeight - 200.0f) / 2.0f;
	gameSettingsPanelHeader = std::make_unique<UILabel>(uiComponents, XMFLOAT3{2.0f, 2.0f, 0.0f}, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, 200.0f, blackBrush.Get(), textFormatHeaders.Get(), d2dContext, writeFactory, d2dFactory);
	gameSettingsPanelHeader->SetText("Game Settings");
	const auto onClickGameSettingsLogoutButton = [this]()
	{
		g_socketManager.SendPacket(OPCODE_DISCONNECT);
		SetActiveLayer(Login);
	};
	gameSettings_logoutButton = std::make_unique<UIButton>(uiComponents, XMFLOAT3{ 10.0f, 26.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, 80.0f, 24.0f, "LOGOUT", onClickGameSettingsLogoutButton, blueBrush.Get(), darkBlueBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatButtonText.Get(), d2dFactory);
	gameSettingsPanel = std::make_unique<UIPanel>(uiComponents, XMFLOAT3{ gameSettingsPanelX, gameSettingsPanelY, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, false, 400.0f, 200.0f, VK_ESCAPE, darkBlueBrush.Get(), whiteBrush.Get(), grayBrush.Get(), d2dContext, d2dFactory);
	gameSettingsPanel->AddChildComponent(*gameSettingsPanelHeader);
	gameSettingsPanel->AddChildComponent(*gameSettings_logoutButton);

	// Game Editor
	const auto gameEditorPanelX = 580.0f;
	const auto gameEditorPanelY = 5.0f;
	gameEditorPanelHeader = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 2.0f, 2.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, 200.0f, blackBrush.Get(), textFormatHeaders.Get(), d2dContext, writeFactory, d2dFactory);
	gameEditorPanelHeader->SetText("Game Editor");
	gameEditorPanel = std::make_unique<UIPanel>(uiComponents, XMFLOAT3{ gameEditorPanelX, gameEditorPanelY, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, true, 200.0f, 400.0f, VK_F1, darkBlueBrush.Get(), whiteBrush.Get(), grayBrush.Get(), d2dContext, d2dFactory);
	gameEditorPanel->AddChildComponent(*gameEditorPanelHeader);

	// Diagnostics
	const auto diagnosticsPanelX = 580.0f;
	const auto diagnosticsPanelY = 336.0f;
	diagnosticsPanel = std::make_unique<UIPanel>(uiComponents, XMFLOAT3{ diagnosticsPanelX, diagnosticsPanelY, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, true, 200.0f, 200.0f, VK_F2, darkBlueBrush.Get(), whiteBrush.Get(), grayBrush.Get(), d2dContext, d2dFactory);

	diagnosticsPanelHeader = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 2.0f, 2.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, 280.0f, blackBrush.Get(), textFormatHeaders.Get(), d2dContext, writeFactory, d2dFactory);
	diagnosticsPanelHeader->SetText("Diagnostics");
	diagnosticsPanel->AddChildComponent(*diagnosticsPanelHeader);

	mousePosLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 2.0f, 22.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, 280.0f, blackBrush.Get(), textFormatFPS.Get(), d2dContext, writeFactory, d2dFactory);
	diagnosticsPanel->AddChildComponent(*mousePosLabel);

	fpsTextLabel = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 2.0f, 36.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, 280.0f, blackBrush.Get(), textFormatFPS.Get(), d2dContext, writeFactory, d2dFactory);
	diagnosticsPanel->AddChildComponent(*fpsTextLabel);

	// Skills
	const auto skillsPanelX = 200.0f;
	const auto skillsPanelY = 200.0f;
	skillsPanel = std::make_unique<UIPanel>(uiComponents, XMFLOAT3{ skillsPanelX, skillsPanelY, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, true, 200.0f, 200.0f, VK_F3, darkBlueBrush.Get(), whiteBrush.Get(), grayBrush.Get(), d2dContext, d2dFactory);

	skillsPanelHeader = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 2.0f, 2.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, 280.0f, blackBrush.Get(), textFormatHeaders.Get(), d2dContext, writeFactory, d2dFactory);
	skillsPanelHeader->SetText("Skills");
	skillsPanel->AddChildComponent(*skillsPanelHeader);

	// Abilities
	const auto abilitiesPanelX = 300.0f;
	const auto abilitiesPanelY = 300.0f;
	abilitiesPanel = std::make_unique<UIPanel>(uiComponents, XMFLOAT3{ abilitiesPanelX, abilitiesPanelY, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, true, 300.0f, 300.0f, VK_F4, darkBlueBrush.Get(), whiteBrush.Get(), grayBrush.Get(), d2dContext, d2dFactory);

	abilitiesPanelHeader = std::make_unique<UILabel>(uiComponents, XMFLOAT3{ 2.0f, 2.0f, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, 280.0f, blackBrush.Get(), textFormatHeaders.Get(), d2dContext, writeFactory, d2dFactory);
	abilitiesPanelHeader->SetText("Abilities");
	abilitiesPanel->AddChildComponent(*abilitiesPanelHeader);
}

UICharacterListing* Game::GetCurrentlySelectedCharacterListing()
{
	UICharacterListing* characterListing = nullptr;
	for (auto i = 0; i < m_characterList.size(); i++)
	{
		if (m_characterList.at(i)->IsSelected())
			characterListing = m_characterList.at(i).get();
	}
	return characterListing;
}

void Game::InitializeShaders()
{
	auto d3dDevice = m_deviceResources->GetD3DDevice();

	vertexShaderBuffer = LoadShader(L"VertexShader.cso");
	d3dDevice->CreateVertexShader(vertexShaderBuffer.buffer, vertexShaderBuffer.size, nullptr, vertexShader.ReleaseAndGetAddressOf());

	pixelShaderBuffer = LoadShader(L"PixelShader.cso");
	d3dDevice->CreatePixelShader(pixelShaderBuffer.buffer, pixelShaderBuffer.size, nullptr, pixelShader.ReleaseAndGetAddressOf());

	spriteVertexShaderBuffer = LoadShader(L"SpriteVertexShader.cso");
	d3dDevice->CreateVertexShader(spriteVertexShaderBuffer.buffer, spriteVertexShaderBuffer.size, nullptr, spriteVertexShader.ReleaseAndGetAddressOf());

	spritePixelShaderBuffer = LoadShader(L"SpritePixelShader.cso");
	d3dDevice->CreatePixelShader(spritePixelShaderBuffer.buffer, spritePixelShaderBuffer.size, nullptr, spritePixelShader.ReleaseAndGetAddressOf());

	m_projectionTransform = XMMatrixOrthographicLH((float)m_clientWidth, (float)m_clientHeight, 0.1f, 5000.0f);
}

void Game::InitializeBuffers()
{
	auto d3dDevice = m_deviceResources->GetD3DDevice();
	auto d3dContext = m_deviceResources->GetD3DDeviceContext();

	// create constant buffer
	D3D11_BUFFER_DESC bufferDesc;
	ZeroMemory(&bufferDesc, sizeof(bufferDesc));
	bufferDesc.MiscFlags = 0;
	bufferDesc.Usage = D3D11_USAGE_DYNAMIC;
	bufferDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	bufferDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	bufferDesc.ByteWidth = sizeof(ConstantBufferOnce);

	ID3D11Buffer* constantBufferOnce = nullptr;
	d3dDevice->CreateBuffer(&bufferDesc, nullptr, &constantBufferOnce);

	// map ConstantBuffer
	D3D11_MAPPED_SUBRESOURCE mappedResource;
	d3dContext->Map(constantBufferOnce, 0, D3D11_MAP_WRITE_DISCARD, 0, &mappedResource);
	auto pCB = reinterpret_cast<ConstantBufferOnce*>(mappedResource.pData);
	XMStoreFloat4(&pCB->directionalLight, XMVECTOR{ 0.0f, -1.0f, 0.5f, 0.0f });
	d3dContext->Unmap(constantBufferOnce, 0);

	d3dContext->PSSetConstantBuffers(0, 1, &constantBufferOnce);
}

void Game::InitializeRasterStates()
{
	auto d3dDevice = m_deviceResources->GetD3DDevice();

	CD3D11_RASTERIZER_DESC wireframeRasterStateDesc(D3D11_FILL_WIREFRAME, D3D11_CULL_BACK, FALSE,
		D3D11_DEFAULT_DEPTH_BIAS, D3D11_DEFAULT_DEPTH_BIAS_CLAMP,
		D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, TRUE, FALSE);
	d3dDevice->CreateRasterizerState(&wireframeRasterStateDesc, wireframeRasterState.ReleaseAndGetAddressOf());

	CD3D11_RASTERIZER_DESC solidRasterStateDesc(D3D11_FILL_SOLID, D3D11_CULL_BACK, FALSE,
		D3D11_DEFAULT_DEPTH_BIAS, D3D11_DEFAULT_DEPTH_BIAS_CLAMP,
		D3D11_DEFAULT_SLOPE_SCALED_DEPTH_BIAS, TRUE, FALSE, TRUE, FALSE);
	d3dDevice->CreateRasterizerState(&solidRasterStateDesc, solidRasterState.ReleaseAndGetAddressOf());
}

void Game::InitializeTextures()
{
	auto d3dDevice = m_deviceResources->GetD3DDevice();

	const wchar_t* paths[] =
	{
		L"../../WrenClient/Textures/texture01.dds",     // 0
		L"../../WrenClient/Textures/texture02.dds",     // 1
		L"../../WrenClient/Textures/grass01.dds",       // 2
		L"../../WrenClient/Textures/abilityicon01.dds"  // 3
	};

	// clear calls the destructor of its elements, and ComPtr's destructor handles calling Release()
	textures.clear();

	for (auto i = 0; i < 4; i++)
	{
		ComPtr<ID3D11ShaderResourceView> ptr;
		CreateDDSTextureFromFile(d3dDevice, paths[i], nullptr, ptr.ReleaseAndGetAddressOf());
		textures.push_back(ptr);
	}
}

void Game::InitializeMeshes()
{
	auto d3dDevice = m_deviceResources->GetD3DDevice();

	std::string paths[] = 
	{
		"../../WrenClient/Models/sphere.blend",  // 0
		"../../WrenClient/Models/tree.blend",    // 1
		"../../WrenClient/Models/dummy.blend"    // 2
	};

	// clear calls the destructor of its elements, and unique_ptr's destructor handles cleaning itself up
	meshes.clear();

	for (auto i = 0; i < sizeof(paths) / sizeof(std::string); i++)
		meshes.push_back(std::make_unique<Mesh>(paths[i], d3dDevice, vertexShaderBuffer.buffer, vertexShaderBuffer.size));
}

void Game::InitializeSprites()
{
	auto d3dDevice = m_deviceResources->GetD3DDevice();
	testSprite = std::make_unique<Sprite>(spriteVertexShader.Get(), spritePixelShader.Get(), textures[3].Get(), spriteVertexShaderBuffer.buffer, spriteVertexShaderBuffer.size, d3dDevice, (m_clientWidth / -2) + 25, (m_clientHeight / -2) + 25, 38.0f, 38.0f);
}

void Game::RecreateCharacterListings(const std::vector<std::string*>* characterNames)
{
	auto writeFactory = m_deviceResources->GetWriteFactory();
	auto d2dFactory = m_deviceResources->GetD2DFactory();
	auto d2dContext = m_deviceResources->GetD2DDeviceContext();

	m_characterList.clear();

	for (auto i = 0; i < characterNames->size(); i++)
	{
		const float y = 100.0f + (i * 40.0f);
		m_characterList.push_back(std::make_unique<UICharacterListing>(uiComponents, XMFLOAT3{ 25.0f, y, 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, CharacterSelect, 260.0f, 30.0f, (*characterNames->at(i)).c_str(), whiteBrush.Get(), selectedCharacterBrush.Get(), grayBrush.Get(), blackBrush.Get(), d2dContext, writeFactory, textFormatAccountCredsInputValue.Get(), d2dFactory));
	}
}

const bool Game::HandleEvent(const Event* const event)
{
	const auto type = event->type;
	switch (type)
	{
		case EventType::KeyDownEvent:
		{
			const auto derivedEvent = (KeyDownEvent*)event;

			break;
		}
		case EventType::MouseMoveEvent:
		{
			const auto derivedEvent = (MouseEvent*)event;

			m_mousePosX = derivedEvent->mousePosX;
			m_mousePosY = derivedEvent->mousePosY;

			break;
		}
		case EventType::CreateAccountFailed:
		{
			const auto derivedEvent = (CreateAccountFailedEvent*)event;

			createAccount_errorMessageLabel->SetText(("Failed to create account. Reason: " + *(derivedEvent->error)).c_str());

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
			const auto derivedEvent = (LoginFailedEvent*)event;

			loginScreen_errorMessageLabel->SetText(("Login failed. Reason: " + *(derivedEvent->error)).c_str());
			SetActiveLayer(Login);

			break;
		}
		case EventType::LoginSuccess:
		{
			const auto derivedEvent = (LoginSuccessEvent*)event;

			RecreateCharacterListings(derivedEvent->characterList);
			SetActiveLayer(CharacterSelect);

			break;
		}
		case EventType::CreateCharacterFailed:
		{
			const auto derivedEvent = (CreateCharacterFailedEvent*)event;

			createCharacter_errorMessageLabel->SetText(("Character creation failed. Reason: " + *(derivedEvent->error)).c_str());

			break;
		}
		case EventType::CreateCharacterSuccess:
		{
			const auto derivedEvent = (CreateCharacterSuccessEvent*)event;

			RecreateCharacterListings(derivedEvent->characterList);
			createCharacter_errorMessageLabel->SetText("");
			characterSelect_successMessageLabel->SetText("Character created successfully.");
			SetActiveLayer(CharacterSelect);

			break;
		}
		case EventType::DeleteCharacterSuccess:
		{
			const auto derivedEvent = (DeleteCharacterSuccessEvent*)event;

			RecreateCharacterListings(derivedEvent->characterList);
			m_characterNamePendingDeletion = "";
			createCharacter_errorMessageLabel->SetText("");
			characterSelect_successMessageLabel->SetText("Character deleted successfully.");
			SetActiveLayer(CharacterSelect);

			break;
		}
		case EventType::EnterWorldSuccess:
		{
			const auto derivedEvent = (EnterWorldSuccessEvent*)event;

			GameObject& player = m_objectManager.CreateGameObject(derivedEvent->position, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, (long)derivedEvent->characterId);
			m_player = &player;
			auto sphereRenderComponent = m_renderComponentManager.CreateRenderComponent(player.GetId(), meshes[derivedEvent->modelId].get(), vertexShader.Get(), pixelShader.Get(), textures[derivedEvent->textureId].Get());
			player.SetRenderComponentId(sphereRenderComponent.GetId());
			m_playerController = std::make_unique<PlayerController>(player);

			skills = derivedEvent->skills;
			auto d2dDeviceContext = m_deviceResources->GetD2DDeviceContext();
			auto writeFactory = m_deviceResources->GetWriteFactory();
			auto xOffset = 5.0f;
			auto yOffset = 25.0f;
			for (auto i = 0; i < derivedEvent->skills->size(); i++)
			{
				auto skill = derivedEvent->skills->at(i);
				m_skillList[skill->skillId] = std::make_unique<UISkillListing>(uiComponents, XMFLOAT3{ xOffset, yOffset + (18.0f * i), 0.0f }, XMFLOAT3{ 0.0f, 0.0f, 0.0f }, InGame, *skill, blackBrush.Get(), d2dDeviceContext, writeFactory, textFormatFPS.Get());
				skillsPanel->AddChildComponent(*m_skillList[skill->skillId]);
			}

			SetActiveLayer(InGame);

			break;
		}
		case EventType::GameObjectUpdate:
		{
			const auto derivedEvent = (GameObjectUpdateEvent*)event;

			const auto gameObjectId = derivedEvent->characterId;
			const auto pos = XMFLOAT3{ derivedEvent->posX, derivedEvent->posY, derivedEvent->posZ };
			const auto mov = XMFLOAT3{ derivedEvent->movX, derivedEvent->movY, derivedEvent->movZ };
			const auto modelId = derivedEvent->modelId;
			const auto textureId = derivedEvent->textureId;

			if (!m_objectManager.GameObjectExists(gameObjectId))
			{
				GameObject& obj = m_objectManager.CreateGameObject(pos, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, gameObjectId);
				obj.SetMovementVector(mov);
				auto sphereRenderComponent = m_renderComponentManager.CreateRenderComponent(gameObjectId, meshes[modelId].get(), vertexShader.Get(), pixelShader.Get(), textures[textureId].Get());
				obj.SetRenderComponentId(sphereRenderComponent.GetId());
			}
			else
			{
				GameObject& gameObject = m_objectManager.GetGameObjectById(derivedEvent->characterId);
				gameObject.SetLocalPosition(pos);
				gameObject.SetMovementVector(mov);
			}

			break;
		}
	}

	return false;
}

void Game::SetActiveLayer(const Layer layer)
{
	m_activeLayer = layer;
	g_eventHandler.QueueEvent(new ChangeActiveLayerEvent{ layer });
}

void Game::SyncWithServer(const float deltaTime)
{
	if (g_socketManager.Connected())
	{
		const auto position = m_player->GetWorldPosition();
		const auto movementVector = m_player->GetMovementVector();

		playerUpdates[m_playerUpdateIdCounter % BUFFER_SIZE] = std::make_unique<PlayerUpdate>(m_playerUpdateIdCounter, position, movementVector, deltaTime);

		g_socketManager.SendPacket(
			OPCODE_PLAYER_UPDATE,
			9,
			std::to_string(m_playerUpdateIdCounter),
			std::to_string(m_player->GetId()),
			std::to_string(position.x),
			std::to_string(position.y),
			std::to_string(position.z),
			std::to_string(movementVector.x),
			std::to_string(movementVector.y),
			std::to_string(movementVector.z),
			std::to_string(deltaTime));

		m_playerUpdateIdCounter++;
	}
}

Game::~Game()
{
	g_eventHandler.Unsubscribe(*this);
}