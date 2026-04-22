#include <stdio.h>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <cctype>
#include <algorithm>
#include <string>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include "cs2whitelist.h"
#include "ics2admin.h"

#include <eiface.h>
#include <tier1/convar.h>
#include <iserver.h>

// SourceHook hook declarations
SH_DECL_HOOK6_void(IServerGameClients, OnClientConnected, SH_NOATTRIB, 0,
                   CPlayerSlot, const char *, uint64, const char *,
                   const char *, bool);
SH_DECL_HOOK4_void(IServerGameClients, ClientPutInServer, SH_NOATTRIB, 0,
                   CPlayerSlot, char const *, int, uint64);
SH_DECL_HOOK5_void(IServerGameClients, ClientDisconnect, SH_NOATTRIB, 0,
                   CPlayerSlot, ENetworkDisconnectionReason, const char *,
                   uint64, const char *);

// Plugin instance
CS2WhitelistPlugin g_ThisPlugin;
PLUGIN_EXPOSE(CS2WhitelistPlugin, g_ThisPlugin);

// Engine interface globals
IVEngineServer *g_pEngine = nullptr;
IServerGameClients *g_pGameClients = nullptr;
IServerGameDLL *g_pServerGameDLL = nullptr;
ICvar *g_pICvar = nullptr;

// ConVars
static CConVar<bool>
    cv_enable("mm_whitelist_enable", FCVAR_RELEASE | FCVAR_GAMEDLL,
              "Enable the server whitelist (1) or disable it (0).", true);

static CConVar<bool> cv_immunity("mm_whitelist_immunity",
                                 FCVAR_RELEASE | FCVAR_GAMEDLL,
                                 "Skip the whitelist check for players with "
                                 "any cs2admin flag (requires mm-cs2admin).",
                                 true);

static CConVar<CUtlString>
    cv_kickmessage("mm_whitelist_kickmessage", FCVAR_RELEASE | FCVAR_GAMEDLL,
                   "Message sent to the player's console when they are kicked "
                   "by the whitelist.",
                   "You are not whitelisted on this server.");

static CConVar<CUtlString>
    cv_filename("mm_whitelist_filename", FCVAR_RELEASE | FCVAR_GAMEDLL,
                "Whitelist file name, relative to <game>/cfg/cs2whitelist/. "
                "Path separators are stripped to prevent directory traversal.",
                "whitelist.txt");

// Helper: build the absolute whitelist file path
static std::string GetWhitelistFilePath() {
  // Strip any path separators to prevent directory traversal
  const char *raw = cv_filename.Get().Get();
  const char *basename = raw;
  for (const char *p = raw; *p; ++p)
    if (*p == '/' || *p == '\\')
      basename = p + 1;

  char path[512];
  snprintf(path, sizeof(path), "%s/cfg/cs2whitelist/%s", g_SMAPI->GetBaseDir(),
           basename);
  return path;
}

// Admin console commands
// Requires mm-cs2admin for in-game use; server console always passes.

CON_COMMAND_F(mm_whitelist_status,
              "Show whitelist status (enabled, entry count, file name).",
              FCVAR_GAMEDLL | FCVAR_RELEASE) {
  int slot = context.GetPlayerSlot().Get();
  if (!g_ThisPlugin.HasAdminAccess(slot, CS2ADMIN_FLAG_GENERIC))
    return;

  g_ThisPlugin.ReplyToSlot(
      slot, "[WHITELIST] enabled=%s | entries=%d | file=%s\n",
      cv_enable.Get() ? "yes" : "no", g_ThisPlugin.GetEntryCount(),
      cv_filename.Get().Get());
}

CON_COMMAND_F(mm_whitelist_reload, "Reload the whitelist file from disk.",
              FCVAR_GAMEDLL | FCVAR_RELEASE) {
  int slot = context.GetPlayerSlot().Get();
  if (!g_ThisPlugin.HasAdminAccess(slot, CS2ADMIN_FLAG_CONVARS))
    return;

  if (g_ThisPlugin.LoadWhitelistFile())
    g_ThisPlugin.ReplyToSlot(slot,
                             "[WHITELIST] Reloaded %d entries from disk.\n",
                             g_ThisPlugin.GetEntryCount());
  else
    g_ThisPlugin.ReplyToSlot(
        slot,
        "[WHITELIST] Failed to reload whitelist file (check server log).\n");
}

CON_COMMAND_F(mm_whitelist_list,
              "List all entries in the currently loaded whitelist.",
              FCVAR_GAMEDLL | FCVAR_RELEASE) {
  int slot = context.GetPlayerSlot().Get();
  if (!g_ThisPlugin.HasAdminAccess(slot, CS2ADMIN_FLAG_GENERIC))
    return;

  g_ThisPlugin.PrintListToSlot(slot);
}

