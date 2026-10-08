#ifndef _INCLUDE_WL_WHITELIST_MANAGER_H_
#define _INCLUDE_WL_WHITELIST_MANAGER_H_

#include "common.h"
#include <functional>
#include <string>
#include <vector>
#include <unordered_set>
#include <cstdint>
#include <tier1/convar.h>

// ConVar declarations
extern CConVar<bool> cv_enable;
extern CConVar<bool> cv_immunity;
extern CConVar<CUtlString> cv_filename;
extern CConVar<int> cv_log;

// Build the absolute path to the active whitelist file.
std::string GetWhitelistFilePath();

// Log a kick, if cv_log > 0.
// cv_log == 1: always  cv_log == 2: only first time per player per map.
// Pass alreadyCached=true when the player was already in the blacklist cache.
void WLLogKick(const char *name, uint64_t xuid, const char *ip, bool alreadyCached);

class WLManager
{
public:
	bool LoadFile();

	// Both write the change to the file, and to the database when it is connected.
	bool AddEntry(const char *entry);
	bool RemoveEntry(const char *entry);

	bool IsPlayerWhitelisted(int slot) const;
	bool IsEntryWhitelisted(const char *entry) const;

	int GetEntryCount() const
	{
		return static_cast<int>(m_whitelist.size());
	}

	void PrintList(int slot) const;

	// Blacklist cache: tracks xuids rejected this map to skip checking repeatedly on reconnects.
	bool IsBlacklisted(uint64_t xuid) const;
	void AddToBlacklistCache(uint64_t xuid);
	void ClearBlacklistCache();

	int GetBlacklistCacheCount() const
	{
		return static_cast<int>(m_blacklistCache.size());
	}

	// Whitelist cache: tracks xuids confirmed whitelisted this map.
	// Allows in on reconnect (and future async checks like Steam groups).
	bool IsWhitelistCached(uint64_t xuid) const;
	void AddToWhitelistCache(uint64_t xuid);
	void ClearWhitelistCache();

	int GetWhitelistCacheCount() const
	{
		return static_cast<int>(m_whitelistCache.size());
	}

	// `done` hears how many there are, or -1 when they could not be read and the last load's stay.
	void LoadDbEntries(std::function<void(int count)> done);

	// Group IDs found in the whitelist file
	const std::vector<uint64_t> &GetFileGroupIds() const
	{
		return m_fileGroupIds;
	}

private:
	// Normalizes the rows, other tools write them too.
	void SetDbEntries(const std::unordered_set<std::string> &rows);

	// Everything that grants access, the file's entries plus whatever the DB merged in.
	std::unordered_set<std::string> m_whitelist;
	// The file's own, kept apart so a database load can replace the database's and leave these.
	std::unordered_set<std::string> m_fileEntries;
	// Rows of the last finished database load, re-applied by LoadFile so a file reload does not drop DB only entries.
	std::unordered_set<std::string> m_dbEntries;
	std::unordered_set<uint64_t> m_blacklistCache;
	std::unordered_set<uint64_t> m_whitelistCache;
	std::vector<uint64_t> m_fileGroupIds;
};

extern WLManager g_WLManager;

#endif // _INCLUDE_WL_WHITELIST_MANAGER_H_
