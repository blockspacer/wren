#include "stdafx.h"
#include <OpCodes.h>
#include <ObjectManager.h>
#include "SocketManager.h"
#include <Components/StatsComponentManager.h>
#include "Components/AIComponentManager.h"
#include "Components/PlayerComponentManager.h"

constexpr auto PLAYER_NOT_FOUND = "Player not found.";
constexpr auto SOCKET_INIT_FAILED = "Failed to initialize sockets.";
constexpr auto INCORRECT_USERNAME = "Incorrect Username.";
constexpr auto INCORRECT_PASSWORD = "Incorrect Password.";
constexpr auto ACCOUNT_ALREADY_EXISTS = "Account already exists.";
constexpr auto LIBSODIUM_MEMORY_ERROR = "Ran out of memory while hashing password.";
constexpr auto CHARACTER_ALREADY_EXISTS = "Character already exists.";
constexpr auto INVALID_ATTACK_TARGET = "You can't attack that!";
constexpr auto NO_ATTACK_TARGET = "You need a target before attacking!";
constexpr auto MESSAGE_TYPE_ERROR = "ERROR";

constexpr auto PORT_NUMBER = 27016;

extern ObjectManager g_objectManager;
extern StatsComponentManager g_statsComponentManager;
extern AIComponentManager g_aiComponentManager;
extern PlayerComponentManager g_playerComponentManager;
extern GameMap g_gameMap;
extern EventHandler g_eventHandler;

SocketManager::SocketManager(ServerRepository& repository, CommonRepository& commonRepository)
	: repository{ repository },
	  commonRepository{ commonRepository }
{
    sodium_init();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != NO_ERROR)
        throw new std::exception(SOCKET_INIT_FAILED);

    local.sin_family = AF_INET;
    local.sin_port = htons(PORT_NUMBER);
    local.sin_addr.s_addr = INADDR_ANY;

	fromlen = sizeof(from);

    socketS = socket(AF_INET, SOCK_DGRAM, 0);
    int error;
    if (socketS == INVALID_SOCKET)
        error = WSAGetLastError();
    bind(socketS, (sockaddr*)&local, sizeof(local));

    DWORD nonBlocking = 1;
    ioctlsocket(socketS, FIONBIO, &nonBlocking);

	// initialize Abilities
	abilities = repository.ListAbilities();

	// initialize StaticObjects
	auto staticObjects = commonRepository.ListStaticObjects();

	for (auto staticObject : staticObjects)
	{
		const auto pos = staticObject->GetPosition();
		GameObject& gameObject = g_objectManager.CreateGameObject(pos, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, GameObjectType::StaticObject, staticObject->GetId(), true);
		g_gameMap.SetTileOccupied(gameObject.localPosition, true);

		delete staticObject;
	}

	// initialize test dummy
	// test
	auto dummyName = new std::string("Dummy");
	GameObject& dummyGameObject = g_objectManager.CreateGameObject(XMFLOAT3{ 30.0f, 0.0f, 30.0f }, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, GameObjectType::Npc, 101, false, 2, 4);
	auto dummyAIComponent = g_aiComponentManager.CreateAIComponent(dummyGameObject.id);
	dummyGameObject.aiComponentId = dummyAIComponent.id;
	StatsComponent& dummyStatsComponent = g_statsComponentManager.CreateStatsComponent(dummyGameObject.id, 100, 100, 100, 100, 100, 100, 10, 10, 10, 10, 10, 10, 10, dummyName);
	dummyGameObject.statsComponentId = dummyStatsComponent.id;
	g_gameMap.SetTileOccupied(dummyGameObject.localPosition, true);
}

bool SocketManager::MessagePartsEqual(const char* first, const char* second, const int length)
{
    for (auto i = 0; i < length; i++)
    {
        if (first[i] != second[i])
            return false;
    }
    return true;
}

const bool SocketManager::ValidateToken(const int accountId, const std::string token)
{
	const auto gameObject = g_objectManager.GetGameObjectById(accountId);
	const PlayerComponent& playerComponent = g_playerComponentManager.GetPlayerComponentById(gameObject.playerComponentId);
	
	return token == playerComponent.token;
}

