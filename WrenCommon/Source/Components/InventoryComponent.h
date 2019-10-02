#pragma once

#include "GameObject.h"

class InventoryComponent
{
	int id{ 0 };
	int gameObjectId{ 0 };

	void Initialize(const int id, const int gameObjectId);

	friend class InventoryComponentManager;
public:
	static const int inventorySize{ 12 };
	int itemIds[inventorySize] = { -1 };

	const int GetId() const;
	const int GetGameObjectId() const;
};