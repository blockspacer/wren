#include "stdafx.h"
#include "AIComponentManager.h"
#include <Components/StatsComponentManager.h>
#include <Components/InventoryComponentManager.h>
#include "../Events/AttackHitEvent.h"
#include "../Events/AttackMissEvent.h"

constexpr XMFLOAT3 DIRECTIONS[8]
{
	VEC_SOUTHWEST,
	VEC_SOUTH,
	VEC_SOUTHEAST,
	VEC_EAST,
	VEC_NORTHEAST,
	VEC_NORTH,
	VEC_NORTHWEST,
	VEC_WEST
};

AIComponentManager::AIComponentManager(EventHandler& eventHandler, ObjectManager& objectManager, GameMap& gameMap, ServerComponentOrchestrator& componentOrchestrator, ServerSocketManager& socketManager)
	: ComponentManager(eventHandler, objectManager),
	  gameMap{ gameMap },
	  componentOrchestrator{ componentOrchestrator },
	  socketManager{ socketManager }
{
}

AIComponent& AIComponentManager::CreateAIComponent(const int gameObjectId)
{
	AIComponent& aiComponent = CreateComponent(gameObjectId);

	// component initialization here

	return aiComponent;
}

void AIComponentManager::Update()
{
	std::random_device dev;
	std::mt19937 rng(dev());
	std::uniform_int_distribution<std::mt19937::result_type> dist100(0, 99);
	std::uniform_int_distribution<std::mt19937::result_type> dist8(0, 7);

	const auto statsComponentManager = componentOrchestrator.GetStatsComponentManager();
	const auto inventoryComponentManager = componentOrchestrator.GetInventoryComponentManager();

	for (auto i = 0; i < componentIndex; i++)
	{
		AIComponent& comp = components[i];
		GameObject& gameObject = objectManager.GetGameObjectById(comp.GetGameObjectId());
		StatsComponent& statsComponent = statsComponentManager->GetComponentById(gameObject.statsComponentId);

		if (!statsComponent.alive)
			continue;

		if (statsComponent.alive && statsComponent.health <= 0)
		{
			statsComponent.alive = false;

			const InventoryComponent& inventoryComponent = inventoryComponentManager->GetComponentById(gameObject.inventoryComponentId);
			
			std::string itemIdString{ "" };
			for (auto i = 0; i < INVENTORY_SIZE; i++)
			{
				const auto itemId = inventoryComponent.itemIds[i];
				if (itemId >= 0)
					itemIdString += std::to_string(itemId) + ";";
			}
			
			std::vector<std::string> args{ std::to_string(gameObject.GetId()), itemIdString };
			socketManager.SendPacketToAllClients(OpCode::NpcDeath, args);
		}

		auto pos = gameObject.GetWorldPosition();

		// handle movement
		if (gameObject.movementVector == VEC_ZERO)
		{
			auto movementVec = VEC_ZERO;

			if (comp.targetId >= 0)
			{
				const GameObject& target = objectManager.GetGameObjectById(comp.targetId);
				
				if (!Utility::AreOnAdjacentOrDiagonalTiles(gameObject.localPosition, target.localPosition))
				{
					const auto npcPosVec = XMLoadFloat3(&pos);
					const auto targetPosVec = XMLoadFloat3(&target.localPosition);

					float shortestDistance{ FLT_MAX };
					for (auto i = 0; i < 8; i++)
					{
						const auto dirVec = XMLoadFloat3(&DIRECTIONS[i]);
						const auto newVec = XMVectorAdd(npcPosVec, dirVec);
						const auto npcToPlayerVec = XMVectorSubtract(newVec, targetPosVec);
						const auto magnitudeVec = XMVector3Length(npcToPlayerVec);

						XMFLOAT3 magnitudeStoredVec;
						XMStoreFloat3(&magnitudeStoredVec, magnitudeVec);

						if (std::abs(magnitudeStoredVec.x) < shortestDistance)
						{
							shortestDistance = magnitudeStoredVec.x;
							movementVec = DIRECTIONS[i];
						}
					}
				}
			}
			else
			{
				auto rnd = dist100(rng);
				if (rnd == 0)
				{
					rnd = dist8(rng);
					movementVec = DIRECTIONS[rnd];
				}
			}

			if (movementVec != VEC_ZERO)
			{
				const auto delta = XMFLOAT3{ movementVec.x * TILE_SIZE, movementVec.y * TILE_SIZE, movementVec.z * TILE_SIZE };
				const auto proposedPos = gameObject.localPosition + delta;

				if (!Utility::CheckOutOfBounds(proposedPos) && !gameMap.IsTileOccupied(proposedPos))
				{
					gameObject.movementVector = movementVec;
					gameObject.destination = proposedPos;

					gameMap.SetTileOccupied(gameObject.localPosition, false);
					gameMap.SetTileOccupied(gameObject.destination, true);
				}
			}
		}

		// handle combat
		const auto weaponSpeed = 5.0f;
		const auto damageMin = 1;
		const auto damageMax = 3;

		if (comp.swingTimer < weaponSpeed)
			comp.swingTimer += UPDATE_FREQUENCY;

		if (comp.targetId >= 0)
		{
			const GameObject& target = objectManager.GetGameObjectById(comp.targetId);

			if (Utility::AreOnAdjacentOrDiagonalTiles(gameObject.localPosition, target.localPosition))
			{
				if (comp.swingTimer >= weaponSpeed)
				{
					comp.swingTimer = 0.0f;
					
					const auto gameObjectId = gameObject.GetId();
					const auto targetId = target.GetId();
					const auto hit = dist100(rng) > 50;
					if (hit)
					{
						std::uniform_int_distribution<std::mt19937::result_type> distDamage(damageMin, damageMax);
						const auto dmg = distDamage(rng);

						StatsComponent& targetStatsComponent = statsComponentManager->GetComponentById(target.statsComponentId);
						targetStatsComponent.health = Utility::Max<int>(0, targetStatsComponent.health - dmg);

						const int* const weaponSkillIds = new int[2]{ 1, 2 }; // Hand-to-Hand Combat, Melee
						std::unique_ptr<Event> e = std::make_unique<AttackHitEvent>(gameObjectId, targetId, (int)dmg, weaponSkillIds, 2);
						eventHandler.QueueEvent(e);

						std::vector<std::string> args{ std::to_string(gameObjectId), std::to_string(targetId), std::to_string(dmg) };
						socketManager.SendPacketToAllClients(OpCode::AttackHit, args);
					}
					else
					{
						const int* const weaponSkillIds = new int[2]{ 1, 2 }; // Hand-to-Hand Combat, Melee
						std::unique_ptr<Event> e = std::make_unique<AttackMissEvent>(gameObjectId, targetId, weaponSkillIds, 2);
						eventHandler.QueueEvent(e);

						std::vector<std::string> args{ std::to_string(gameObjectId), std::to_string(targetId)};
						socketManager.SendPacketToAllClients(OpCode::AttackMiss, args);
					}
				}
			}
		}
	}
}
