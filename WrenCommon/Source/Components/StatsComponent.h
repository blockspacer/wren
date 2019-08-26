#pragma once

class StatsComponent
{
	unsigned int id{ 0 };
	unsigned int gameObjectId{ 0 };

	void Initialize(
		const unsigned int id, const long gameObjectId,
		const int agility, const int strength, const int wisdom, const int intelligence, const int charisma, const int luck, const int endurance,
		const int health, const int maxHealth, const int mana, const int maxMana, const int stamina, const int maxStamina);

	friend class StatsComponentManager;
public:
	int agility{ 0 };
	int strength{ 0 };
	int wisdom{ 0 };
	int intelligence{ 0 };
	int charisma{ 0 };
	int luck{ 0 };
	int endurance{ 0 };
	int health{ 0 };
	int maxHealth{ 0 };
	int mana{ 0 };
	int maxMana{ 0 };
	int stamina{ 0 };
	int maxStamina{ 0 };

	const unsigned int GetId() const;
	const unsigned int GetGameObjectId() const;
};