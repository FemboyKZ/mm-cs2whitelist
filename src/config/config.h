#ifndef _INCLUDE_WL_CONFIG_H_
#define _INCLUDE_WL_CONFIG_H_

#include "utils/config_blocks.h"

#include <string>
#include <vector>
#include <cstdint>

struct WLConfig
{
	// [General] section
	bool enabled = true;
	bool immunity = true;
	std::string filename = "whitelist.txt";
	int logRejects = 0; // 0=off  1=always  2=once per player per map
	mmu::config::GeneralBlock general;

	mmu::config::LogBlock log;

	// [Database] section
	bool dbEnabled = false;
	mmu::config::DatabaseBlock database =
		mmu::config::DatabaseBlock::Defaults("cs2whitelist", "addons/cs2whitelist/data/cs2whitelist.db", "cs2wl", "addons/cs2whitelist/whitelist.db");

	// [SteamGroups] section
	bool sgEnabled = false;
	std::string sgMode = "xml"; // "xml" or "api"
	std::string sgSteamApiKey;
	float sgTimeout = 5.0f;
	std::vector<uint64_t> sgGroupIds;
};

// Parse cfg/cs2whitelist/core.cfg into out.
// Returns true if the file was successfully opened and parsed.
bool WL_LoadConfig(const char *filePath, WLConfig &out);

extern WLConfig g_WLConfig;

#endif // _INCLUDE_WL_CONFIG_H_
