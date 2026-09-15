#include "whitelist_manager.h"
#include "mmu/log.h"
#include "player/player_manager.h"
#include "steamgroup/steamgroup_manager.h"
#include "utils/utils.h"
#include "db/wl_database.h"

#include <algorithm>
#include <eiface.h>
#include <filesystem>
#include <fstream>
#include <ctime>
#include <cctype>
#include <cstring>
#include <stdexcept>

CConVar<bool> cv_enable("mm_whitelist_enable", FCVAR_RELEASE | FCVAR_GAMEDLL, "Enable the server whitelist (1) or disable it (0).", true);

CConVar<bool> cv_immunity("mm_whitelist_immunity", FCVAR_RELEASE | FCVAR_GAMEDLL,
						  "Skip the whitelist check for players with any cs2admin flag "
						  "(requires mm-cs2admin).",
						  true);

CConVar<CUtlString> cv_filename("mm_whitelist_filename", FCVAR_RELEASE | FCVAR_GAMEDLL,
								"Whitelist file name, relative to <game>/cfg/cs2whitelist/. "
								"Path separators are stripped to prevent directory traversal.",
								"whitelist.txt");

CConVar<int> cv_log("mm_whitelist_log", FCVAR_RELEASE | FCVAR_GAMEDLL,
					"Log failed join attempts to console and daily log file. "
					"0=off  1=always  2=once per player per map.",
					0, true, 0, true, 2);

WLManager g_WLManager;

std::string GetWhitelistFilePath()
{
	const char *raw = cv_filename.Get().Get();
	const char *basename = raw;
	for (const char *p = raw; *p; ++p)
	{
		if (*p == '/' || *p == '\\')
		{
			basename = p + 1;
		}
	}

	char path[512];
	snprintf(path, sizeof(path), "%s/cfg/cs2whitelist/%s", g_SMAPI->GetBaseDir(), basename);
	return path;
}

// A line naming a Steam group, written either as GROUP:<id> or as a bare all-digit ID.
// Short 32-bit clan IDs are promoted to full group ID64s.
// Returns 0 for anything else. User SteamID64s always start with 76561197, so there is no ambiguity.
static uint64_t ParseGroupEntry(const std::string &line)
{
	const char *numStart = line.c_str();
	if (line.size() >= 6)
	{
		char upper[7] = {};
		for (int i = 0; i < 6; ++i)
		{
			upper[i] = static_cast<char>(line[i] >= 'a' && line[i] <= 'z' ? line[i] - 32 : line[i]);
		}
		if (std::memcmp(upper, "GROUP:", 6) == 0)
		{
			numStart = line.c_str() + 6;
		}
	}

	if (*numStart == '\0')
	{
		return 0;
	}
	for (const char *p = numStart; *p; ++p)
	{
		if (*p < '0' || *p > '9')
		{
			return 0;
		}
	}

	uint64_t id = std::strtoull(numStart, nullptr, 10);
	if (id == 0)
	{
		return 0;
	}
	if ((id >> 32) == 0)
	{
		id = 0x0170000000000000ULL | id;
	}

	// k_EAccountTypeClan
	return ((id >> 52) & 0xF) == 7 ? id : 0;
}

bool WLManager::LoadFile()
{
	m_whitelist.clear();
	m_fileEntries.clear();
	m_blacklistCache.clear();
	m_whitelistCache.clear();
	m_fileGroupIds.clear();

	// Survives a file reload, the database is a separate source.
	m_whitelist.insert(m_dbEntries.begin(), m_dbEntries.end());

	std::string path = GetWhitelistFilePath();
	std::ifstream file(path);
	if (!file.is_open())
	{
		MMU_LOG_WARN("Could not open whitelist file: %s\n"
					 "[WHITELIST] Create the file with one SteamID or IP per line.\n",
					 path.c_str());
		return false;
	}

	int count = 0;
	int groupCount = 0;
	std::string line;
	while (std::getline(file, line))
	{
		const char *ws = " \t\r\n";
		auto first = line.find_first_not_of(ws);
		if (first == std::string::npos)
		{
			continue;
		}
		std::string trimmed = line.substr(first);

		auto cpos = trimmed.find_first_of(";#");
		if (cpos == std::string::npos)
		{
			cpos = trimmed.find("//");
		}
		else
		{
			auto slashes = trimmed.find("//");
			if (slashes != std::string::npos && slashes < cpos)
			{
				cpos = slashes;
			}
		}
		if (cpos != std::string::npos)
		{
			trimmed = trimmed.substr(0, cpos);
		}
		auto last = trimmed.find_last_not_of(ws);
		if (last == std::string::npos)
		{
			continue;
		}
		trimmed = trimmed.substr(0, last + 1);

		uint64_t groupId = ParseGroupEntry(trimmed);
		if (groupId != 0)
		{
			m_fileGroupIds.push_back(groupId);
			++groupCount;
			continue;
		}

		std::string entry = NormalizeEntry(trimmed.c_str());
		if (!entry.empty())
		{
			m_whitelist.insert(entry);
			m_fileEntries.insert(entry);
			++count;
		}
	}

	if (groupCount > 0)
	{
		MMU_LOG_INFO("Loaded %d entries + %d GROUP entries from %s.\n", count, groupCount, path.c_str());
	}
	else
	{
		MMU_LOG_INFO("Loaded %d entries from %s.\n", count, path.c_str());
	}
	return true;
}

