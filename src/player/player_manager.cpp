#include "player_manager.h"
#include "utils/utils.h"

WLPlayerManager g_WLPlayerManager;

void WLPlayerManager::OnClientConnected(int slot, uint64_t xuid, const char *address, bool fakePlayer)
{
	if (slot < 0 || slot > WL_MAXPLAYERS)
	{
		return;
	}

	m_players[slot].xuid = xuid;
	m_players[slot].fakePlayer = fakePlayer;
	m_players[slot].ip = StripPort(address ? address : "");
}

void WLPlayerManager::OnClientDisconnect(int slot)
{
	if (slot < 0 || slot > WL_MAXPLAYERS)
	{
		return;
	}

	m_players[slot] = PlayerInfo {};
}

const PlayerInfo *WLPlayerManager::GetPlayer(int slot) const
{
	if (slot < 0 || slot > WL_MAXPLAYERS)
	{
		return nullptr;
	}

	return &m_players[slot];
}