PlayerComponent& SocketManager::GetPlayerComponent(const int accountId)
{
	const auto gameObject = g_objectManager.GetGameObjectById(accountId);

	return g_playerComponentManager.GetPlayerComponentById(gameObject.playerComponentId);
}

void SocketManager::HandleTimeout()
{
	auto playerComponents = g_playerComponentManager.GetPlayerComponents();
	auto playerComponentIndex = g_playerComponentManager.GetPlayerComponentIndex();

	for (unsigned int i = 0; i < playerComponentIndex; i++)
	{
		PlayerComponent& comp = playerComponents[i];
		if (GetTickCount() > comp.lastHeartbeat + TIMEOUT_DURATION)
		{
			std::cout << "AccountId " << comp.gameObjectId << " timed out." << "\n\n";
			g_objectManager.DeleteGameObject(g_eventHandler, comp.gameObjectId);
		}
	}
}

void SocketManager::Login(const std::string& accountName, const std::string& password, const std::string& ipAndPort, sockaddr_in from)
{
	std::cout << ipAndPort << std::endl;

	std::string error;
	auto account = repository.GetAccount(accountName);
	if (account)
	{
		auto passwordArr = password.c_str();
		if (crypto_pwhash_str_verify(account->GetPassword().c_str(), passwordArr, strlen(passwordArr)) != 0)
			error = INCORRECT_PASSWORD;
		else
		{
			GUID guid;
			CoCreateGuid(&guid);
			char guid_cstr[39];
			snprintf(guid_cstr, sizeof(guid_cstr),
				"{%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x}",
				guid.Data1, guid.Data2, guid.Data3,
				guid.Data4[0], guid.Data4[1], guid.Data4[2], guid.Data4[3],
				guid.Data4[4], guid.Data4[5], guid.Data4[6], guid.Data4[7]);
			const std::string token = std::string(guid_cstr);

			const auto accountId = account->GetId();
			GameObject& playerGameObject = g_objectManager.CreateGameObject(VEC_ZERO, XMFLOAT3{ 14.0f, 14.0f, 14.0f }, GameObjectType::Player, (long)accountId);
			const PlayerComponent& playerComponent = g_playerComponentManager.CreatePlayerComponent(playerGameObject.id, token, ipAndPort, from, GetTickCount());
			playerGameObject.playerComponentId = playerComponent.id;
            
			SendPacket(from, OPCODE_LOGIN_SUCCESSFUL, 3, std::to_string(accountId), token, ListCharacters(accountId));
			std::cout << "AccountId " << account->GetId() << " connected to the server.\n\n";
		}

	}
	else
		error = INCORRECT_USERNAME;

	if (error != "")
		SendPacket(from, OPCODE_LOGIN_UNSUCCESSFUL, 1, error);
}

void SocketManager::Logout(const int accountId)
{
	g_objectManager.DeleteGameObject(g_eventHandler, accountId);
}

void SocketManager::CreateAccount(const std::string& accountName, const std::string& password, sockaddr_in from)
{
	if (repository.AccountExists(accountName))
	{
		const std::string error = ACCOUNT_ALREADY_EXISTS;
		SendPacket(from, OPCODE_CREATE_ACCOUNT_UNSUCCESSFUL, 1, error);
	}
	else
	{
		char hashedPassword[crypto_pwhash_STRBYTES];
		auto passwordArr = password.c_str();
		const auto result = crypto_pwhash_str(
			hashedPassword,
			passwordArr,
			strlen(passwordArr),
			crypto_pwhash_OPSLIMIT_INTERACTIVE,
			crypto_pwhash_MEMLIMIT_INTERACTIVE);
		if (result != 0)
			throw std::exception(LIBSODIUM_MEMORY_ERROR);

		repository.CreateAccount(accountName, hashedPassword);
		SendPacket(from, OPCODE_CREATE_ACCOUNT_SUCCESSFUL);
	}
}

