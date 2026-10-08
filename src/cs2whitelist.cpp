#include <cstring>
#include <algorithm>

#include "cs2whitelist.h"
#include "db/wl_config.h"
#include "db/wl_database.h"
#include "interfaces/cs2admin/ics2admin.h"
#include "lang/translations.h"
#include "steamgroup/steamgroup_manager.h"
#include "utils/utils.h"
#include "whitelist/whitelist_manager.h"

#include "game/cvarquery.h"
#include "game/players.h"
#include "utils/http_client.h"
#include "utils/log.h"

#include <eiface.h>
#include <engine/igameeventsystem.h>
#include <interfaces/interfaces.h>
#include <iserver.h>
#include <networksystem/inetworkmessages.h>
#include <tier1/convar.h>

CS2WhitelistPlugin g_ThisPlugin;
PLUGIN_EXPOSE(CS2WhitelistPlugin, g_ThisPlugin);

PLUGIN_GLOBALVARS();
IVEngineServer *g_pEngine = nullptr;
IServerGameClients *g_pGameClients = nullptr;
IServerGameDLL *g_pServerGameDLL = nullptr;
ICvar *g_pICvar = nullptr;
mmu::AdminAccess g_CS2Admin("whitelist");
IGameEventSystem *g_pGameEventSystem = nullptr;

constexpr double kAuthWaitSeconds = 10.0;
constexpr double kLateHoldSeconds = 15.0;

std::string WL_SlotLanguage(int slot)
{
	const char *raw = mmu::cvarquery::GetClientLanguage(slot);
	return g_WLTranslations.MapClientLanguage(raw);
}

void WL_LoadTranslations()
{
	// Phrases render in consoles and on the disconnect screen, never in chat.
	g_WLTranslations.SetResolveColorTags(false);
	g_WLTranslations.Load(g_SMAPI->GetBaseDir(), "cs2whitelist");
	g_WLTranslations.SetDefaultLanguage(g_WLConfig.defaultLanguage);
}

CS2WhitelistPlugin::CS2WhitelistPlugin()
	: m_OnClientConnected(&IServerGameClients::OnClientConnected, this, &CS2WhitelistPlugin::Hook_OnClientConnected, nullptr),
	  m_ClientPutInServer(&IServerGameClients::ClientPutInServer, this, nullptr, &CS2WhitelistPlugin::Hook_ClientPutInServer),
	  m_ClientDisconnect(&IServerGameClients::ClientDisconnect, this, nullptr, &CS2WhitelistPlugin::Hook_ClientDisconnect),
	  m_GameFrame(&IServerGameDLL::GameFrame, this, &CS2WhitelistPlugin::Hook_GameFrame, nullptr)
{
}

bool CS2WhitelistPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	mmu::log::Init("CS2Whitelist", "cs2whitelist");

	mmu::http::SetUserAgent((std::string("CS2Whitelist/") + PLUGIN_FULL_VERSION).c_str());
	mmu::http::ResetShutdownLatch();

	MMU_GET_CORE_INTERFACES();

	m_bSkipLevelInitReload = !late;
	g_SMAPI->AddListener(this, this);

	// Non fatal, translations fall back to the default language.
	mmu::cvarquery::Init(g_pEngine);

	if (late)
	{
		const double now = Plat_FloatTime();
		m_lateHoldUntil = now + kLateHoldSeconds;
		mmu::players::OnLateLoad();
		for (int slot = 0; slot <= MAXPLAYERS; slot++)
		{
			if (mmu::players::Get(slot))
			{
				m_authDeadline[slot] = now + kAuthWaitSeconds;
				mmu::cvarquery::OnClientConnected(slot, false);
			}
		}
	}

	m_OnClientConnected.Add(g_pGameClients);
	m_ClientPutInServer.Add(g_pGameClients);
	m_ClientDisconnect.Add(g_pGameClients);
	m_GameFrame.Add(g_pServerGameDLL);

	g_pCVar = g_pICvar;
	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);

	// core.cfg names the whitelist file, so it is read before the file rather than from AllPluginsLoaded,
	// which would leave the whitelist empty for anything querying us in between.
	// Nothing here depends on another plugin.
	LoadConfigAndFile();

	MMU_LOG_INFO("Plugin loaded (v%s)%s.\n", PLUGIN_FULL_VERSION, late ? " [late]" : "");
	return true;
}

