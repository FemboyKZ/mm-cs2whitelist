#pragma once
#include "mmu/config_blocks.h"

#include <string>
#include <vector>
#include <cstdint>

struct WLConfig
{
	// [Config] section
	bool enable = true;
	bool immunity = true;
	std::string filename = "whitelist.txt";
	int logMode = 0;                    // 0=off  1=always  2=once per player per map
	std::string defaultLanguage = "en"; // phrase-file key used when a client's language is unknown

	mmu::config::LogBlock log;

	// [Database] section
	bool dbEnabled = false;
	mmu::config::DatabaseBlock database = mmu::config::DatabaseBlock::Defaults("cs2whitelist", "addons/cs2whitelist/whitelist.db", "cs2wl");

	// [SteamGroups] section
	bool sgEnabled = false;
	std::string sgMethod = "xml"; // "xml" or "api"
	std::string sgApiKey;
	float sgTimeout = 5.0f;
	std::vector<uint64_t> sgGroupIds;
};

// Parse cfg/cs2whitelist/core.cfg into out.
// Returns true if the file was successfully opened and parsed.
bool WL_LoadConfig(const char *filePath, WLConfig &out);

extern WLConfig g_WLConfig;
