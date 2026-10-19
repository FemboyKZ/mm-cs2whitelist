#include "translations.h"

#include "game/cvarquery.h"

mmu::Translations g_WLTranslations;

std::string WL_Translate(int slot, const char *phrase)
{
	return mmu::cvarquery::Translate(g_WLTranslations, slot, phrase);
}