void SocketManager::CreateCharacter(const int accountId, const std::string& characterName)
{
	const PlayerComponent& playerComponent = GetPlayerComponent(accountId);

	if (repository.CharacterExists(characterName))
	{
		const std::string error = CHARACTER_ALREADY_EXISTS;
		SendPacket(playerComponent.fromSockAddr, OPCODE_CREATE_CHARACTER_UNSUCCESSFUL, 1, error);
	}
	else
	{
		repository.CreateCharacter(characterName, accountId);
		SendPacket(playerComponent.fromSockAddr, OPCODE_CREATE_CHARACTER_SUCCESSFUL, 1, ListCharacters(accountId));
	}
}

void SocketManager::UpdateLastHeartbeat(const int accountId)
{
	PlayerComponent& playerComponent = GetPlayerComponent(accountId);
	playerComponent.lastHeartbeat = GetTickCount();
}

void SocketManager::SendPacket(sockaddr_in from, const std::string& opcode, const int argCount, ...)
{
	int fromlen = sizeof(from);
	std::string response = std::string(CHECKSUM) + opcode;

	va_list args;
	va_start(args, argCount);

	for (auto i = 0; i < argCount; i++)
		response += (va_arg(args, std::string) + "|");
	va_end(args);

	char responseBuffer[1024];
	ZeroMemory(responseBuffer, sizeof(responseBuffer));
	strcpy_s(responseBuffer, sizeof(responseBuffer), response.c_str());
	sendto(socketS, responseBuffer, sizeof(responseBuffer), 0, (sockaddr*)&from, fromlen);
}

void SocketManager::CloseSockets()
{
    closesocket(socketS);
    WSACleanup();
}

