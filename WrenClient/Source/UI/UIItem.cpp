#include "stdafx.h"
#include "UIItem.h"
#include "EventHandling/Events/ChangeActiveLayerEvent.h"
#include "EventHandling/Events/StartDraggingUIItemEvent.h"
#include "Events/UIItemDroppedEvent.h"
#include "UILootContainer.h"

extern float g_clientWidth;
extern float g_clientHeight;
extern XMMATRIX g_projectionTransform;
extern bool g_mouseIsDragging;
extern unsigned int g_zIndex;
extern float g_mousePosX;
extern float g_mousePosY;

UIItem::UIItem(
	UIComponentArgs uiComponentArgs,
	EventHandler& eventHandler,
	ClientSocketManager& socketManager,
	const int itemId,
	const std::string& name,
	const std::string& description,
	const bool isDragging,
	const float mousePosX,
	const float mousePosY)
	: UIComponent(uiComponentArgs),
	  eventHandler{ eventHandler },
	  socketManager{ socketManager },
	  itemId{ itemId },
	  name{ name },
	  description{ description },
	  isDragging{ isDragging },
	  lastDragX{ mousePosX },
	  lastDragY{ mousePosY }
{
}

void UIItem::Initialize(
	ID3D11VertexShader* vertexShader,
	ID3D11PixelShader* pixelShader,
	ID3D11ShaderResourceView* texture,
	ID3D11ShaderResourceView* grayTexture,
	ID2D1SolidColorBrush* highlightBrush,
	const BYTE* vertexShaderBuffer,
	const int vertexShaderSize,
	ID2D1SolidColorBrush* bodyBrush,
	ID2D1SolidColorBrush* borderBrush,
	ID2D1SolidColorBrush* textBrush,
	IDWriteTextFormat* textFormatTitle,
	IDWriteTextFormat* textFormatDescription
)
{
	this->vertexShader = vertexShader;
	this->pixelShader = pixelShader;
	this->texture = texture;
	this->grayTexture = grayTexture;
	this->highlightBrush = highlightBrush;
	this->vertexShaderBuffer = vertexShaderBuffer;
	this->vertexShaderSize = vertexShaderSize;
	this->bodyBrush = bodyBrush;
	this->borderBrush = borderBrush;
	this->textBrush = textBrush;
	this->textFormatTitle = textFormatTitle;
	this->textFormatDescription = textFormatDescription;

	tooltip = std::make_unique<UITooltip>(UIComponentArgs{ deviceResources, uiComponents, [](const float, const float) { return XMFLOAT2{ -3.0f, 42.0f }; }, uiLayer, 9999 }, eventHandler, name, description);
	AddChildComponent(*tooltip.get());
	tooltip->Initialize(bodyBrush, borderBrush, textBrush, textFormatTitle, textFormatDescription);
}

void UIItem::Draw()
{
	if (!isVisible) return;

	const auto d2dDeviceContext = deviceResources->GetD2DDeviceContext();

	d2dDeviceContext->EndDraw();

	if (itemCopy)
		graySprite->Draw(deviceResources->GetD3DDeviceContext());
	else
		sprite->Draw(deviceResources->GetD3DDeviceContext());

	d2dDeviceContext->BeginDraw();

	if (itemCopy)
		itemCopy->Draw();
}

