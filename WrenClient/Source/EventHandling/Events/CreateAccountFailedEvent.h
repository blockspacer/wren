#pragma once

#include "Event.h"

class CreateAccountFailedEvent : public Event
{
public:
	CreateAccountFailedEvent(const std::string* error)
		: Event(EventType::CreateAccountFailed),
		  error{ error }
	{
	}
	~CreateAccountFailedEvent() { delete error; }
	const std::string* error;
};