bool SocketManager::TryRecieveMessage()
{
	char buffer[1024];
	char str[INET_ADDRSTRLEN];
	ZeroMemory(buffer, sizeof(buffer));
	ZeroMemory(str, sizeof(str));
	auto result = recvfrom(socketS, buffer, sizeof(buffer), 0, (sockaddr*)&from, &fromlen);
	int errorCode;
	if (result == -1)
		errorCode = WSAGetLastError();
	if (result != SOCKET_ERROR)
	{
		inet_ntop(AF_INET, &(from.sin_addr), str, INET_ADDRSTRLEN);

		const auto checksumArrLen = 8;
		char checksumArr[checksumArrLen];
		memcpy(&checksumArr[0], &buffer[0], checksumArrLen * sizeof(char));
		if (!MessagePartsEqual(checksumArr, CHECKSUM.c_str(), checksumArrLen))
		{
			std::cout << "Wrong checksum. Ignoring packet.\n";
			return true;
		}

		const auto opcodeArrLen = 2;
		char opcodeArr[opcodeArrLen];
		memcpy(&opcodeArr[0], &buffer[8], opcodeArrLen * sizeof(char));

		auto logMessage = true;
		if ((opcodeArr[0] == '1' && opcodeArr[1] == '5') || // PlayerUpdate
			(opcodeArr[0] == '1' && opcodeArr[1] == '0'))   // Heartbeat
			logMessage = false;

		if (logMessage)
		{
			printf("Received message from %s:%i - %s\n", str, from.sin_port, buffer);
			std::cout << "Opcode: " << opcodeArr[0] << opcodeArr[1] << "\n";
		}

		std::vector<std::string> args;
		auto bufferLength = strlen(buffer);
		if (bufferLength > 10)
		{
			std::string arg = "";
			for (unsigned int i = 10; i < bufferLength; i++)
			{
				if (buffer[i] == '|')
				{
					args.push_back(arg);
					arg = "";
				}
				else
					arg += buffer[i];
			}

			if (logMessage)
			{
				std::cout << "Args:\n";
				for_each(args.begin(), args.end(), [](std::string str) { std::cout << "  " << str << "\n";  });
				std::cout << "\n";
			}
		}

		if (MessagePartsEqual(opcodeArr, OPCODE_CONNECT, opcodeArrLen))
		{
			const auto accountName = args[0];
			const auto password = args[1];
			const auto ipAndPort = std::string(str) + ":" + std::to_string(from.sin_port);

			Login(accountName, password, ipAndPort, from);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_DISCONNECT, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];

			ValidateToken(accountId, token);
			Logout(accountId);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_CREATE_ACCOUNT, opcodeArrLen))
		{
			const auto accountName = args[0];
			const auto password = args[1];

			CreateAccount(accountName, password, from);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_CREATE_CHARACTER, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];
			const auto characterName = args[2];

			ValidateToken(accountId, token);
			CreateCharacter(accountId, characterName);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_HEARTBEAT, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];

			ValidateToken(accountId, token);
			UpdateLastHeartbeat(accountId);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_ENTER_WORLD, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];
			const auto characterName = args[2];

			ValidateToken(accountId, token);
			EnterWorld(accountId, characterName);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_DELETE_CHARACTER, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];
			const auto characterName = args[2];

			ValidateToken(accountId, token);
			DeleteCharacter(accountId, characterName);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_PLAYER_UPDATE, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];
			const auto idCounter = args[2];
			const auto characterId = args[3];
			const auto posX = args[4];
			const auto posY = args[5];
			const auto posZ = args[6];
			const auto isRightClickHeld = args[7];
			const auto mouseDirX = args[8];
			const auto mouseDirY = args[9];
			const auto mouseDirZ = args[10];

			ValidateToken(accountId, token);
			PlayerUpdate(accountId, idCounter, characterId, posX, posY, posZ, isRightClickHeld, mouseDirX, mouseDirY, mouseDirZ);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_ACTIVATE_ABILITY, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];
			const auto abilityId = args[2];

			ValidateToken(accountId, token);
			PlayerComponent& playerComponent = GetPlayerComponent(accountId);

			const auto abilityIt = find_if(abilities.begin(), abilities.end(), [&abilityId](Ability ability) { return ability.abilityId == std::stoi(abilityId); });
			if (abilityIt == abilities.end())
				return true;
			
			ActivateAbility(playerComponent, *abilityIt);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_SEND_CHAT_MESSAGE, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];
			const auto message = args[2];
			const auto senderName = args[3];

			ValidateToken(accountId, token);
			PropagateChatMessage(senderName, message);

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_SET_TARGET, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];
			const auto targetId = args[2];

			ValidateToken(accountId, token);
			PlayerComponent& playerComponent = GetPlayerComponent(accountId);
			playerComponent.targetId = std::stol(targetId);

			const auto gameObject = g_objectManager.GetGameObjectById(playerComponent.targetId);

			// Toggle off Auto-Attack on the server and the client if the player switches to an invalid target.
			if (gameObject.isStatic && playerComponent.autoAttackOn)
			{
				playerComponent.autoAttackOn = false;
				SendPacket(playerComponent.fromSockAddr, OPCODE_SERVER_MESSAGE, 2, std::string(INVALID_ATTACK_TARGET), std::string(MESSAGE_TYPE_ERROR));
				const std::string autoAttackAbilityId = "1";
				SendPacket(playerComponent.fromSockAddr, OPCODE_ACTIVATE_ABILITY_SUCCESS, 1, autoAttackAbilityId);
			}

			return true;
		}
		else if (MessagePartsEqual(opcodeArr, OPCODE_UNSET_TARGET, opcodeArrLen))
		{
			const auto accountId = std::stoi(args[0]);
			const auto token = args[1];

			ValidateToken(accountId, token);
			PlayerComponent& playerComponent = GetPlayerComponent(accountId);
			playerComponent.targetId = -1;

			return true;
		}
	}

	return false;
}

std::string SocketManager::ListCharacters(const int accountId)
{
    auto characters = repository.ListCharacters(accountId);
    std::string characterString = "";
    for (auto i = 0; i < characters.size(); i++)
        characterString += (characters.at(i) + ";");
    return characterString;
}