void CS2WhitelistPlugin::LoadConfigAndFile()
{
	char cfgPath[512];
	snprintf(cfgPath, sizeof(cfgPath), "%s/cfg/cs2whitelist/core.cfg", g_SMAPI->GetBaseDir());

	if (WL_LoadConfig(cfgPath, g_WLConfig))
	{
		cv_enable.Set(g_WLConfig.enable);
		cv_immunity.Set(g_WLConfig.immunity);
		cv_filename.Set(CUtlString(g_WLConfig.filename.c_str()));
		cv_log.Set(g_WLConfig.logMode);
		MMU_LOG_INFO("Loaded core.cfg.\n");
	}
	else
	{
		MMU_LOG_WARN("core.cfg not found, using ConVar defaults.\n");
	}

	WL_LoadTranslations();

	// Reads cv_filename, so it follows the config.
	g_WLManager.LoadFile();
}

bool CS2WhitelistPlugin::Unload(char *error, size_t maxlen)
{
	m_OnClientConnected.Remove(g_pGameClients);
	m_ClientPutInServer.Remove(g_pGameClients);
	m_ClientDisconnect.Remove(g_pGameClients);
	m_GameFrame.Remove(g_pServerGameDLL);

	// Drops pending callbacks pointing into this DLL.
	mmu::cvarquery::Shutdown();

	g_CS2Admin.Shutdown();
	m_listeners.clear();
	g_SteamGroupManager.Shutdown();
	g_WLDatabase.Shutdown();

	// Join the HTTP worker, then drop any callbacks it queued for the game thread.
	mmu::http::Shutdown();
	mmu::http::ClearMainQueue();

	MMU_LOG_INFO("Plugin unloaded.\n");

	mmu::log::Shutdown();
	return true;
}

void CS2WhitelistPlugin::OnPluginLoad(PluginId id)
{
	if (g_CS2Admin.Refresh() == mmu::BridgeChange::Loaded)
	{
		MMU_LOG_INFO("mm-cs2admin interface acquired (late load).\n");
	}
}

void CS2WhitelistPlugin::OnPluginUnload(PluginId id)
{
	if (g_CS2Admin.Refresh() == mmu::BridgeChange::Unloaded)
	{
		MMU_LOG_INFO("mm-cs2admin unloaded - admin commands restricted to server console.\n");
	}
}

void CS2WhitelistPlugin::AllPluginsLoaded()
{
	g_CS2Admin.Refresh();

	if (g_CS2Admin.Available())
	{
		MMU_LOG_INFO("mm-cs2admin interface acquired! Admin immunity and in-game commands enabled.\n");
	}
	else
	{
		MMU_LOG_INFO("mm-cs2admin not loaded. Admin commands restricted to server console.\n");
	}

	{
		SteamGroupManager::Config sgCfg;
		sgCfg.enabled = g_WLConfig.sgEnabled;
		sgCfg.method = (g_WLConfig.sgMethod == "api") ? SteamGroupManager::Method::API : SteamGroupManager::Method::XML;
		sgCfg.apiKey = g_WLConfig.sgApiKey;
		sgCfg.timeout = g_WLConfig.sgTimeout;
		sgCfg.groupIds = g_WLConfig.sgGroupIds;
		g_SteamGroupManager.Init(sgCfg);
		g_SteamGroupManager.FetchGroups();
	}

	if (g_WLDatabase.Init(g_WLConfig))
	{
		// Runs again when a retried connect gets through.
		g_WLDatabase.Connect(
			[](bool success)
			{
				if (success)
				{
					g_WLManager.LoadDbEntries(
						[](int count)
						{
							if (count >= 0)
							{
								MMU_LOG_INFO("Loaded %d entries from database.\n", count);
							}
						});
				}
			});
	}
}

void CS2WhitelistPlugin::OnLevelInit(char const *pMapName, char const *pMapEntities, char const *pOldLevel, char const *pLandmarkName, bool loadGame,
									 bool background)
{
	g_WLManager.ClearBlacklistCache();
	g_WLManager.ClearWhitelistCache();

	// Redoing the startup load here would fire a second round of group fetches,
	// and StartXmlFetches throws away the first round's replies.
	if (m_bSkipLevelInitReload)
	{
		m_bSkipLevelInitReload = false;
		return;
	}

	g_WLManager.LoadFile();
	g_SteamGroupManager.FetchGroups();

	if (g_WLDatabase.IsConnected())
	{
		g_WLManager.LoadDbEntries(
			[](int count)
			{
				if (count >= 0)
				{
					MMU_LOG_INFO("Merged %d DB entries on map load.\n", count);
				}
			});
	}
}

void *CS2WhitelistPlugin::OnMetamodQuery(const char *iface, int *ret)
{
	if (!strcmp(iface, CS2WHITELIST_INTERFACE))
	{
		if (ret)
		{
			*ret = META_IFACE_OK;
		}
		return static_cast<ICS2Whitelist *>(this);
	}
	if (ret)
	{
		*ret = META_IFACE_FAILED;
	}
	return nullptr;
}

