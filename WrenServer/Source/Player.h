#pragma once

class Player
{
	const int accountId;
	const std::string token;
	const std::string ipAndPort;
	sockaddr_in fromSockAddr;
	int updateCounter{ 0 };

public:
	DWORD lastHeartbeat;
	std::string characterName{ "" };
	int characterId{ 0 };
	int modelId{ 0 };
	int textureId{ 0 };
	std::unique_ptr<PlayerController> playerController;
	long targetId{ -1 };
	unsigned int attackTimer{ 0 };
	bool autoAttackOn{ false };

	Player(
		const int accountId,
		const std::string& token,
		const std::string& ipAndPort,
		const DWORD lastHeartbeat,
		sockaddr_in fromSockAddr)
		: accountId{ accountId },
		  token{ token },
		  ipAndPort{ ipAndPort },
		  lastHeartbeat{ lastHeartbeat },
		  fromSockAddr{ fromSockAddr }
	{
	}

	const int GetAccountId() const { return accountId; }
	const std::string& GetToken() const { return token; }
	const std::string& GetIPAndPort() const { return ipAndPort; }
	const int GetUpdateCounter() const { return updateCounter; }
	void IncrementUpdateCounter() { updateCounter++; };
	sockaddr_in GetSockAddr() { return fromSockAddr; }
};
