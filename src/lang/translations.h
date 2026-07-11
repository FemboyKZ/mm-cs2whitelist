#ifndef _INCLUDE_WL_TRANSLATIONS_H_
#define _INCLUDE_WL_TRANSLATIONS_H_

#include "mmu/translations.h"

#include <string>

// SourceMod-style phrase tables for the whitelist's console and kick text, resolved per client language.
extern mmu::Translations g_WLTranslations;

// Defined in cs2whitelist.cpp.
// Returns the mapped phrase-file language key for the client in `slot`, or the default language when unavailable.
std::string WL_SlotLanguage(int slot);

// Defined in cs2whitelist.cpp.
// Re-acquires ClientCvarValue and reloads phrase tables. Call after (re)loading the plugin config.
void WL_LoadTranslations();

// Translate `phrase` into the language of the client in `slot`.
std::string WL_Translate(int slot, const char *phrase);

#endif // _INCLUDE_WL_TRANSLATIONS_H_
