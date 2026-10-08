#ifndef _INCLUDE_WL_UTILS_H_
#define _INCLUDE_WL_UTILS_H_

#include "utils/steamid.h"

#include <string>
#include <cstdint>
#include <cstdarg>

// A whitelist line or command argument in the one form entries are kept in:
// STEAM_0:Y:Z for a SteamID however it was written, the dotted quad for an IPv4 address. Empty for anything else.
std::string NormalizeEntry(const char *input);

// Printf-style reply to a player slot or the server console (slot < 0).
void ReplyToSlot(int slot, const char *fmt, ...);

// Translated reply. `phrase` is resolved to the slot's language, then formatted.
// Output is prefixed with [WHITELIST] and newline-terminated.
void ReplyToSlotT(int slot, const char *phrase, ...);

// Check whether slot may run commandName via cs2admin's override chain.
// defaultFlag is the required flag when no override applies. Group "whitelist".
// Server console (slot < 0) always passes.
// Prints a denial message to the player if access is refused.
bool HasAdminAccess(int slot, const char *commandName, uint32_t defaultFlag);

#endif // _INCLUDE_WL_UTILS_H_
