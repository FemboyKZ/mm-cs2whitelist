#ifndef _INCLUDE_ICS2WHITELIST_H_
#define _INCLUDE_ICS2WHITELIST_H_

#include <cstdint>

// Interface string passed to ISmmAPI::MetaFactory().
#define CS2WHITELIST_INTERFACE "ICS2Whitelist001"

// Public readonly whitelist interface for other Metamod plugins.
//
// Obtain via:
//   ICS2Whitelist *pWL = static_cast<ICS2Whitelist *>(
//       g_SMAPI->MetaFactory(CS2WHITELIST_INTERFACE, nullptr, nullptr));
class ICS2Whitelist
{
public:
	// Returns true if the player in the given slot passes the whitelist check.
	// This is the same check performed on connect: the whitelist must be enabled,
	// and the player must match either a whitelisted SteamID or IP.
	// Always returns true if the whitelist is disabled or the slot is invalid.
	virtual bool IsPlayerWhitelisted(int slot) const = 0;

	// Returns true if the given raw entry string (SteamID or IP) is present in
	// the inmemory whitelist.  The entry is normalised before the lookup.
	virtual bool IsEntryWhitelisted(const char *entry) const = 0;

	// Returns the number of entries currently in the inmemory whitelist.
	virtual int GetEntryCount() const = 0;
};

#endif // _INCLUDE_ICS2WHITELIST_H_