CON_COMMAND_F(
    mm_whitelist_add,
    "Add a SteamID (STEAM_0:X:Y or SteamID64) or IP address to the whitelist. "
    "Usage: mm_whitelist_add <steamid|ip>",
    FCVAR_GAMEDLL | FCVAR_RELEASE) {
  int slot = context.GetPlayerSlot().Get();
  if (!g_ThisPlugin.HasAdminAccess(slot, CS2ADMIN_FLAG_UNBAN))
    return;

  if (args.ArgC() < 2) {
    g_ThisPlugin.ReplyToSlot(
        slot, "[WHITELIST] Usage: mm_whitelist_add <steamid|ip>\n");
    return;
  }

  const char *entry = args.Arg(1);
  if (g_ThisPlugin.AddEntry(entry)) {
    g_ThisPlugin.ReplyToSlot(slot, "[WHITELIST] Added '%s'. Saving file...\n",
                             entry);
    g_ThisPlugin.SaveWhitelistFile();
  } else {
    g_ThisPlugin.ReplyToSlot(
        slot, "[WHITELIST] '%s' is already in the whitelist (or is invalid).\n",
        entry);
  }
}

CON_COMMAND_F(mm_whitelist_remove,
              "Remove a SteamID or IP address from the whitelist. "
              "Usage: mm_whitelist_remove <steamid|ip>",
              FCVAR_GAMEDLL | FCVAR_RELEASE) {
  int slot = context.GetPlayerSlot().Get();
  if (!g_ThisPlugin.HasAdminAccess(slot, CS2ADMIN_FLAG_UNBAN))
    return;

  if (args.ArgC() < 2) {
    g_ThisPlugin.ReplyToSlot(
        slot, "[WHITELIST] Usage: mm_whitelist_remove <steamid|ip>\n");
    return;
  }

  const char *entry = args.Arg(1);
  if (g_ThisPlugin.RemoveEntry(entry)) {
    g_ThisPlugin.ReplyToSlot(slot, "[WHITELIST] Removed '%s'. Saving file...\n",
                             entry);
    g_ThisPlugin.SaveWhitelistFile();
  } else {
    g_ThisPlugin.ReplyToSlot(
        slot, "[WHITELIST] '%s' was not found in the whitelist.\n", entry);
  }
}

// Plugin lifecycle

bool CS2WhitelistPlugin::Load(PluginId id, ISmmAPI *ismm, char *error,
                              size_t maxlen, bool late) {
  PLUGIN_SAVEVARS();

  GET_V_IFACE_CURRENT(GetEngineFactory, g_pEngine, IVEngineServer,
                      INTERFACEVERSION_VENGINESERVER);
  GET_V_IFACE_CURRENT(GetEngineFactory, g_pICvar, ICvar,
                      CVAR_INTERFACE_VERSION);
  GET_V_IFACE_ANY(GetServerFactory, g_pServerGameDLL, IServerGameDLL,
                  INTERFACEVERSION_SERVERGAMEDLL);
  GET_V_IFACE_ANY(GetServerFactory, g_pGameClients, IServerGameClients,
                  INTERFACEVERSION_SERVERGAMECLIENTS);

  m_bLateLoaded = late;

  g_SMAPI->AddListener(this, this);

  SH_ADD_HOOK(IServerGameClients, OnClientConnected, g_pGameClients,
              SH_MEMBER(this, &CS2WhitelistPlugin::Hook_OnClientConnected),
              false);
  SH_ADD_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients,
              SH_MEMBER(this, &CS2WhitelistPlugin::Hook_ClientPutInServer),
              true);
  SH_ADD_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients,
              SH_MEMBER(this, &CS2WhitelistPlugin::Hook_ClientDisconnect),
              true);

  g_pCVar = g_pICvar;
  META_CONVAR_REGISTER(FCVAR_RELEASE | FCVAR_GAMEDLL);

  LoadWhitelistFile();

  META_CONPRINTF("[WHITELIST] Plugin loaded (v%s)%s.\n", PLUGIN_FULL_VERSION,
                 late ? " [late]" : "");
  return true;
}

bool CS2WhitelistPlugin::Unload(char *error, size_t maxlen) {
  SH_REMOVE_HOOK(IServerGameClients, OnClientConnected, g_pGameClients,
                 SH_MEMBER(this, &CS2WhitelistPlugin::Hook_OnClientConnected),
                 false);
  SH_REMOVE_HOOK(IServerGameClients, ClientPutInServer, g_pGameClients,
                 SH_MEMBER(this, &CS2WhitelistPlugin::Hook_ClientPutInServer),
                 true);
  SH_REMOVE_HOOK(IServerGameClients, ClientDisconnect, g_pGameClients,
                 SH_MEMBER(this, &CS2WhitelistPlugin::Hook_ClientDisconnect),
                 true);

  META_CONPRINTF("[WHITELIST] Plugin unloaded.\n");
  return true;
}

