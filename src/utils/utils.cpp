#include "utils.h"
#include "common.h"
#include "lang/translations.h"
#include "interfaces/cs2admin/ics2admin.h"
#include "utils/str.h"

#include <cstdarg>
#include <cstdio>

std::string NormalizeEntry(const char *input)
{
	if (!input || !input[0])
	{
		return {};
	}

	std::string s(input);

	auto cpos = s.find("//");
	if (cpos != std::string::npos)
	{
		s = s.substr(0, cpos);
	}

	s = str::Trim(s);
	if (s.empty() || s[0] == '#')
	{
		return {};
	}

	// Upper half of every player's SteamID64. Any other would come out as some other player's STEAM_0 form.
	constexpr uint64_t kPlayerIdHigh = 0x01100001;
	const uint64_t steamid64 = ParseSteamID64(s);
	if (steamid64 != 0)
	{
		return (steamid64 >> 32) == kPlayerIdHigh ? SteamID64ToAuthId(steamid64) : std::string();
	}

	unsigned int octets[4];
	int end = 0;
	if (sscanf(s.c_str(), "%3u.%3u.%3u.%3u%n", &octets[0], &octets[1], &octets[2], &octets[3], &end) != 4 || end != static_cast<int>(s.size())
		|| octets[0] > 255 || octets[1] > 255 || octets[2] > 255 || octets[3] > 255)
	{
		return {};
	}
	// Written back out, a leading zero would never match a player's address.
	char ip[16];
	snprintf(ip, sizeof(ip), "%u.%u.%u.%u", octets[0], octets[1], octets[2], octets[3]);
	return ip;
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

void ReplyToSlotT(int slot, const char *phrase, ...)
{
	std::string fmt = WL_Translate(slot, phrase);

	char buf[512];
	va_list args;
	va_start(args, phrase);
	vsnprintf(buf, sizeof(buf), fmt.c_str(), args);
	va_end(args);

	ReplyToSlot(slot, "[WHITELIST] %s\n", buf);
}

bool HasAdminAccess(int slot, const char *commandName, uint32_t defaultFlag)
{
	if (slot < 0)
	{
		return true;
	}

	// Stricter than AdminAccess. Console-only without mm-cs2admin, even for defaultFlag 0.
	if (!g_CS2Admin.Available())
	{
		ReplyToSlotT(slot, "mm-cs2admin is not loaded; this command can only be used from the server console.");
		return false;
	}

	if (g_CS2Admin.CanUseCommand(slot, commandName, defaultFlag))
	{
		return true;
	}

	ReplyToSlotT(slot, "You do not have permission to use this command.");
	return false;
}
