#include "utils.h"
#include "common.h"
#include "ics2admin.h"

#include <cctype>
#include <cstring>
#include <algorithm>
#include <cstdarg>
#include <cstdio>

std::string SteamID64ToAuthId(uint64_t id64)
{
	uint32_t accountId = static_cast<uint32_t>(id64 & 0xFFFFFFFF);
	uint32_t y = accountId & 1u;
	uint32_t z = accountId >> 1u;
	char buf[32];
	snprintf(buf, sizeof(buf), "STEAM_0:%u:%u", y, z);
	return buf;
}

std::string NormalizeEntry(const char *input)
{
	if (!input || !input[0])
	{
		return {};
	}

	std::string s(input);

	// Strip inline comment (// ...)
	auto cpos = s.find("//");
	if (cpos != std::string::npos)
	{
		s = s.substr(0, cpos);
	}

	// Trim surrounding whitespace and CR
	const char *ws = " \t\r\n";
	auto first = s.find_first_not_of(ws);
	if (first == std::string::npos)
	{
		return {};
	}
	s = s.substr(first, s.find_last_not_of(ws) - first + 1);

	if (s.empty() || s[0] == '#')
	{
		return {};
	}

	// STEAM_X:Y:Z -> STEAM_0:Y:Z (case insensitive prefix)
	if (s.size() > 6)
	{
		char prefix[7] = {};
		for (int i = 0; i < 6; ++i)
		{
			prefix[i] = static_cast<char>(toupper(static_cast<unsigned char>(s[i])));
		}

		if (memcmp(prefix, "STEAM_", 6) == 0)
		{
			auto colon = s.find(':', 6);
			if (colon != std::string::npos)
			{
				return "STEAM_0:" + s.substr(colon + 1);
			}
			return s;
		}
	}

	// SteamID64: all digits, 15-20 chars
	bool allDigits = !s.empty();
	for (char c : s)
	{
		if (!isdigit(static_cast<unsigned char>(c)))
		{
			allDigits = false;
			break;
		}
	}

	if (allDigits && s.size() >= 15 && s.size() <= 20)
	{
		return s;
	}

	return s;
}

std::string StripPort(const char *addr)
{
	if (!addr || !addr[0])
	{
		return {};
	}

	std::string s(addr);
	auto colon = s.rfind(':');
	if (colon != std::string::npos)
	{
		return s.substr(0, colon);
	}
	return s;
}

void ReplyToSlot(int slot, const char *fmt, ...)
{
	char buf[512];
	va_list args;
	va_start(args, fmt);
	vsnprintf(buf, sizeof(buf), fmt, args);
	va_end(args);

	if (slot < 0)
	{
		META_CONPRINTF("%s", buf);
	}
	else if (g_pEngine)
	{
		g_pEngine->ClientPrintf(CPlayerSlot(slot), buf);
	}
}

bool HasAdminAccess(int slot, uint32_t flag)
{
	if (slot < 0)
	{
		return true;
	}

	if (!g_pCS2Admin)
	{
		if (g_pEngine)
		{
			g_pEngine->ClientPrintf(CPlayerSlot(slot), "[WHITELIST] mm-cs2admin is not loaded; "
													   "this command can only be used from the server console.\n");
		}
		return false;
	}

	if (g_pCS2Admin->HasFlag(slot, CS2ADMIN_FLAG_ROOT))
	{
		return true;
	}

	if (!g_pCS2Admin->HasFlag(slot, flag))
	{
		if (g_pEngine)
		{
			g_pEngine->ClientPrintf(CPlayerSlot(slot), "[WHITELIST] You do not have permission to use this command.\n");
		}
		return false;
	}

	return true;
}
