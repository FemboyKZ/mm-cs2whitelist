#ifndef _INCLUDE_WL_COMMON_H_
#define _INCLUDE_WL_COMMON_H_

#include "mmu/admin_access.h"
#include "mmu/plugin_globals.h"

#include <ISmmPlugin.h>
#include <iserver.h>
#include <cstdint>
#include <string>

// Optional. Without mm-cs2admin, in-game commands are console-only.
extern mmu::AdminAccess g_CS2Admin;

#endif // _INCLUDE_WL_COMMON_H_