std::string SocketManager::ListSkills(const int characterId)
{
	auto skills = repository.ListCharacterSkills(characterId);
	std::string skillString = "";
	for (auto i = 0; i < skills.size(); i++)
	{
		auto skill = skills.at(i);
		skillString += (std::to_string(skill.skillId) + "%" + skill.name + "%" + std::to_string(skill.value) + ";");
	}
	return skillString;
}

std::string SocketManager::ListAbilities(const int characterId)
{
	auto abilities = repository.ListCharacterAbilities(characterId);
	std::string abilityString = "";
	for (auto i = 0; i < abilities.size(); i++)
	{
		auto ability = abilities.at(i);
		abilityString += (std::to_string(ability.abilityId) + "%" + ability.name + "%" + std::to_string(ability.spriteId) +  + "%" + std::to_string(ability.toggled) + "%" + std::to_string(ability.targeted) + ";");
	}
	return abilityString;
}

void SocketManager::EnterWorld(const int accountId, const std::string& characterName)
{
	GameObject& gameObject = g_objectManager.GetGameObjectById(accountId);
	PlayerComponent& playerComponent = GetPlayerComponent(accountId);
	auto character = repository.GetCharacter(characterName);
	gameObject.localPosition = character->GetPosition();

	playerComponent.lastHeartbeat = GetTickCount();
	playerComponent.characterId = character->GetId();
	playerComponent.modelId = character->GetModelId();
	playerComponent.textureId = character->GetTextureId();
	playerComponent.characterName = character->GetName();
	playerComponent.playerController = std::make_unique<PlayerController>(gameObject, g_gameMap);
	const auto pos = character->GetPosition();
	const auto charId = character->GetId();
	SendPacket(playerComponent.fromSockAddr, OPCODE_ENTER_WORLD_SUCCESSFUL, 9, std::to_string(accountId), std::to_string(pos.x), std::to_string(pos.y), std::to_string(pos.z), std::to_string(character->GetModelId()), std::to_string(character->GetTextureId()), ListSkills(charId), ListAbilities(charId), character->GetName());
	g_gameMap.SetTileOccupied(pos, true);
}

void SocketManager::DeleteCharacter(const int accountId, const std::string& characterName)
{
	const PlayerComponent& playerComponent = GetPlayerComponent(accountId);
	repository.DeleteCharacter(characterName);
	SendPacket(playerComponent.fromSockAddr , OPCODE_DELETE_CHARACTER_SUCCESSFUL, 1, ListCharacters(accountId));
}

void SocketManager::PlayerUpdate(
	const int accountId,
	const std::string& idCounter,
	const std::string& characterId,
	const std::string& posX,
	const std::string& posY,
	const std::string& posZ,
	const std::string& isRightClickHeld,
	const std::string& mouseDirX,
	const std::string& mouseDirY,
	const std::string& mouseDirZ)
{
	PlayerComponent& playerComponent = GetPlayerComponent(accountId);

	const auto id = playerComponent.updateCounter;

	if (id != std::stoi(idCounter))
		std::cout << "UpdateIds don't match! Id from client: " << idCounter << ", Id on server: " << id << std::endl;

	GameObject& player = g_objectManager.GetGameObjectById(std::stol(characterId));

	const auto clientPosX = std::stof(posX);
	const auto clientPosY = std::stof(posY);
	const auto clientPosZ = std::stof(posZ);
	auto currentPos = player.GetWorldPosition();

	if (currentPos.x != clientPosX || currentPos.y != clientPosY || currentPos.z != clientPosZ)
	{
		//std::cout << "Updates don't match. Need to send correction.\n";
		SendPacket(playerComponent.fromSockAddr, OPCODE_PLAYER_CORRECTION, 4, std::to_string(id), std::to_string(currentPos.x), std::to_string(currentPos.y), std::to_string(currentPos.z));
	}

	playerComponent.playerController->isRightClickHeld = isRightClickHeld == "1";
	playerComponent.playerController->currentMouseDirection = XMFLOAT3{ std::stof(mouseDirX), std::stof(mouseDirY), std::stof(mouseDirZ) };
	playerComponent.playerController->Update();
	playerComponent.updateCounter++;
}