const bool UIItem::HandleEvent(const Event* const event)
{
	// first pass the event to UIComponent base so it can reset localPosition based on new client dimensions
	UIComponent::HandleEvent(event);

	const auto type = event->type;
	switch (type)
	{
		case EventType::ChangeActiveLayer:
		{
			const auto derivedEvent = (ChangeActiveLayerEvent*)event;

			if (derivedEvent->layer == uiLayer && GetParent() == nullptr)
				isVisible = true;
			else
				isVisible = false;

			break;
		}
		case EventType::MouseMove:
		{
			if (!isVisible)
				return false;

			const auto derivedEvent = (MouseEvent*)event;
			const auto mousePosX = derivedEvent->mousePosX;
			const auto mousePosY = derivedEvent->mousePosY;

			const auto worldPos = GetWorldPosition();

			// if the button is pressed, and the mouse starts moving, let's move the UIItem
			if (isPressed && !isDragging && !itemCopy && GetParent()->GetUIItemDragBehavior() == "MOVE")
			{
				itemCopy = std::make_unique<UIItem>(UIComponentArgs{ deviceResources, uiComponents, calculatePosition, uiLayer, g_zIndex++ }, eventHandler, socketManager, itemId, name, description, true, mousePosX, mousePosY);
				itemCopy->Initialize(vertexShader, pixelShader, texture, grayTexture, highlightBrush, vertexShaderBuffer, vertexShaderSize, bodyBrush, borderBrush, textBrush, textFormatTitle, textFormatDescription);
				itemCopy->SetLocalPosition(XMFLOAT2{ worldPos.x, worldPos.y });
				itemCopy->isVisible = true;
				itemCopy->CreatePositionDependentResources();

				const auto parentWorldPos = GetParent()->GetWorldPosition();
				const auto slot = Utility::GetInventoryIndex(parentWorldPos.x, parentWorldPos.y, worldPos.x + 16.0f, worldPos.y + 16.0f);
				std::unique_ptr<Event> e = std::make_unique<StartDraggingUIItemEvent>(slot);
				eventHandler.QueueEvent(e);
			}

			if (isDragging)
			{
				const auto deltaX = mousePosX - lastDragX;
				const auto deltaY = mousePosY - lastDragY;

				Translate(XMFLOAT2(deltaX, deltaY));

				lastDragX = mousePosX;
				lastDragY = mousePosY;

				CreatePositionDependentResources();
			}

			if (IsHovered(mousePosX, mousePosY) && !itemCopy && !isDragging && !g_mouseIsDragging)
				tooltip->isVisible = true;
			else
				tooltip->isVisible = false;

			break;
		}
		case EventType::LeftMouseDown:
		{
			const auto derivedEvent = (MouseEvent*)event;

			if (isVisible && IsHovered(derivedEvent->mousePosX, derivedEvent->mousePosY) && !isDragging)
			{
				isPressed = true;
				return true;
			}

			break;
		}
		case EventType::LeftMouseUp:
		{
			const auto derivedEvent = (MouseEvent*)event;

			if (itemCopy)
			{
				std::unique_ptr<Event> e = std::make_unique<UIItemDroppedEvent>(derivedEvent->mousePosX, derivedEvent->mousePosY);
				eventHandler.QueueEvent(e);

				itemCopy.reset();
			}

			isPressed = false;
			isDragging = false;

			break;
		}
		case EventType::UIItemDropped:
		{
			break;
		}
		case EventType::RightMouseDown:
		{
			const auto derivedEvent = (MouseEvent*)event;
			const auto mousePosX = derivedEvent->mousePosX;
			const auto mousePosY = derivedEvent->mousePosY;
			const auto worldPos = GetWorldPosition();

			if (Utility::DetectClick(worldPos.x, worldPos.y, worldPos.x + 38.0f, worldPos.y + 38.0f, mousePosX, mousePosY))
			{
				const auto parent = GetParent();
				if (isVisible && parent->GetUIItemRightClickBehavior() == "LOOT")
				{
					const auto uiLootContainer = (UILootContainer*)parent;
					for (auto i = 0; i < uiLootContainer->uiItems.size(); i++)
					{
						if (uiLootContainer->uiItems[i].get() == this)
						{
							std::vector<std::string> args{ std::to_string(uiLootContainer->currentGameObjectId), std::to_string(i) };
							socketManager.SendPacket(OpCode::LootItem, args);
						}
					}
				}

				return true;
			}

			break;
		}
		case EventType::WindowResize:
		{
			CreatePositionDependentResources();

			break;
		}
		case EventType::ShowPanel:
		{
			if (IsHovered(g_mousePosX, g_mousePosY))
				tooltip->isVisible = true;
		}
	}

	return false;
}

void UIItem::CreatePositionDependentResources()
{
	const auto worldPos = GetWorldPosition();

	// create highlight
	deviceResources->GetD2DFactory()->CreateRectangleGeometry(D2D1::RectF(worldPos.x, worldPos.y, worldPos.x + SPRITE_SIZE, worldPos.y + SPRITE_SIZE), highlightGeometry.ReleaseAndGetAddressOf());

	XMFLOAT3 pos{ worldPos.x + 18.0f, worldPos.y + 18.0f, 0.0f };
	FXMVECTOR v = XMLoadFloat3(&pos);
	CXMMATRIX view = XMMatrixIdentity();
	CXMMATRIX world = XMMatrixIdentity();

	auto res = XMVector3Unproject(v, 0.0f, 0.0f, g_clientWidth, g_clientHeight, 0.0f, 1000.0f, g_projectionTransform, view, world);
	XMFLOAT3 vec;
	XMStoreFloat3(&vec, res);
	sprite = std::make_shared<Sprite>(vertexShader, pixelShader, texture, vertexShaderBuffer, vertexShaderSize, deviceResources->GetD3DDevice(), vec.x, vec.y, SPRITE_SIZE, SPRITE_SIZE, zIndex);
	graySprite = std::make_shared<Sprite>(vertexShader, pixelShader, grayTexture, vertexShaderBuffer, vertexShaderSize, deviceResources->GetD3DDevice(), vec.x, vec.y, SPRITE_SIZE, SPRITE_SIZE, zIndex);

	tooltip->CreatePositionDependentResources();
}

const int UIItem::GetItemId() const { return itemId; }

const bool UIItem::IsHovered(const float mousePosX, const float mousePosY)
{
	const auto worldPos = GetWorldPosition();
	return Utility::DetectClick(worldPos.x, worldPos.y, worldPos.x + 38.0f, worldPos.y + 38.0f, mousePosX, mousePosY);
}

void UIItem::SetTooltipAsVisible()
{
	tooltip->isVisible = true;
}
