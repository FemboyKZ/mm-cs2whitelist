#include <stdio.h>
#include "cs2whitelist.h"

CS2WhitelistPlugin g_ThisPlugin;
PLUGIN_EXPOSE(CS2WhitelistPlugin, g_ThisPlugin);
bool CS2WhitelistPlugin::Load(PluginId id, ISmmAPI *ismm, char *error, size_t maxlen, bool late)
{
	PLUGIN_SAVEVARS();

	return true;
}

bool CS2WhitelistPlugin::Unload(char *error, size_t maxlen)
{
	return true;
}

void CS2WhitelistPlugin::AllPluginsLoaded()
{
	/* This is where we'd do stuff that relies on the mod or other plugins 
	 * being initialized (for example, cvars added and events registered).
	 */
}