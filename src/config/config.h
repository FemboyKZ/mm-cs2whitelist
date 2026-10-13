#ifndef _INCLUDE_WL_CONFIG_H_
#define _INCLUDE_WL_CONFIG_H_

#include "utils/config_blocks.h"

#include <string>
#include <vector>
#include <cstdint>

struct GeneralCfg : mmu::config::GeneralBlock
{
	bool enabled = true;
	bool immunity = true;
	std::string filename = "whitelist.txt";
	int logRejects = 0; // 0=off  1=always  2=once per player per map
};

struct SteamGroupsCfg
{
	bool enabled = false;
	std::string mode = "xml"; // "xml" or "api"
	std::string steamApiKey;
	float timeout = 5.0f;
	std::vector<uint64_t> groupIds;
};

struct WLConfig
{
	GeneralCfg general;
	SteamGroupsCfg steamGroups;

	bool databaseEnabled = false;
	mmu::config::DatabaseBlock database =
		mmu::config::DatabaseBlock::Defaults("cs2whitelist", "addons/cs2whitelist/data/cs2whitelist.db", "cs2wl", "addons/cs2whitelist/whitelist.db");
	mmu::config::LogBlock log;
};

// Parse cfg/cs2whitelist/core.cfg into out.
// Returns true if the file was successfully opened and parsed.
bool WL_LoadConfig(const char *filePath, WLConfig &out);

extern WLConfig g_WLConfig;

#endif // _INCLUDE_WL_CONFIG_H_
