#ifndef _INCLUDE_WL_UTILS_H_
#define _INCLUDE_WL_UTILS_H_

#include <string>
#include <cstdint>
#include <cstdarg>

// SteamID64 -> "STEAM_0:X:Y"
std::string SteamID64ToAuthId(uint64_t id64);

// Normalise a raw whitelist line or command argument to a canonical form.
// Returns an empty string if the input is blank or a comment.
std::string NormalizeEntry(const char *input);

// Strip the :port suffix from an address string ("1.2.3.4:27015" -> "1.2.3.4").
std::string StripPort(const char *addr);

// Printf-style reply to a player slot or the server console (slot < 0).
void ReplyToSlot(int slot, const char *fmt, ...);

// Check whether slot has the given cs2admin flag.
// Server console (slot < 0) always passes.
// Prints a denial message to the player if access is refused.
bool HasAdminAccess(int slot, uint32_t flag);

#endif // _INCLUDE_WL_UTILS_H_
