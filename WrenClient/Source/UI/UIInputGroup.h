#pragma once

#include "UIInput.h"
#include "EventHandling/EventHandler.h"
#include "EventHandling/Observer.h"

class UIInputGroup : public Observer
{
	EventHandler& eventHandler;
	bool active{ false };
	const Layer uiLayer{ Login };
	std::vector<UIInput*> inputs;
public:
	UIInputGroup(const Layer uiLayer, EventHandler& eventHandler);
	virtual const bool HandleEvent(const Event* const event);
	void AddInput(UIInput* input) { inputs.push_back(input); }
	~UIInputGroup();
};