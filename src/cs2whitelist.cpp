#include <cstring>
#include <algorithm>

#include "cs2whitelist.h"
#include "db/wl_config.h"
#include "db/wl_database.h"
#include "interfaces/cs2admin/ics2admin.h"
#include "lang/translations.h"
#include "player/player_manager.h"
#include "steamgroup/steamgroup_manager.h"
#include "utils/utils.h"
#include "whitelist/whitelist_manager.h"

#include "mmu/cvarquery.h"
#include "mmu/http_client.h"
#include "mmu/log.h"

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
ICS2Admin *g_pCS2Admin = nullptr;
IGameEventSystem *g_pGameEventSystem = nullptr;

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

	mmu::log::Setup logSetup;
	logSetup.channelName = "CS2Whitelist";
	logSetup.addonName = "cs2whitelist";
	logSetup.toFile = true;
	mmu::log::Init(logSetup);

	mmu::http::SetUserAgent("CS2Whitelist/1.0");
	mmu::http::ResetShutdownLatch();

	GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngine, IVEngineServer, INTERFACEVERSION_VENGINESERVER);
	GET_V_IFACE_CURRENT(GetEngineFactory, g_pICvar, ICvar, CVAR_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetServerFactory, g_pServerGameDLL, IServerGameDLL, INTERFACEVERSION_SERVERGAMEDLL);
	GET_V_IFACE_ANY(GetServerFactory, g_pGameClients, IServerGameClients, INTERFACEVERSION_SERVERGAMECLIENTS);
	GET_V_IFACE_ANY(GetEngineFactory, g_pNetworkMessages, INetworkMessages, NETWORKMESSAGES_INTERFACE_VERSION);
	GET_V_IFACE_ANY(GetEngineFactory, g_pGameEventSystem, IGameEventSystem, GAMEEVENTSYSTEM_INTERFACE_VERSION);

	m_bLateLoaded = late;
	m_bSkipLevelInitReload = !late;
	g_SMAPI->AddListener(this, this);

	// Non fatal, translations fall back to the default language.
	mmu::cvarquery::Init(g_pEngine);

	m_OnClientConnected.Add(g_pGameClients);
	m_ClientPutInServer.Add(g_pGameClients);
	m_ClientDisconnect.Add(g_pGameClients);
	m_GameFrame.Add(g_pServerGameDLL);

	g_pCVar = g_pICvar;
	META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);

	g_WLManager.LoadFile();

	MMU_LOG_INFO("Plugin loaded (v%s)%s.\n", PLUGIN_FULL_VERSION, late ? " [late]" : "");
	return true;
}

bool CS2WhitelistPlugin::Unload(char *error, size_t maxlen)
{
	m_OnClientConnected.Remove(g_pGameClients);
	m_ClientPutInServer.Remove(g_pGameClients);
	m_ClientDisconnect.Remove(g_pGameClients);
	m_GameFrame.Remove(g_pServerGameDLL);

	// Drops pending callbacks pointing into this DLL.
	mmu::cvarquery::Shutdown();

	g_pCS2Admin = nullptr;
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
	// Re-check if mm-cs2admin is now available (e.g. loaded dynamically after us).
	if (!g_pCS2Admin)
	{
		g_pCS2Admin = static_cast<ICS2Admin *>(g_SMAPI->MetaFactory(CS2ADMIN_INTERFACE, nullptr, nullptr));
		if (g_pCS2Admin)
		{
			MMU_LOG_INFO("mm-cs2admin interface acquired (late load).\n");
		}
	}
}

void CS2WhitelistPlugin::OnPluginUnload(PluginId id)
{
	// Re-check if mm-cs2admin is still available; if it was the unloaded plugin,
	// MetaFactory will return nullptr and we clear the dangling pointer.
	g_pCS2Admin = static_cast<ICS2Admin *>(g_SMAPI->MetaFactory(CS2ADMIN_INTERFACE, nullptr, nullptr));
}

