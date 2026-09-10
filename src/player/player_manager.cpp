#include "player_manager.h"
#include "mmu/str_utils.h"

WLPlayerManager g_WLPlayerManager;

void WLPlayerManager::OnClientConnected(int slot, uint64_t xuid, const char *address, bool fakePlayer)
{
	PlayerInfo *p = m_players.Get(slot);
	if (!p)
	{
		return;
	}

	p->xuid = xuid;
	p->fakePlayer = fakePlayer;
	p->ip = str::StripPort(address ? address : "");
}

void WLPlayerManager::OnClientDisconnect(int slot)
{
	m_players.Clear(slot);
}

const PlayerInfo *WLPlayerManager::GetPlayer(int slot) const
{
	return m_players.Get(slot);
}
