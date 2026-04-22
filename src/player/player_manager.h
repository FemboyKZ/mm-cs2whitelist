#ifndef _INCLUDE_WL_PLAYER_MANAGER_H_
#define _INCLUDE_WL_PLAYER_MANAGER_H_

#include "common.h"
#include <cstdint>
#include <string>

struct PlayerInfo
{
	std::string ip;
	uint64_t xuid = 0;
	bool fakePlayer = false;
};

class WLPlayerManager
{
public:
	void OnClientConnected(int slot, uint64_t xuid, const char *address, bool fakePlayer);
	void OnClientDisconnect(int slot);

	const PlayerInfo *GetPlayer(int slot) const;

private:
	PlayerInfo m_players[WL_MAXPLAYERS + 1];
};

extern WLPlayerManager g_WLPlayerManager;

#endif // _INCLUDE_WL_PLAYER_MANAGER_H_
