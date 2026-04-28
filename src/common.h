#ifndef _INCLUDE_WL_COMMON_H_
#define _INCLUDE_WL_COMMON_H_

#include <ISmmPlugin.h>
#include <iserver.h>
#include <cstdint>
#include <string>

#define WL_MAXPLAYERS 64

extern IVEngineServer *g_pEngine;
extern IServerGameClients *g_pGameClients;
extern IServerGameDLL *g_pServerGameDLL;
extern ICvar *g_pICvar;

extern ISmmAPI *g_SMAPI;
extern ISmmPlugin *g_PLAPI;
extern PluginId g_PLID;
extern SourceHook::ISourceHook *g_SHPtr;

class ICS2Admin;
extern ICS2Admin *g_pCS2Admin;

#endif // _INCLUDE_WL_COMMON_H_