KHook::Return<void> CS2WhitelistPlugin::Hook_OnClientConnected(IServerGameClients *, CPlayerSlot slot, const char *pszName, uint64 xuid,
															   const char *pszNetworkID, const char *pszAddress, bool bFakePlayer)
{
	mmu::players::OnClientConnected(slot.Get(), pszName, xuid, pszAddress, bFakePlayer);
	mmu::cvarquery::OnClientConnected(slot.Get(), bFakePlayer);
	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2WhitelistPlugin::Hook_ClientPutInServer(IServerGameClients *, CPlayerSlot slot, char const *pszName, int type, uint64 xuid)
{
	int idx = slot.Get();
	mmu::players::OnClientPutInServer(idx);
	const mmu::Player *p = mmu::players::Get(idx);
	if (!p || p->fakePlayer)
	{
		return {KHook::Action::Ignore};
	}

	if (p->authenticated)
	{
		QueueCheck(idx);
	}
	else
	{
		m_authDeadline[idx] = Plat_FloatTime() + kAuthWaitSeconds;
	}
	return {KHook::Action::Ignore};
}

void CS2WhitelistPlugin::QueueCheck(int idx)
{
	const mmu::Player *p = mmu::players::Get(idx);
	if (!p || p->fakePlayer || !p->inGame)
	{
		return;
	}
	m_authDeadline[idx] = 0.0;
	m_pendingChecks.push_back({idx, p->steamid64, p->name});
}

void CS2WhitelistPlugin::CheckPlayer(int idx, const std::string &name)
{
	CPlayerSlot slot(idx);
	const char *pszName = name.c_str();
	const mmu::Player *p = mmu::players::Get(idx);
	if (!p)
	{
		return;
	}

	if (!cv_enable.Get())
	{
		return;
	}
	if (g_WLManager.IsWhitelistCached(p->steamid64))
	{
		return;
	}

	if (cv_immunity.Get() && g_CS2Admin.IsAdmin(idx))
	{
		MMU_LOG_INFO("Slot %d (%s) has admin immunity.\n", idx, pszName ? pszName : "?");
		g_WLManager.AddToWhitelistCache(p->steamid64);
		return;
	}

	if (g_WLManager.IsBlacklisted(p->steamid64))
	{
		WLLogKick(pszName, p->steamid64, p->ip.c_str(), true);
		std::string msg = WL_Translate(idx, "You are not whitelisted on this server.");
		char kickmsg[512];
		snprintf(kickmsg, sizeof(kickmsg), "[WHITELIST] %s\n", msg.c_str());
		if (g_pEngine)
		{
			g_pEngine->ClientPrintf(slot, kickmsg);
			g_pEngine->DisconnectClient(slot, NETWORK_DISCONNECT_KICKED, msg.c_str());
		}
		return;
	}

	if (g_WLManager.IsPlayerWhitelisted(idx))
	{
		g_WLManager.AddToWhitelistCache(p->steamid64);
		return;
	}

	// Steam group check (may be async, returns pending=true to defer the kick)
	bool groupDataMissing = false;
	if (g_SteamGroupManager.IsEnabled())
	{
		bool pending = false;
		bool inGroup = g_SteamGroupManager.CheckPlayer(idx, p->steamid64, name, pending);
		if (pending)
		{
			return; // async check in flight; kick (or allow) will happen from the callback
		}
		if (inGroup)
		{
			g_WLManager.AddToWhitelistCache(p->steamid64);
			return;
		}
		// A "not in group" answer off an incomplete member list is not one to hold against the player for the rest of the map.
		groupDataMissing = g_SteamGroupManager.DataIncomplete();
	}

	RejectPlayer(idx, pszName, !groupDataMissing);
}

void CS2WhitelistPlugin::RejectPlayer(int idx, const char *pszName, bool cacheReject)
{
	const mmu::Player *p = mmu::players::Get(idx);
	if (!p)
	{
		return;
	}

	for (ICS2WhitelistListener *l : m_listeners)
	{
		if (l->OnWhitelistKickPre(idx) == WLKickResult::Block)
		{
			g_WLManager.AddToWhitelistCache(p->steamid64);
			return;
		}
	}

	std::string msg = WL_Translate(idx, "You are not whitelisted on this server.");

	if (g_pEngine)
	{
		WLLogKick(pszName, p->steamid64, p->ip.c_str(), false);
		if (cacheReject)
		{
			g_WLManager.AddToBlacklistCache(p->steamid64);
		}

		CPlayerSlot slot(idx);
		char kickmsg[512];
		snprintf(kickmsg, sizeof(kickmsg), "[WHITELIST] %s\n", msg.c_str());
		g_pEngine->ClientPrintf(slot, kickmsg);

		g_pEngine->DisconnectClient(slot, NETWORK_DISCONNECT_KICKED, msg.c_str());
	}
}

KHook::Return<void> CS2WhitelistPlugin::Hook_ClientDisconnect(IServerGameClients *, CPlayerSlot slot, ENetworkDisconnectionReason reason,
															  const char *pszName, uint64 xuid, const char *pszNetworkID)
{
	const int idx = slot.Get();
	g_SteamGroupManager.OnPlayerDisconnect(idx);
	mmu::players::OnClientDisconnect(idx);
	if (idx >= 0 && idx <= MAXPLAYERS)
	{
		m_authDeadline[idx] = 0.0;
	}
	mmu::cvarquery::OnClientDisconnect(idx);
	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2WhitelistPlugin::Hook_GameFrame(IServerGameDLL *, bool simulating, bool bFirstTick, bool bLastTick)
{
	// Run HTTP continuations queued by the mmu::http worker (steam group checks).
	mmu::http::DrainMainThread();
	g_SteamGroupManager.OnGameFrame();

	const double now = Plat_FloatTime();
	g_WLDatabase.RunFrame(now);

	if (!m_pendingChecks.empty())
	{
		// Swapped out first, a kick below re-enters the client hooks.
		std::vector<PendingCheck> checks;
		checks.swap(m_pendingChecks);
		for (const PendingCheck &check : checks)
		{
			// The slot may have emptied or been reused since it was queued.
			const mmu::Player *p = mmu::players::Get(check.slot);
			if (p && !p->fakePlayer && p->steamid64 == check.xuid)
			{
				CheckPlayer(check.slot, check.name);
			}
		}
	}

	if (now < m_lateHoldUntil && g_WLDatabase.StartupLoadPending())
	{
		return {KHook::Action::Ignore};
	}

	// After the checks: one queued here runs next frame, once mm-cs2admin has seen the same confirmation.
	mmu::players::RunFrame([this](int slot) { QueueCheck(slot); });
	for (int slot = 0; slot <= MAXPLAYERS; slot++)
	{
		if (m_authDeadline[slot] > 0.0 && now >= m_authDeadline[slot])
		{
			m_authDeadline[slot] = 0.0;
			QueueCheck(slot);
		}
	}
	return {KHook::Action::Ignore};
}

bool CS2WhitelistPlugin::IsPlayerWhitelisted(int slot) const
{
	const mmu::Player *p = mmu::players::Get(slot);
	if (!cv_enable.Get() || !p || p->fakePlayer)
	{
		return true;
	}
	if (g_WLManager.IsWhitelistCached(p->steamid64))
	{
		return true;
	}
	if (g_WLManager.IsBlacklisted(p->steamid64))
	{
		return false;
	}
	if (cv_immunity.Get() && g_CS2Admin.IsAdmin(slot))
	{
		return true;
	}
	return g_WLManager.IsPlayerWhitelisted(slot) || g_SteamGroupManager.IsXuidInAnyGroup(p->steamid64);
}

bool CS2WhitelistPlugin::IsEntryWhitelisted(const char *entry) const
{
	return g_WLManager.IsEntryWhitelisted(entry);
}

int CS2WhitelistPlugin::GetEntryCount() const
{
	return g_WLManager.GetEntryCount();
}

bool CS2WhitelistPlugin::IsPlayerWhitelistCached(int slot) const
{
	const mmu::Player *p = mmu::players::Get(slot);
	return p && g_WLManager.IsWhitelistCached(p->steamid64);
}

int CS2WhitelistPlugin::GetWhitelistCacheCount() const
{
	return g_WLManager.GetWhitelistCacheCount();
}

bool CS2WhitelistPlugin::IsPlayerBlacklisted(int slot) const
{
	const mmu::Player *p = mmu::players::Get(slot);
	return p && g_WLManager.IsBlacklisted(p->steamid64);
}

int CS2WhitelistPlugin::GetBlacklistCacheCount() const
{
	return g_WLManager.GetBlacklistCacheCount();
}

bool CS2WhitelistPlugin::ReloadFile()
{
	return g_WLManager.LoadFile();
}

bool CS2WhitelistPlugin::AddEntry(const char *entry)
{
	return g_WLManager.AddEntry(entry);
}

bool CS2WhitelistPlugin::RemoveEntry(const char *entry)
{
	return g_WLManager.RemoveEntry(entry);
}

void CS2WhitelistPlugin::AddListener(ICS2WhitelistListener *listener)
{
	if (listener)
	{
		m_listeners.push_back(listener);
	}
}

void CS2WhitelistPlugin::RemoveListener(ICS2WhitelistListener *listener)
{
	m_listeners.erase(std::remove(m_listeners.begin(), m_listeners.end(), listener), m_listeners.end());
}
