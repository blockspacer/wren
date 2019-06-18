#pragma once

#include <string>
#include "Event.h"
#include "Components/StatsComponent.h"

class SetTargetEvent : public Event
{
public:
	SetTargetEvent(const long gameObjectId, StatsComponent* statsComponent, const char* name)
		: Event(EventType::SetTarget),
		  gameObjectId{ gameObjectId },
		  statsComponent{ statsComponent },
		  name{ name }
	{
	}
	~SetTargetEvent() { delete name; }
	const long gameObjectId;
	StatsComponent* statsComponent;
	const char* name;
};