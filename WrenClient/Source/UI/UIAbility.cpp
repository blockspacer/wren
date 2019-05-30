#include "stdafx.h"
#include "UIAbility.h"
#include "Layer.h"
#include "EventHandling/EventHandler.h"
#include "EventHandling/Events/ChangeActiveLayerEvent.h"
#include "EventHandling/Events/MouseEvent.h"
#include "EventHandling/Events/ActivateAbilityEvent.h"
#include "Events/UIAbilityDroppedEvent.h"

extern EventHandler g_eventHandler;

UIAbility::UIAbility(
	std::vector<UIComponent*>& uiComponents,
	const XMFLOAT3 position,
	const XMFLOAT3 scale,
	const Layer uiLayer,
	const int abilityId,
	ID2D1DeviceContext1* d2dDeviceContext,
	ID2D1Factory2* d2dFactory,
	ID3D11Device* d3dDevice,
	ID3D11DeviceContext* d3dDeviceContext,
	ID3D11VertexShader* vertexShader,
	ID3D11PixelShader* pixelShader,
	ID3D11ShaderResourceView* texture,
	ID2D1SolidColorBrush* highlightBrush,
	ID2D1SolidColorBrush* abilityPressedBrush,
	const BYTE* vertexShaderBuffer,
	const int vertexShaderSize,
	const float worldPosX,
	const float worldPosY,
	const float clientWidth,
	const float clientHeight,
	const XMMATRIX projectionTransform,
	const bool isDragging,
	const float mousePosX,
	const float mousePosY)
	: UIComponent(uiComponents, position, scale, uiLayer),
	  abilityId{ abilityId },
	  d2dDeviceContext{ d2dDeviceContext },
	  d2dFactory{ d2dFactory },
	  clientWidth{ clientWidth },
	  clientHeight{ clientHeight },
	  d3dDevice{ d3dDevice },
	  vertexShader{ vertexShader },
	  pixelShader{ pixelShader },
	  texture{ texture },
	  vertexShaderBuffer{ vertexShaderBuffer },
	  vertexShaderSize{ vertexShaderSize },
	  d3dDeviceContext{ d3dDeviceContext },
	  highlightBrush{ highlightBrush },
	  abilityPressedBrush{ abilityPressedBrush },
	  projectionTransform{ projectionTransform },
	  isDragging{ isDragging },
	  lastDragX{ mousePosX },
	  lastDragY{ mousePosY }
{
}

void UIAbility::Draw()
{
	if (!isVisible) return;

	const auto worldPos = GetWorldPosition();

	// create highlight
	d2dFactory->CreateRectangleGeometry(D2D1::RectF(worldPos.x, worldPos.y, worldPos.x + HIGHLIGHT_WIDTH, worldPos.y + HIGHLIGHT_WIDTH), highlightGeometry.ReleaseAndGetAddressOf());

	auto pos = XMFLOAT3{ worldPos.x + 18.0f, worldPos.y + 18.0f, 0.0f };
	FXMVECTOR v = XMLoadFloat3(&pos);
	CXMMATRIX view = XMMatrixIdentity();
	CXMMATRIX world = XMMatrixIdentity();

	auto res = XMVector3Unproject(v, 0.0f, 0.0f, clientWidth, clientHeight, 0.0f, 1000.0f, projectionTransform, view, world);
	XMFLOAT3 vec;
	XMStoreFloat3(&vec, res);
	sprite = std::make_shared<Sprite>(vertexShader, pixelShader, texture, vertexShaderBuffer, vertexShaderSize, d3dDevice, vec.x, vec.y, SPRITE_WIDTH, SPRITE_WIDTH);

	if (isHovered)
	{
		const auto thickness = isPressed ? 3.0f : 2.0f;
		const auto brush = isPressed ? abilityPressedBrush : highlightBrush;
		d2dDeviceContext->DrawGeometry(highlightGeometry.Get(), brush, thickness);
	}

	if (abilityCopy)
		abilityCopy->Draw();
}

const bool UIAbility::HandleEvent(const Event* const event)
{
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
		case EventType::MouseMoveEvent:
		{
			const auto derivedEvent = (MouseEvent*)event;
			const auto mousePosX = derivedEvent->mousePosX;
			const auto mousePosY = derivedEvent->mousePosY;

			if (isVisible)
			{
				const auto worldPos = GetWorldPosition();
				if (Utility::DetectClick(worldPos.x, worldPos.y, worldPos.x + 38.0f, worldPos.y + 38.0f, mousePosX, mousePosY))
					isHovered = true;
				else
					isHovered = false;

				// if the button is pressed, and the mouse starts moving, let's move the UIAbility
				if (isPressed)
				{
					if (!isDragging && !abilityCopy)
					{
						const auto dragBehavior = GetParent()->GetUIAbilityDragBehavior();

						if (dragBehavior == "COPY")
						{
							abilityCopy = new UIAbility(uiComponents, worldPos, scale, uiLayer, abilityId, d2dDeviceContext, d2dFactory, d3dDevice, d3dDeviceContext, vertexShader, pixelShader, texture, highlightBrush, abilityPressedBrush, vertexShaderBuffer, vertexShaderSize, worldPos.x, worldPos.y, clientWidth, clientHeight, projectionTransform, true, mousePosX, mousePosY);
							abilityCopy->SetVisible(true);
						}
						else if (dragBehavior == "MOVE")
						{
							isDragging = true;
							lastDragX = mousePosX;
							lastDragY = mousePosY;
						}
					}
				}

				if (isDragging)
				{
					const auto deltaX = mousePosX - lastDragX;
					const auto deltaY = mousePosY - lastDragY;

					Translate(XMFLOAT3(deltaX, deltaY, 0.0f));

					lastDragX = mousePosX;
					lastDragY = mousePosY;
				}
			}

			break;
		}
		case EventType::LeftMouseDownEvent:
		{
			const auto derivedEvent = (MouseEvent*)event;

			if (isVisible)
			{
				if (isHovered)
					isPressed = true;
			}

			break;
		}
		case EventType::LeftMouseUpEvent:
		{
			const auto derivedEvent = (MouseEvent*)event;

			if (isDragging)
			{
				g_eventHandler.QueueEvent(new UIAbilityDroppedEvent{ this, derivedEvent->mousePosX, derivedEvent->mousePosY });
				isDragging = false;
			}
			
			if (isVisible)
			{
				g_eventHandler.QueueEvent(new ActivateAbilityEvent{ abilityId });
				isPressed = false;
			}

			break;
		}
		case EventType::UIAbilityDroppedEvent:
		{
			abilityCopy = nullptr;

			break;
		}
	}

	return false;
}

void UIAbility::DrawSprite()
{
	if (!isVisible) return;

	sprite->Draw(d3dDeviceContext, projectionTransform);
	if (abilityCopy)
		abilityCopy->DrawSprite();
}