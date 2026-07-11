#include "translations.h"

mmu::Translations g_WLTranslations;

std::string WL_Translate(int slot, const char *phrase)
{
	return g_WLTranslations.Translate(WL_SlotLanguage(slot), phrase ? phrase : "");
}
