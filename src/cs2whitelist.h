#ifndef _INCLUDE_CS2WHITELIST_H_
#define _INCLUDE_CS2WHITELIST_H_

#include "common.h"
#include "version_gen.h"
#include "interfaces/cs2whitelist/ics2whitelist.h"

#include <string>
#include <vector>

class CS2WhitelistPlugin : public ISmmPlugin, public IMetamodListener, public ICS2Whitelist
{
public:
	CS2WhitelistPlugin();

	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late);

	// Read core.cfg, apply it to the convars, then load the whitelist file it names.
	void LoadConfigAndFile();

	bool Unload(char *error, size_t maxlen);
	void AllPluginsLoaded();
	void *OnMetamodQuery(const char *iface, int *ret);

public:
	void OnLevelInit(char const *pMapName, char const *pMapEntities, char const *pOldLevel, char const *pLandmarkName, bool loadGame,
					 bool background);
	void OnPluginLoad(PluginId id);
	void OnPluginUnload(PluginId id);

public:
	const char *GetAuthor()
	{
		return PLUGIN_AUTHOR;
	}

	const char *GetName()
	{
		return PLUGIN_DISPLAY_NAME;
	}

	const char *GetDescription()
	{
		return PLUGIN_DESCRIPTION;
	}

	const char *GetURL()
	{
		return PLUGIN_URL;
	}

	const char *GetLicense()
	{
		return PLUGIN_LICENSE;
	}

	const char *GetVersion()
	{
		return PLUGIN_FULL_VERSION;
	}

	const char *GetDate()
	{
		return __DATE__;
	}

	const char *GetLogTag()
	{
		return PLUGIN_LOGTAG;
	}

public:
	KHook::Return<void> Hook_OnClientConnected(IServerGameClients *, CPlayerSlot slot, const char *pszName, uint64 xuid, const char *pszNetworkID,
											   const char *pszAddress, bool bFakePlayer);
	KHook::Return<void> Hook_ClientPutInServer(IServerGameClients *, CPlayerSlot slot, char const *pszName, int type, uint64 xuid);
	KHook::Return<void> Hook_ClientDisconnect(IServerGameClients *, CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName,
											  uint64 xuid, const char *pszNetworkID);
	KHook::Return<void> Hook_GameFrame(IServerGameDLL *, bool simulating, bool bFirstTick, bool bLastTick);

public:
	bool IsPlayerWhitelisted(int slot) const override;
	bool IsEntryWhitelisted(const char *entry) const override;
	int GetEntryCount() const override;

	bool IsPlayerWhitelistCached(int slot) const override;
	int GetWhitelistCacheCount() const override;

	bool IsPlayerBlacklisted(int slot) const override;
	int GetBlacklistCacheCount() const override;

	bool ReloadFile() override;
	bool AddEntry(const char *entry) override;
	bool RemoveEntry(const char *entry) override;

	void AddListener(ICS2WhitelistListener *listener) override;
	void RemoveListener(ICS2WhitelistListener *listener) override;

	// The final not-whitelisted step, shared by the synchronous check and the async Steam group results.
	// Listeners can still let the player in, otherwise the kick is logged, cached and carried out.
	// cacheReject=false kicks without caching, for a rejection that only reflects missing group data.
	void RejectPlayer(int slot, const char *name, bool cacheReject = true);

private:
	bool m_bLateLoaded = false;

	// On a normal load the startup path already loads the file,
	// fetches groups and reads the DB, and the first level init lands right on top of it.
	// Only later level inits are real map changes.
	bool m_bSkipLevelInitReload = false;

	std::vector<ICS2WhitelistListener *> m_listeners;

	// The whitelist decision for one player, kick included.
	void CheckPlayer(int slot, const std::string &name);

	struct PendingCheck
	{
		int slot;
		uint64_t xuid;
		std::string name;
	};

	// Decided on the GameFrame after ClientPutInServer rather than inside it.
	// mm-cs2admin assigns admin flags in its own ClientPutInServer hook,
	// and which plugin's hook runs first follows load order, which nothing enforces.
	std::vector<PendingCheck> m_pendingChecks;

	KHook::Virtual<IServerGameClients, void, CPlayerSlot, const char *, uint64, const char *, const char *, bool> m_OnClientConnected;
	KHook::Virtual<IServerGameClients, void, CPlayerSlot, char const *, int, uint64> m_ClientPutInServer;
	KHook::Virtual<IServerGameClients, void, CPlayerSlot, ENetworkDisconnectionReason, const char *, uint64, const char *> m_ClientDisconnect;
	KHook::Virtual<IServerGameDLL, void, bool, bool, bool> m_GameFrame;
};

extern CS2WhitelistPlugin g_ThisPlugin;

#endif // _INCLUDE_CS2WHITELIST_H_