bool WLManager::SaveFile()
{
	std::string path = GetWhitelistFilePath();
	std::ofstream file(path);
	if (!file.is_open())
	{
		MMU_LOG_WARN("Could not write whitelist file: %s\n", path.c_str());
		return false;
	}

	file << "// CS2 Whitelist - managed by cs2whitelist plugin\n"
		 << "// One entry per line: STEAM_0:X:Y, SteamID64, IPv4 address,\n"
		 << "// or groupID64 to whitelist all members of a Steam group.\n"
		 << "// Lines starting with // or # are comments\n\n";

	for (uint64_t gid : m_fileGroupIds)
	{
		file << "GROUP:" << gid << "\n";
	}
	if (!m_fileGroupIds.empty())
	{
		file << "\n";
	}

	for (const auto &e : m_fileEntries)
	{
		file << e << "\n";
	}

	return true;
}

bool WLManager::AddEntry(const char *entry)
{
	std::string normalized = NormalizeEntry(entry);
	if (normalized.empty())
	{
		return false;
	}

	// A group belongs in the file's group list, not the entry set, and its members only match once they are fetched.
	uint64_t groupId = ParseGroupEntry(normalized);
	if (groupId != 0)
	{
		if (std::find(m_fileGroupIds.begin(), m_fileGroupIds.end(), groupId) != m_fileGroupIds.end())
		{
			return false;
		}
		m_fileGroupIds.push_back(groupId);
		g_SteamGroupManager.FetchGroups();
		ClearBlacklistCache();
		return true;
	}

	m_fileEntries.insert(normalized);
	bool inserted = m_whitelist.insert(normalized).second;
	if (inserted && g_WLDatabase.IsConnected())
	{
		g_WLDatabase.AddEntry(normalized);
	}
	if (inserted)
	{
		// A player kicked earlier this map sits in the blacklist cache, which is checked before the whitelist.
		// An entry can be an IP as well as a SteamID, so drop the whole cache rather than guess which players it covers.
		ClearBlacklistCache();
	}
	return inserted;
}

bool WLManager::RemoveEntry(const char *entry)
{
	std::string normalized = NormalizeEntry(entry);
	if (normalized.empty())
	{
		return false;
	}

	uint64_t groupId = ParseGroupEntry(normalized);
	if (groupId != 0)
	{
		auto it = std::find(m_fileGroupIds.begin(), m_fileGroupIds.end(), groupId);
		if (it == m_fileGroupIds.end())
		{
			return false;
		}
		m_fileGroupIds.erase(it);
		g_SteamGroupManager.FetchGroups();
		// Otherwise members let in by that group stay let in until the map changes.
		ClearWhitelistCache();
		return true;
	}

	m_fileEntries.erase(normalized);
	bool erased = m_whitelist.erase(normalized) > 0;
	if (erased && g_WLDatabase.IsConnected())
	{
		g_WLDatabase.RemoveEntry(normalized);
	}
	if (erased)
	{
		// Otherwise a removed player stays let in by the whitelist cache until the map changes.
		ClearWhitelistCache();
	}
	return erased;
}

std::unordered_set<std::string> &WLManager::BeginDbLoad()
{
	m_dbLoading.clear();
	return m_dbLoading;
}

