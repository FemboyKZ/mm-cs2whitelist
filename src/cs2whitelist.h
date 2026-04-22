#ifndef _INCLUDE_CS2WHITELIST_H_
#define _INCLUDE_CS2WHITELIST_H_

#include <ISmmPlugin.h>
#include <igameevents.h>
#include <sh_vector.h>
#include <iserver.h>
#include <cstdint>
#include <string>
#include <unordered_set>
#include "version_gen.h"

#define WL_MAXPLAYERS 64

// Forward declaration - full definition in ics2admin.h, optional dependency
class ICS2Admin;

class CS2WhitelistPlugin : public ISmmPlugin, public IMetamodListener
{
public:
	bool Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late);
	bool Unload(char *error, size_t maxlen);
	void AllPluginsLoaded();

public: // IMetamodListener
	void OnLevelInit(char const *pMapName, char const *pMapEntities,
	                 char const *pOldLevel, char const *pLandmarkName,
	                 bool loadGame, bool background);

public: // ISmmPlugin info
	const char *GetAuthor()      { return PLUGIN_AUTHOR; }
	const char *GetName()        { return PLUGIN_DISPLAY_NAME; }
	const char *GetDescription() { return PLUGIN_DESCRIPTION; }
	const char *GetURL()         { return PLUGIN_URL; }
	const char *GetLicense()     { return PLUGIN_LICENSE; }
	const char *GetVersion()     { return PLUGIN_FULL_VERSION; }
	const char *GetDate()        { return __DATE__; }
	const char *GetLogTag()      { return PLUGIN_LOGTAG; }

public: // SH hook handlers
	void Hook_OnClientConnected(CPlayerSlot slot, const char *pszName, uint64 xuid,
	                            const char *pszNetworkID, const char *pszAddress,
	                            bool bFakePlayer);
	void Hook_ClientPutInServer(CPlayerSlot slot, char const *pszName, int type,
	                            uint64 xuid);
	void Hook_ClientDisconnect(CPlayerSlot slot, ENetworkDisconnectionReason reason,
	                           const char *pszName, uint64 xuid,
	                           const char *pszNetworkID);

public: // Whitelist API
	// Load/save the whitelist file. Returns true on success.
	bool LoadWhitelistFile();
	bool SaveWhitelistFile();

	// Add/remove a raw entry string (SteamID or IP). Returns true if the set changed.
	bool AddEntry(const char *entry);
	bool RemoveEntry(const char *entry);

	// Check if the player in the given slot passes the whitelist.
	bool IsPlayerWhitelisted(int slot) const;

	// Print the full whitelist to a player slot (or server console if slot < 0).
	void PrintListToSlot(int slot) const;

	int GetEntryCount() const { return static_cast<int>(m_whitelist.size()); }

	// Check whether the caller (slot < 0 = server console) has the given cs2admin flag.
	// Always returns true for the server console.
	bool HasAdminAccess(int slot, uint32_t flag) const;

	// Printf-style reply to either a player's console or the server console.
	void ReplyToSlot(int slot, const char *fmt, ...) const;

public: // Static helpers
	// Convert a SteamID64 to STEAM_0:X:Y form.
	static std::string SteamID64ToAuthId(uint64_t id64);

	// Normalise any whitelist entry to a canonical form:
	//   STEAM_X:Y:Z  -> STEAM_0:Y:Z
	//   SteamID64    -> decimal string as is
	//   IP address   -> as is
	// Returns an empty string for blank or comment-only input.
	static std::string NormalizeEntry(const char *input);

	// Strip the :port suffix from an address string ("1.2.3.4:12345" -> "1.2.3.4").
	static std::string StripPort(const char *addr);

private:
	std::unordered_set<std::string> m_whitelist;

	struct PlayerInfo
	{
		std::string ip;
		uint64      xuid       = 0;
		bool        fakePlayer = false;
	};
	PlayerInfo m_players[WL_MAXPLAYERS + 1];

	ICS2Admin *m_pCS2Admin  = nullptr;
	bool       m_bLateLoaded = false;
};

extern CS2WhitelistPlugin g_ThisPlugin;

PLUGIN_GLOBALVARS();

extern IVEngineServer     *g_pEngine;
extern IServerGameClients *g_pGameClients;
extern IServerGameDLL     *g_pServerGameDLL;
extern ICvar              *g_pICvar;

#endif // _INCLUDE_CS2WHITELIST_H_
