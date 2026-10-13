#include "config.h"

#include <fstream>
#include <string>
#include <algorithm>
#include <cstdlib>
#include <cstdint>
#include <stdexcept>
#include <utility>

#include "utils/str.h"
#include "utils/kv_parser.h"
#include "utils/log.h"

WLConfig g_WLConfig;

static void OnKV(const std::string &section, const std::string &key, const std::string &value, void *userdata)
{
	WLConfig *cfg = static_cast<WLConfig *>(userdata);
	std::string sec = str::ToLower(section);
	std::string k = str::ToLower(key);
	bool known = true;

	// "config" and "enable" are the old names.
	if (sec == "general" || sec == "config")
	{
		if (k == "enabled" || k == "enable")
		{
			cfg->enabled = (value != "0");
		}
		else if (k == "immunity")
		{
			cfg->immunity = (value != "0");
		}
		else if (k == "filename")
		{
			cfg->filename = value;
		}
		else if (k == "logrejects" || k == "log")
		{
			cfg->logRejects = std::atoi(value.c_str());
		}
		else if (mmu::config::ApplyLogKey(cfg->log, k, value))
		{
			// consumed
		}
		else
		{
			known = mmu::config::ApplyGeneralKey(cfg->general, k, value);
		}
	}
	else if (sec == "steamgroups")
	{
		if (k == "enabled")
		{
			cfg->sgEnabled = (value != "0");
		}
		else if (k == "mode" || k == "method")
		{
			cfg->sgMode = str::ToLower(value);
		}
		else if (k == "steamapikey" || k == "apikey")
		{
			cfg->sgSteamApiKey = value;
		}
		else if (k == "timeout")
		{
			cfg->sgTimeout = static_cast<float>(std::atof(value.c_str()));
		}
		else
		{
			known = false;
		}
	}
	else if (sec == "groups")
	{
		bool allDigits = !value.empty();
		for (char c : value)
		{
			if (c < '0' || c > '9')
			{
				allDigits = false;
				break;
			}
		}
		if (allDigits)
		{
			uint64_t gid = std::strtoull(value.c_str(), nullptr, 10);
			// Short 32-bit clan ID, promoted to a full group ID64 like the whitelist file and the Steam API response are.
			if (gid != 0 && (gid >> 32) == 0)
			{
				gid = 0x0170000000000000ULL | gid;
			}
			if (gid != 0)
			{
				cfg->sgGroupIds.push_back(gid);
			}
		}
	}
	else if (sec == "database")
	{
		known = mmu::config::ApplyOptionalDatabaseKey(cfg->database, cfg->dbEnabled, k, value);
	}
	else
	{
		known = false;
	}
	if (!known)
	{
		mmu::config::WarnUnknownKey(section, key);
	}
}

bool WL_LoadConfig(const char *filePath, WLConfig &out)
{
	std::ifstream file(filePath);
	if (!file.is_open())
	{
		return false;
	}

	// Expect: "cs2whitelist" { ... }
	kv::Token root = kv::NextToken(file);
	if (root.kind != kv::TokenType::String)
	{
		return false;
	}

	kv::Token brace = kv::NextToken(file);
	if (brace.kind != kv::TokenType::OpenBrace)
	{
		return false;
	}

	// Fresh, so a removed key reverts to its default and the group list starts over.
	WLConfig fresh;
	kv::ParseSection(file, root.value, OnKV, &fresh);
	out = std::move(fresh);

	mmu::config::ApplyLogBlock(out.log);
	return true;
}