void CS2WhitelistPlugin::AllPluginsLoaded() {
  m_pCS2Admin = static_cast<ICS2Admin *>(
      g_SMAPI->MetaFactory(CS2ADMIN_INTERFACE, nullptr, nullptr));

  if (m_pCS2Admin)
    META_CONPRINTF("[WHITELIST] mm-cs2admin interface acquired! "
                   "admin immunity and in-game commands enabled.\n");
  else
    META_CONPRINTF("[WHITELIST] mm-cs2admin not loaded! "
                   "admin commands restricted to server console.\n");
}

void CS2WhitelistPlugin::OnLevelInit(char const *pMapName,
                                     char const *pMapEntities,
                                     char const *pOldLevel,
                                     char const *pLandmarkName, bool loadGame,
                                     bool background) {
  LoadWhitelistFile();
}

// Hook: OnClientConnected - record IP and xuid before auth
void CS2WhitelistPlugin::Hook_OnClientConnected(
    CPlayerSlot slot, const char *pszName, uint64 xuid,
    const char *pszNetworkID, const char *pszAddress, bool bFakePlayer) {
  int idx = slot.Get();
  if (idx < 0 || idx > WL_MAXPLAYERS)
    return;

  m_players[idx].xuid = xuid;
  m_players[idx].fakePlayer = bFakePlayer;
  m_players[idx].ip = StripPort(pszAddress ? pszAddress : "");
}

// Hook: ClientPutInServer - enforce whitelist once auth is confirmed
void CS2WhitelistPlugin::Hook_ClientPutInServer(CPlayerSlot slot,
                                                char const *pszName, int type,
                                                uint64 xuid) {
  int idx = slot.Get();
  if (idx < 0 || idx > WL_MAXPLAYERS)
    return;

  if (m_players[idx].fakePlayer)
    return;

  if (!cv_enable.Get())
    return;

  if (cv_immunity.Get() && m_pCS2Admin && m_pCS2Admin->IsAdmin(idx)) {
    META_CONPRINTF("[WHITELIST] Slot %d (%s) has admin immunity.\n", idx,
                   pszName ? pszName : "?");
    return;
  }

  if (IsPlayerWhitelisted(idx))
    return;

  const char *msg = cv_kickmessage.Get().Get();
  if (g_pEngine) {
    char kickmsg[512];
    snprintf(kickmsg, sizeof(kickmsg), "[WHITELIST] %s\n", msg);
    g_pEngine->ClientPrintf(slot, kickmsg);

    META_CONPRINTF(
        "[WHITELIST] Kicking slot %d (%s, ip=%s): not whitelisted.\n", idx,
        pszName ? pszName : "?", m_players[idx].ip.c_str());

    g_pEngine->DisconnectClient(slot, NETWORK_DISCONNECT_KICKED, msg);
  }
}

// Hook: ClientDisconnect - clean up player state
void CS2WhitelistPlugin::Hook_ClientDisconnect(
    CPlayerSlot slot, ENetworkDisconnectionReason reason, const char *pszName,
    uint64 xuid, const char *pszNetworkID) {
  int idx = slot.Get();
  if (idx < 0 || idx > WL_MAXPLAYERS)
    return;

  m_players[idx] = PlayerInfo{};
}

// Whitelist file I/O

bool CS2WhitelistPlugin::LoadWhitelistFile() {
  m_whitelist.clear();

  std::string path = GetWhitelistFilePath();
  std::ifstream file(path);
  if (!file.is_open()) {
    META_CONPRINTF(
        "[WHITELIST] Could not open whitelist file: %s\n"
        "[WHITELIST] Create the file with one SteamID or IP per line.\n",
        path.c_str());
    return false;
  }

  int count = 0;
  std::string line;
  while (std::getline(file, line)) {
    std::string entry = NormalizeEntry(line.c_str());
    if (!entry.empty()) {
      m_whitelist.insert(entry);
      ++count;
    }
  }

  META_CONPRINTF("[WHITELIST] Loaded %d entries from %s.\n", count,
                 path.c_str());
  return true;
}

bool CS2WhitelistPlugin::SaveWhitelistFile() {
  std::string path = GetWhitelistFilePath();
  std::ofstream file(path);
  if (!file.is_open()) {
    META_CONPRINTF("[WHITELIST] Could not write whitelist file: %s\n",
                   path.c_str());
    return false;
  }

  file << "// CS2 Whitelist - managed by cs2whitelist plugin\n"
       << "// One entry per line: STEAM_0:X:Y, SteamID64, or IPv4 address\n"
       << "// Lines starting with // or # are comments\n\n";

  for (const auto &e : m_whitelist)
    file << e << "\n";

  return true;
}

