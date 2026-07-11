#include "utils.h"
#include "common.h"
#include "lang/translations.h"
#include "vendor/interfaces/mm-cs2admin/ics2admin.h"

#include "mmu/print.h"

#include <cctype>
#include <cstring>
#include <algorithm>
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

	// STEAM_X:Y:Z -> STEAM_0:Y:Z
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

// Console-only printer. slot < 0 prints to the server console.
// Only ClientConsoleV is used, which formats and prints without touching the setup fields.
static mmu::ChatPrinter &Printer()
{
	static mmu::ChatPrinter printer = []
	{
		mmu::ChatPrinter p;
		mmu::ChatPrinter::Setup s;
		s.translations = &g_WLTranslations;
		s.slotLanguage = &WL_SlotLanguage;
		s.conTag = "WHITELIST";
		p.Configure(s);
		return p;
	}();
	return printer;
}

void ReplyToSlot(int slot, const char *fmt, ...)
{
	va_list args;
	va_start(args, fmt);
	Printer().ClientConsoleV(slot, fmt, args);
	va_end(args);
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

	if (!g_pCS2Admin)
	{
		ReplyToSlotT(slot, "mm-cs2admin is not loaded; this command can only be used from the server console.");
		return false;
	}

	if (g_pCS2Admin->CanUseCommand(slot, commandName, "whitelist", defaultFlag))
	{
		return true;
	}

	ReplyToSlotT(slot, "You do not have permission to use this command.");
	return false;
}
