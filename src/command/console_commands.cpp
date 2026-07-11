#include "common.h"
#include "whitelist/whitelist_manager.h"
#include "steamgroup/steamgroup_manager.h"
#include "db/wl_database.h"
#include "utils/utils.h"
#include "vendor/interfaces/mm-cs2admin/ics2admin.h"

#include <tier1/convar.h>

CON_COMMAND_F(mm_whitelist_status, "Show whitelist status (enabled, entry count, file name).", FCVAR_GAMEDLL | FCVAR_RELEASE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!HasAdminAccess(slot, "whitelist_status", CS2ADMIN_FLAG_GENERIC))
	{
		return;
	}

	ReplyToSlotT(slot, "enabled=%s | entries=%d | wl_cache=%d | bl_cache=%d | file=%s", cv_enable.Get() ? "yes" : "no", g_WLManager.GetEntryCount(),
				 g_WLManager.GetWhitelistCacheCount(), g_WLManager.GetBlacklistCacheCount(), cv_filename.Get().Get());
}

CON_COMMAND_F(mm_whitelist_reload, "Reload the whitelist file from disk.", FCVAR_GAMEDLL | FCVAR_RELEASE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!HasAdminAccess(slot, "whitelist_reload", CS2ADMIN_FLAG_CONVARS))
	{
		return;
	}

	if (g_WLManager.LoadFile())
	{
		g_SteamGroupManager.FetchGroups();
		if (g_WLDatabase.IsConnected())
		{
			g_WLDatabase.LoadEntries(
				g_WLManager.GetSet(), [slot](int count)
				{ ReplyToSlotT(slot, "Reloaded %d entries from disk + %d from database.", g_WLManager.GetEntryCount() - count, count); });
		}
		else
		{
			ReplyToSlotT(slot, "Reloaded %d entries from disk.", g_WLManager.GetEntryCount());
		}
	}
	else
	{
		ReplyToSlotT(slot, "Failed to reload whitelist file (check server log).");
	}
}

CON_COMMAND_F(mm_whitelist_list, "List all entries in the currently loaded whitelist.", FCVAR_GAMEDLL | FCVAR_RELEASE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!HasAdminAccess(slot, "whitelist_list", CS2ADMIN_FLAG_GENERIC))
	{
		return;
	}

	g_WLManager.PrintList(slot);
}

CON_COMMAND_F(mm_whitelist_add,
			  "Add a SteamID (STEAM_0:X:Y or SteamID64) or IP address to the whitelist. "
			  "Usage: mm_whitelist_add <steamid|ip>",
			  FCVAR_GAMEDLL | FCVAR_RELEASE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!HasAdminAccess(slot, "whitelist_add", CS2ADMIN_FLAG_UNBAN))
	{
		return;
	}

	if (args.ArgC() < 2)
	{
		ReplyToSlotT(slot, "Usage: mm_whitelist_add <steamid|ip>");
		return;
	}

	const char *entry = args.Arg(1);
	if (g_WLManager.AddEntry(entry))
	{
		ReplyToSlotT(slot, "Added '%s'. Saving file...", entry);
		g_WLManager.SaveFile();
	}
	else
	{
		ReplyToSlotT(slot, "'%s' is already in the whitelist (or is invalid).", entry);
	}
}

CON_COMMAND_F(mm_whitelist_remove,
			  "Remove a SteamID or IP address from the whitelist. "
			  "Usage: mm_whitelist_remove <steamid|ip>",
			  FCVAR_GAMEDLL | FCVAR_RELEASE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!HasAdminAccess(slot, "whitelist_remove", CS2ADMIN_FLAG_UNBAN))
	{
		return;
	}

	if (args.ArgC() < 2)
	{
		ReplyToSlotT(slot, "Usage: mm_whitelist_remove <steamid|ip>");
		return;
	}

	const char *entry = args.Arg(1);
	if (g_WLManager.RemoveEntry(entry))
	{
		ReplyToSlotT(slot, "Removed '%s'. Saving file...", entry);
		g_WLManager.SaveFile();
	}
	else
	{
		ReplyToSlotT(slot, "'%s' was not found in the whitelist.", entry);
	}
}

CON_COMMAND_F(mm_whitelist_exist,
			  "Check whether a SteamID or IP is in the currently loaded whitelist. "
			  "Usage: mm_whitelist_exist <steamid|ip>",
			  FCVAR_GAMEDLL | FCVAR_RELEASE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!HasAdminAccess(slot, "whitelist_exist", CS2ADMIN_FLAG_GENERIC))
	{
		return;
	}

	if (args.ArgC() < 2)
	{
		ReplyToSlotT(slot, "Usage: mm_whitelist_exist <steamid|ip>");
		return;
	}

	const char *entry = args.Arg(1);
	if (g_WLManager.IsEntryWhitelisted(entry))
	{
		ReplyToSlotT(slot, "'%s' IS in the whitelist.", entry);
	}
	else
	{
		ReplyToSlotT(slot, "'%s' is NOT in the whitelist.", entry);
	}
}

CON_COMMAND_F(mm_whitelist_cache_clear,
			  "Clear both the per-map whitelist cache (confirmed-allowed) and rejection cache (confirmed-rejected). "
			  "Forces all players to re-run the full whitelist check on next connect.",
			  FCVAR_GAMEDLL | FCVAR_RELEASE)
{
	int slot = context.GetPlayerSlot().Get();
	if (!HasAdminAccess(slot, "whitelist_cache_clear", CS2ADMIN_FLAG_CONVARS))
	{
		return;
	}

	g_WLManager.ClearBlacklistCache();
	g_WLManager.ClearWhitelistCache();
	ReplyToSlotT(slot, "Cache cleared.");
}
