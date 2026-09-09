#ifndef _INCLUDE_WL_COMMON_H_
#define _INCLUDE_WL_COMMON_H_

#include "mmu/admin_access.h"
#include "mmu/plugin_globals.h"

#include <ISmmPlugin.h>
#include <iserver.h>
#include <cstdint>
#include <string>

// mm-cs2admin permission gate.
// Optional: the whitelist works without it, with in-game commands restricted to the server console.
extern mmu::AdminAccess g_CS2Admin;

#endif // _INCLUDE_WL_COMMON_H_
