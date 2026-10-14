#include "whitelist_manager.h"
#include "utils/log.h"
#include "utils/str.h"
#include "game/players.h"
#include "steamgroup/steamgroup_manager.h"
#include "utils/utils.h"
#include "db/wl_database.h"

#include <algorithm>
#include <engine2/isource2server.h>
#include <engine2/ivengineserver2.h>
#include <fstream>
#include <cctype>
#include <cstring>
#include <stdexcept>

CConVar<bool> cv_enable("mm_whitelist_enabled", FCVAR_RELEASE | FCVAR_GAMEDLL, "Enable the server whitelist (1) or disable it (0).", true);

CConVar<bool> cv_immunity("mm_whitelist_immunity", FCVAR_RELEASE | FCVAR_GAMEDLL,
						  "Skip the whitelist check for players with any cs2admin flag "
						  "(requires mm-cs2admin).",
						  true);

CConVar<CUtlString> cv_filename("mm_whitelist_filename", FCVAR_RELEASE | FCVAR_GAMEDLL,
								"Whitelist file name, relative to <game>/cfg/cs2whitelist/. "
								"Path separators are stripped to prevent directory traversal.",
								"whitelist.txt");

CConVar<int> cv_log("mm_whitelist_log_rejects", FCVAR_RELEASE | FCVAR_GAMEDLL,
					"Log failed join attempts to the plugin's log. "
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
			numStart += std::strspn(numStart, " \t");
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

enum class LineKind
{
	Blank,
	Group,
	Entry,
	Invalid,
};

static LineKind ParseLine(const std::string &line, uint64_t &groupId, std::string &entry)
{
	// Some Windows editors lead the file with a UTF-8 byte-order mark.
	std::string text = line.compare(0, 3, "\xEF\xBB\xBF") == 0 ? line.substr(3) : line;
	text = str::Trim(text.substr(0, (std::min)(text.find_first_of(";#"), text.find("//"))));
	if (text.empty())
	{
		return LineKind::Blank;
	}
	groupId = ParseGroupEntry(text);
	if (groupId != 0)
	{
		return LineKind::Group;
	}
	entry = NormalizeEntry(text.c_str());
	return entry.empty() ? LineKind::Invalid : LineKind::Entry;
}

// Appended, so the lines already there keep their comments and order.
static bool AppendLine(const std::string &text)
{
	const std::string path = GetWhitelistFilePath();

	// A last line without a newline would run into this one.
	bool endsInNewline = true;
	{
		std::ifstream existing(path, std::ios::binary | std::ios::ate);
		if (existing.is_open() && existing.tellg() > 0)
		{
			existing.seekg(-1, std::ios::end);
			endsInNewline = existing.get() == '\n';
		}
	}

	std::ofstream file(path, std::ios::app);
	if (!file.is_open())
	{
		MMU_LOG_WARN("Could not write whitelist file: %s\n", path.c_str());
		return false;
	}
	file << (endsInNewline ? "" : "\n") << text << "\n";
	return true;
}

// Drops the lines holding `groupId`, or `entry` when it is 0.
static bool RemoveLines(uint64_t groupId, const std::string &entry)
{
	const std::string path = GetWhitelistFilePath();

	// Binary, so a line keeps the ending it has.
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
	{
		return false;
	}

	std::string kept;
	std::string line;
	while (std::getline(in, line))
	{
		uint64_t lineGroup = 0;
		std::string lineEntry;
		const LineKind kind = ParseLine(line, lineGroup, lineEntry);
		const bool removed = groupId != 0 ? (kind == LineKind::Group && lineGroup == groupId) : (kind == LineKind::Entry && lineEntry == entry);
		if (!removed)
		{
			kept += line + "\n";
		}
	}
	in.close();

	// In place: a rename over it would swap a symlinked file for a copy, and fails on one mounted into a container.
	std::ofstream out(path, std::ios::binary | std::ios::trunc);
	out << kept;
	out.close();
	if (out.fail())
	{
		MMU_LOG_WARN("Could not write whitelist file: %s\n", path.c_str());
		return false;
	}
	return true;
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
	int lineNumber = 0;
	std::string line;
	while (std::getline(file, line))
	{
		lineNumber++;
		uint64_t groupId = 0;
		std::string entry;
		switch (ParseLine(line, groupId, entry))
		{
			case LineKind::Group:
				m_fileGroupIds.push_back(groupId);
				++groupCount;
				break;
			case LineKind::Entry:
				m_whitelist.insert(entry);
				m_fileEntries.insert(entry);
				++count;
				break;
			case LineKind::Invalid:
				MMU_LOG_WARN("Line %d of %s is not a SteamID, an IPv4 address or a group, skipped.\n", lineNumber, path.c_str());
				break;
			case LineKind::Blank:
				break;
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

static const char *const kNotSaved = "The whitelist file could not be written.";

bool WLManager::AddEntry(const char *entry, const char **problem)
{
	const std::string text = str::Trim(entry ? entry : "");

	// A group belongs in the file's group list, not the entry set, and its members only match once they are fetched.
	uint64_t groupId = ParseGroupEntry(text);
	if (groupId != 0)
	{
		if (std::find(m_fileGroupIds.begin(), m_fileGroupIds.end(), groupId) != m_fileGroupIds.end())
		{
			return false;
		}
		m_fileGroupIds.push_back(groupId);
		const bool saved = AppendLine("GROUP:" + std::to_string(groupId));
		if (problem && !saved)
		{
			*problem = kNotSaved;
		}
		else if (problem && !g_SteamGroupManager.IsEnabled())
		{
			*problem = "Steam groups are disabled in core.cfg, so this group lets nobody in.";
		}
		g_SteamGroupManager.FetchGroups();
		ClearBlacklistCache();
		return true;
	}

	std::string normalized = NormalizeEntry(text.c_str());
	if (normalized.empty() || !m_whitelist.insert(normalized).second)
	{
		return false;
	}
	m_fileEntries.insert(normalized);
	if (!AppendLine(normalized) && problem)
	{
		*problem = kNotSaved;
	}
	m_dbRemovals.erase(normalized);
	if (g_WLDatabase.IsConnected())
	{
		g_WLDatabase.AddEntry(normalized);
		m_dbEntries.insert(normalized);
	}
	return true;
}

bool WLManager::RemoveEntry(const char *entry, const char **problem)
{
	const std::string text = str::Trim(entry ? entry : "");

	uint64_t groupId = ParseGroupEntry(text);
	if (groupId != 0)
	{
		auto removed = std::remove(m_fileGroupIds.begin(), m_fileGroupIds.end(), groupId);
		if (removed == m_fileGroupIds.end())
		{
			return false;
		}
		m_fileGroupIds.erase(removed, m_fileGroupIds.end());
		if (!RemoveLines(groupId, "") && problem)
		{
			*problem = kNotSaved;
		}
		g_SteamGroupManager.FetchGroups();
		// Otherwise members let in by that group stay let in until the map changes.
		ClearWhitelistCache();
		return true;
	}

	std::string normalized = NormalizeEntry(text.c_str());
	if (normalized.empty() || m_whitelist.erase(normalized) == 0)
	{
		return false;
	}
	if (m_fileEntries.erase(normalized) > 0 && !RemoveLines(0, normalized) && problem)
	{
		*problem = kNotSaved;
	}
	// Or LoadFile would bring it back from the last database load.
	m_dbEntries.erase(normalized);
	// Its rows are deleted by a load, which sees how they are written. With no connection, by the next one.
	if (g_WLDatabase.IsEnabled())
	{
		m_dbRemovals.insert(normalized);
	}
	if (g_WLDatabase.IsConnected())
	{
		LoadDbEntries(nullptr);
	}
	// Otherwise a removed player stays let in by the whitelist cache until the map changes.
	ClearWhitelistCache();
	return true;
}

void WLManager::LoadDbEntries(std::function<void(int count)> done)
{
	g_WLDatabase.LoadEntries(
		[this, done](bool ok, const std::unordered_set<std::string> &rows)
		{
			if (ok)
			{
				SetDbEntries(rows);
			}
			else
			{
				MMU_LOG_WARN("The database's entries could not be read, those of the last load stay in place.\n");
			}
			if (done)
			{
				done(ok ? static_cast<int>(m_dbEntries.size()) : -1);
			}
		});
}

void WLManager::SetDbEntries(const std::unordered_set<std::string> &rows)
{
	m_dbEntries.clear();
	std::unordered_set<std::string> stillThere;
	for (const std::string &row : rows)
	{
		std::string normalized = NormalizeEntry(row.c_str());
		if (m_dbRemovals.count(normalized))
		{
			g_WLDatabase.RemoveEntry(row);
			stillThere.insert(std::move(normalized));
		}
		else if (!normalized.empty())
		{
			m_dbEntries.insert(std::move(normalized));
		}
	}
	// Kept until a load comes back without the row, one sent before the delete still has it.
	m_dbRemovals = std::move(stillThere);

	// Rebuilt rather than added to, a row deleted from the database has to stop granting access.
	m_whitelist = m_fileEntries;
	m_whitelist.insert(m_dbEntries.begin(), m_dbEntries.end());

	// A player let in by a row that is gone now is still in the whitelist cache.
	ClearWhitelistCache();
}

bool WLManager::IsPlayerWhitelisted(int slot) const
{
	const mmu::Player *p = mmu::players::Get(slot);
	if (!p)
	{
		return false;
	}

	if (!p->ip.empty() && m_whitelist.count(p->ip))
	{
		return true;
	}

	// Until Steam confirms it, the SteamID is only what the client claimed.
	return p->authenticated && p->steamid64 != 0 && m_whitelist.count(SteamID64ToAuthId(p->steamid64)) > 0;
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
	for (uint64_t groupId : m_fileGroupIds)
	{
		ReplyToSlot(slot, "  GROUP:%llu\n", static_cast<unsigned long long>(groupId));
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

	MMU_LOG_INFO("Kick: \"%s\" xuid=%llu authid=%s ip=%s\n", name ? name : "?", static_cast<unsigned long long>(xuid), authid.c_str(), ip ? ip : "?");
}