void SocketManager::UpdateClients()
{
	auto gameObjectLength = g_objectManager.GetGameObjectIndex();
	auto gameObjects = g_objectManager.GetGameObjects();

	auto playerComponents = g_playerComponentManager.GetPlayerComponents();
	auto playerComponentIndex = g_playerComponentManager.GetPlayerComponentIndex();

	for (unsigned int i = 0; i < playerComponentIndex; i++)
	{
		PlayerComponent& playerToUpdate = playerComponents[i];

		// skip players that have logged in, but haven't selected a character and entered the game yet
		if (playerToUpdate.characterId == 0)
			continue;

		// for each player, send them an update for every non-static game object
		for (unsigned int j = 0; j < gameObjectLength; j++)
		{
			auto gameObject = gameObjects[j];

			// skip sending an update if the game object is the player receiving the update
			if (playerToUpdate.characterId == gameObject.id)
				continue;

			auto pos = gameObject.GetWorldPosition();
			auto mov = gameObject.movementVector;

			if (gameObject.type == GameObjectType::Npc)
				SendPacket(playerToUpdate.fromSockAddr, OPCODE_GAMEOBJECT_UPDATE, 8, std::to_string(gameObject.id), std::to_string(pos.x), std::to_string(pos.y), std::to_string(pos.z), std::to_string(mov.x), std::to_string(mov.y), std::to_string(mov.z), std::to_string(static_cast<int>(GameObjectType::Npc)));
			else if (gameObject.type == GameObjectType::Player)
			{
				auto characterId = gameObject.id;
				const PlayerComponent& otherPlayer = g_playerComponentManager.GetPlayerComponentById(gameObject.playerComponentId);
				SendPacket(playerToUpdate.fromSockAddr, OPCODE_OTHER_PLAYER_UPDATE, 10, std::to_string(gameObject.id), std::to_string(pos.x), std::to_string(pos.y), std::to_string(pos.z), std::to_string(mov.x), std::to_string(mov.y), std::to_string(mov.z), std::to_string(otherPlayer.modelId), std::to_string(otherPlayer.textureId), otherPlayer.characterName);
			}
		}
	}
}

void SocketManager::PropagateChatMessage(const std::string& senderName, const std::string& message)
{
	auto playerComponents = g_playerComponentManager.GetPlayerComponents();
	auto playerComponentIndex = g_playerComponentManager.GetPlayerComponentIndex();

	for (unsigned int i = 0; i < playerComponentIndex; i++)
	{
		PlayerComponent& playerToUpdate = playerComponents[i];		
		SendPacket(playerToUpdate.fromSockAddr, OPCODE_PROPAGATE_CHAT_MESSAGE, 2, senderName, message);
	}
}

void SocketManager::ActivateAbility(PlayerComponent& playerComponent, Ability& ability)
{
	if (ability.name == "Auto Attack")
	{
		if (!playerComponent.autoAttackOn && playerComponent.targetId == -1)
		{
			SendPacket(playerComponent.fromSockAddr, OPCODE_SERVER_MESSAGE, 2, std::string(NO_ATTACK_TARGET), std::string(MESSAGE_TYPE_ERROR));
			return;
		}
		else if (!playerComponent.autoAttackOn && g_objectManager.GetGameObjectById(playerComponent.targetId).isStatic)
		{
			SendPacket(playerComponent.fromSockAddr, OPCODE_SERVER_MESSAGE, 2, std::string(INVALID_ATTACK_TARGET), std::string(MESSAGE_TYPE_ERROR));
			return;
		}
		else
		{
			playerComponent.autoAttackOn = !playerComponent.autoAttackOn;
		}
	}
	else if (ability.name == "Fireball")
	{

	}
	else if (ability.name == "Healing")
	{

	}

	SendPacket(playerComponent.fromSockAddr, OPCODE_ACTIVATE_ABILITY_SUCCESS, 1, std::to_string(ability.abilityId));
}