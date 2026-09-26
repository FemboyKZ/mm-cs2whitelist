#include "utils.h"
#include "common.h"
#include "lang/translations.h"
#include "interfaces/cs2admin/ics2admin.h"
#include "mmu/str_utils.h"

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

	// A SteamID64 stays as typed, bare numbers are also Steam group IDs.
	std::string authid = SteamID2Or3ToAuthId(s);
	return authid.empty() ? s : authid;
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