void CS2WhitelistPlugin::AllPluginsLoaded()
{
	g_pCS2Admin = static_cast<ICS2Admin *>(g_SMAPI->MetaFactory(CS2ADMIN_INTERFACE, nullptr, nullptr));

	if (g_pCS2Admin)
	{
		MMU_LOG_INFO("mm-cs2admin interface acquired! "
					 "Admin immunity and in-game commands enabled.\n");
	}
	else
	{
		MMU_LOG_INFO("mm-cs2admin not loaded. "
					 "Admin commands restricted to server console.\n");
	}

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
		g_WLDatabase.Connect(
			[this](bool success)
			{
				if (success)
				{
					g_WLDatabase.LoadEntries(g_WLManager.GetSet(), [](int count) { MMU_LOG_INFO("Loaded %d entries from database.\n", count); });
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
		g_WLDatabase.LoadEntries(g_WLManager.GetSet(), [](int count) { MMU_LOG_INFO("Merged %d DB entries on map load.\n", count); });
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
	g_WLPlayerManager.OnClientConnected(slot.Get(), xuid, pszAddress, bFakePlayer);
	mmu::cvarquery::OnClientConnected(slot.Get(), bFakePlayer);
	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2WhitelistPlugin::Hook_ClientPutInServer(IServerGameClients *, CPlayerSlot slot, char const *pszName, int type, uint64 xuid)
{
	int idx = slot.Get();
	const PlayerInfo *p = g_WLPlayerManager.GetPlayer(idx);
	if (!p || p->fakePlayer)
	{
		return {KHook::Action::Ignore};
	}

	if (!cv_enable.Get())
	{
		return {KHook::Action::Ignore};
	}
	if (g_WLManager.IsWhitelistCached(p->xuid))
	{
		return {KHook::Action::Ignore};
	}

	if (cv_immunity.Get() && g_pCS2Admin && g_pCS2Admin->IsAdmin(idx))
	{
		MMU_LOG_INFO("Slot %d (%s) has admin immunity.\n", idx, pszName ? pszName : "?");
		g_WLManager.AddToWhitelistCache(p->xuid);
		return {KHook::Action::Ignore};
	}

	if (g_WLManager.IsBlacklisted(p->xuid))
	{
		std::string msg = WL_Translate(idx, "You are not whitelisted on this server.");
		char kickmsg[512];
		snprintf(kickmsg, sizeof(kickmsg), "[WHITELIST] %s\n", msg.c_str());
		if (g_pEngine)
		{
			g_pEngine->ClientPrintf(slot, kickmsg);
			g_pEngine->DisconnectClient(slot, NETWORK_DISCONNECT_KICKED, msg.c_str());
		}
		return {KHook::Action::Ignore};
	}

	if (g_WLManager.IsPlayerWhitelisted(idx))
	{
		g_WLManager.AddToWhitelistCache(p->xuid);
		return {KHook::Action::Ignore};
	}

	// Steam group check (may be async, returns pending=true to defer the kick)
	if (g_SteamGroupManager.IsEnabled())
	{
		bool pending = false;
		bool inGroup = g_SteamGroupManager.CheckPlayer(idx, p->xuid, pending);
		if (pending)
		{
			return {KHook::Action::Ignore}; // async check in flight; kick (or allow) will happen from the callback
		}
		if (inGroup)
		{
			g_WLManager.AddToWhitelistCache(p->xuid);
			return {KHook::Action::Ignore};
		}
	}

	for (ICS2WhitelistListener *l : m_listeners)
	{
		if (l->OnWhitelistKickPre(idx) == WLKickResult::Block)
		{
			g_WLManager.AddToWhitelistCache(p->xuid);
			return {KHook::Action::Ignore};
		}
	}

	std::string msg = WL_Translate(idx, "You are not whitelisted on this server.");

	if (g_pEngine)
	{
		WLLogKick(pszName, p->xuid, p->ip.c_str(), false);
		g_WLManager.AddToBlacklistCache(p->xuid);

		char kickmsg[512];
		snprintf(kickmsg, sizeof(kickmsg), "[WHITELIST] %s\n", msg.c_str());
		g_pEngine->ClientPrintf(slot, kickmsg);

		g_pEngine->DisconnectClient(slot, NETWORK_DISCONNECT_KICKED, msg.c_str());
	}

	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2WhitelistPlugin::Hook_ClientDisconnect(IServerGameClients *, CPlayerSlot slot, ENetworkDisconnectionReason reason,
															  const char *pszName, uint64 xuid, const char *pszNetworkID)
{
	const int idx = slot.Get();
	g_SteamGroupManager.OnPlayerDisconnect(idx);
	g_WLPlayerManager.OnClientDisconnect(idx);
	mmu::cvarquery::OnClientDisconnect(idx);
	return {KHook::Action::Ignore};
}

KHook::Return<void> CS2WhitelistPlugin::Hook_GameFrame(IServerGameDLL *, bool simulating, bool bFirstTick, bool bLastTick)
{
	// Run HTTP continuations queued by the mmu::http worker (steam group checks).
	mmu::http::DrainMainThread();
	g_SteamGroupManager.OnGameFrame();
	return {KHook::Action::Ignore};
}

bool CS2WhitelistPlugin::IsPlayerWhitelisted(int slot) const
{
	return g_WLManager.IsPlayerWhitelisted(slot);
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
	const PlayerInfo *p = g_WLPlayerManager.GetPlayer(slot);
	return p && g_WLManager.IsWhitelistCached(p->xuid);
}

int CS2WhitelistPlugin::GetWhitelistCacheCount() const
{
	return g_WLManager.GetWhitelistCacheCount();
}

bool CS2WhitelistPlugin::IsPlayerBlacklisted(int slot) const
{
	const PlayerInfo *p = g_WLPlayerManager.GetPlayer(slot);
	return p && g_WLManager.IsBlacklisted(p->xuid);
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
	bool ok = g_WLManager.AddEntry(entry);
	if (ok)
	{
		g_WLManager.SaveFile();
	}
	return ok;
}

bool CS2WhitelistPlugin::RemoveEntry(const char *entry)
{
	bool ok = g_WLManager.RemoveEntry(entry);
	if (ok)
	{
		g_WLManager.SaveFile();
	}
	return ok;
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