void WLManager::FinishDbLoad()
{
	m_dbEntries.clear();
	for (const std::string &row : m_dbLoading)
	{
		std::string normalized = NormalizeEntry(row.c_str());
		if (!normalized.empty())
		{
			m_dbEntries.insert(std::move(normalized));
		}
	}
	m_dbLoading.clear();

	m_whitelist.insert(m_dbEntries.begin(), m_dbEntries.end());

	// A player kicked before the rows arrived is still in the blacklist cache, which is checked before the whitelist.
	ClearBlacklistCache();
}

bool WLManager::IsPlayerWhitelisted(int slot) const
{
	const PlayerInfo *p = g_WLPlayerManager.GetPlayer(slot);
	if (!p)
	{
		return false;
	}

	if (!p->ip.empty() && m_whitelist.count(p->ip))
	{
		return true;
	}

	if (p->xuid != 0)
	{
		if (m_whitelist.count(SteamID64ToAuthId(p->xuid)))
		{
			return true;
		}

		char buf[32];
		snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(p->xuid));
		if (m_whitelist.count(buf))
		{
			return true;
		}
	}

	return false;
}

bool WLManager::IsEntryWhitelisted(const char *entry) const
{
	std::string normalized = NormalizeEntry(entry);
	if (normalized.empty())
	{
		return false;
	}
	return m_whitelist.count(normalized) > 0;
}

void WLManager::PrintList(int slot) const
{
	ReplyToSlotT(slot, "%d entries:", static_cast<int>(m_whitelist.size()));
	for (const auto &e : m_whitelist)
	{
		ReplyToSlot(slot, "  %s\n", e.c_str());
	}
}

bool WLManager::IsBlacklisted(uint64_t xuid) const
{
	return xuid != 0 && m_blacklistCache.count(xuid) > 0;
}

void WLManager::AddToBlacklistCache(uint64_t xuid)
{
	if (xuid != 0)
	{
		m_blacklistCache.insert(xuid);
	}
}

void WLManager::ClearBlacklistCache()
{
	m_blacklistCache.clear();
}

bool WLManager::IsWhitelistCached(uint64_t xuid) const
{
	return xuid != 0 && m_whitelistCache.count(xuid) > 0;
}

void WLManager::AddToWhitelistCache(uint64_t xuid)
{
	if (xuid != 0)
	{
		m_whitelistCache.insert(xuid);
	}
}

void WLManager::ClearWhitelistCache()
{
	m_whitelistCache.clear();
}

void WLLogKick(const char *name, uint64_t xuid, const char *ip, bool alreadyCached)
{
	int logMode = cv_log.Get();
	if (logMode == 0)
	{
		return;
	}
	// mode 2: only log first time (alreadyCached == false means first rejection)
	if (logMode == 2 && alreadyCached)
	{
		return;
	}

	std::string authid = xuid ? SteamID64ToAuthId(xuid) : "unknown";

	time_t now = time(nullptr);
	struct tm tm_info;
#ifdef _WIN32
	localtime_s(&tm_info, &now);
#else
	localtime_r(&now, &tm_info);
#endif
	char timebuf[32];
	strftime(timebuf, sizeof(timebuf), "%Y-%m-%d %H:%M:%S", &tm_info);

	char datebuf[16];
	strftime(datebuf, sizeof(datebuf), "%Y-%m-%d", &tm_info);

	const char *safeName = name ? name : "?";
	const char *safeIp = ip ? ip : "?";

	MMU_LOG_INFO("[%s] Kick: \"%s\" xuid=%llu authid=%s ip=%s\n", timebuf, safeName, static_cast<unsigned long long>(xuid), authid.c_str(), safeIp);

	char logDir[512];
	snprintf(logDir, sizeof(logDir), "%s/addons/cs2whitelist/logs", g_SMAPI->GetBaseDir());

	std::error_code ec;
	std::filesystem::create_directories(logDir, ec);

	char logPath[600];
	snprintf(logPath, sizeof(logPath), "%s/%s.log", logDir, datebuf);

	std::ofstream f(logPath, std::ios::app);
	if (f.is_open())
	{
		f << "[" << timebuf << "] Kick: \"" << safeName << "\" xuid=" << xuid << " authid=" << authid << " ip=" << safeIp << "\n";
	}
}
