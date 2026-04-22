#ifndef _INCLUDE_WL_COMMON_H_
#define _INCLUDE_WL_COMMON_H_

#include <ISmmPlugin.h>
#include <iserver.h>
#include <cstdint>
#include <string>

#define WL_MAXPLAYERS 64

// Engine interface globals (defined in cs2whitelist.cpp)
extern IVEngineServer     *g_pEngine;
extern IServerGameClients *g_pGameClients;
extern IServerGameDLL     *g_pServerGameDLL;
extern ICvar              *g_pICvar;

// Metamod globals (defined via PLUGIN_GLOBALVARS() in cs2whitelist.cpp)
extern ISmmAPI              *g_SMAPI;
extern ISmmPlugin           *g_PLAPI;
extern PluginId              g_PLID;
extern SourceHook::ISourceHook *g_SHPtr;

// Optional mm-cs2admin interface (nullptr if mm-cs2admin is not loaded)
class ICS2Admin;
extern ICS2Admin *g_pCS2Admin;

#endif // _INCLUDE_WL_COMMON_H_
