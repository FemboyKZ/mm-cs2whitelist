#ifndef _INCLUDE_WL_WHITELIST_MANAGER_H_
#define _INCLUDE_WL_WHITELIST_MANAGER_H_

#include "common.h"
#include <string>
#include <unordered_set>
#include <tier1/convar.h>

// ConVar declarations
extern CConVar<bool> cv_enable;
extern CConVar<bool> cv_immunity;
extern CConVar<CUtlString> cv_kickmessage;
extern CConVar<CUtlString> cv_filename;

// Build the absolute path to the active whitelist file.
std::string GetWhitelistFilePath();

class WLManager
{
public:
	bool LoadFile();
	bool SaveFile();

	bool AddEntry(const char *entry);
	bool RemoveEntry(const char *entry);

	bool IsPlayerWhitelisted(int slot) const;
	bool IsEntryWhitelisted(const char *entry) const;

	int GetEntryCount() const
	{
		return static_cast<int>(m_whitelist.size());
	}

	void PrintList(int slot) const;

	std::unordered_set<std::string> &GetSet()
	{
		return m_whitelist;
	}

private:
	std::unordered_set<std::string> m_whitelist;
};

extern WLManager g_WLManager;

#endif // _INCLUDE_WL_WHITELIST_MANAGER_H_