bool CS2WhitelistPlugin::AddEntry(const char *entry) {
  std::string normalized = NormalizeEntry(entry);
  if (normalized.empty())
    return false;

  return m_whitelist.insert(std::move(normalized)).second;
}

bool CS2WhitelistPlugin::RemoveEntry(const char *entry) {
  std::string normalized = NormalizeEntry(entry);
  if (normalized.empty())
    return false;

  return m_whitelist.erase(normalized) > 0;
}

// Whitelist check

bool CS2WhitelistPlugin::IsPlayerWhitelisted(int slot) const {
  if (slot < 0 || slot > WL_MAXPLAYERS)
    return false;

  const PlayerInfo &p = m_players[slot];

  if (!p.ip.empty() && m_whitelist.count(p.ip))
    return true;

  if (p.xuid != 0) {
    if (m_whitelist.count(SteamID64ToAuthId(p.xuid)))
      return true;

    char buf[32];
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(p.xuid));
    if (m_whitelist.count(buf))
      return true;
  }

  return false;
}

// Output helpers

void CS2WhitelistPlugin::PrintListToSlot(int slot) const {
  ReplyToSlot(slot, "[WHITELIST] %d entries:\n",
              static_cast<int>(m_whitelist.size()));
  for (const auto &e : m_whitelist)
    ReplyToSlot(slot, "  %s\n", e.c_str());
}

void CS2WhitelistPlugin::ReplyToSlot(int slot, const char *fmt, ...) const {
  char buf[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buf, sizeof(buf), fmt, args);
  va_end(args);

  if (slot < 0)
    META_CONPRINTF("%s", buf);
  else if (g_pEngine)
    g_pEngine->ClientPrintf(CPlayerSlot(slot), buf);
}

// Admin access check

bool CS2WhitelistPlugin::HasAdminAccess(int slot, uint32_t flag) const {
  if (slot < 0)
    return true;

  if (!m_pCS2Admin) {
    if (g_pEngine)
      g_pEngine->ClientPrintf(
          CPlayerSlot(slot),
          "[WHITELIST] mm-cs2admin is not loaded; "
          "this command can only be used from the server console.\n");
    return false;
  }

  if (m_pCS2Admin->HasFlag(slot, CS2ADMIN_FLAG_ROOT))
    return true;

  if (!m_pCS2Admin->HasFlag(slot, flag)) {
    g_pEngine->ClientPrintf(
        CPlayerSlot(slot),
        "[WHITELIST] You do not have permission to use this command.\n");
    return false;
  }

  return true;
}

// Static helpers

std::string CS2WhitelistPlugin::SteamID64ToAuthId(uint64_t id64) {
  uint32_t accountId = static_cast<uint32_t>(id64 & 0xFFFFFFFF);
  uint32_t y = accountId & 1 u;
  uint32_t z = accountId >> 1 u;
  char buf[32];
  snprintf(buf, sizeof(buf), "STEAM_0:%u:%u", y, z);
  return buf;
}

std::string CS2WhitelistPlugin::NormalizeEntry(const char *input) {
  if (!input || !input[0])
    return {};

  std::string s(input);

  // Strip inline comment (// ...)
  auto cpos = s.find("//");
  if (cpos != std::string::npos)
    s = s.substr(0, cpos);

  // Trim surrounding whitespace and CR
  auto trim = [](std::string &str) {
    const char *ws = " \t\r\n";
    str.erase(0, str.find_first_not_of(ws));
    auto last = str.find_last_not_of(ws);
    if (last != std::string::npos)
      str.erase(last + 1);
    else
      str.clear();
  };
  trim(s);

  if (s.empty() || s[0] == '#')
    return {};

  // STEAM_X:Y:Z -> STEAM_0:Y:Z (case-insensitive prefix)
  if (s.size() > 6) {
    char prefix[7] = {};
    for (int i = 0; i < 6; ++i)
      prefix[i] = static_cast<char>(toupper(static_cast<unsigned char>(s[i])));

    if (memcmp(prefix, "STEAM_", 6) == 0) {
      auto colon = s.find(':', 6);
      if (colon != std::string::npos)
        return "STEAM_0:" + s.substr(colon + 1);
      return s;
    }
  }

  // SteamID64: all digits, 15-20 chars
  bool allDigits = !s.empty();
  for (char c : s)
    if (!isdigit(static_cast<unsigned char>(c))) {
      allDigits = false;
      break;
    }

  if (allDigits && s.size() >= 15 && s.size() <= 20)
    return s;

  // IP address (or anything else)
  return s;
}

std::string CS2WhitelistPlugin::StripPort(const char *addr) {
  if (!addr || !addr[0])
    return {};

  std::string s(addr);
  auto colon = s.rfind(':');
  if (colon != std::string::npos)
    return s.substr(0, colon);
  return s;
}
