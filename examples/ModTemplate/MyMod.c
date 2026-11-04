/* MyMod - starting point for a SLAM mod. Rename "MyMod" everywhere (file names too). */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SLAM_SDK_IMPLEMENTATION
#include "slam.h"

SLAM_MOD("MyMod", "0.1.0", "your name")

static const SLAM_API *api;
static SlamMod *me;

static void SLAM_CALL on_ready(void *user)
{
    (void)user;
    /* TODO: find the game code you need and install hooks/patches here:
       api->hook(me, target, my_detour, (void **)&original);
       api->patch(me, address, bytes, length);                                    */
    api->log(me, "ready");
}

SLAM_EXPORT int SLAM_CALL SlamModStart(const SLAM_API *a, SlamMod *self)
{
    api = a; me = self;
    /* TODO: read your settings:  GetPrivateProfileIntA("MyMod", "Key", 1, api->mod_ini(me)); */
    api->on_game_ready(me, on_ready, 0);
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID reserved)
{
    (void)reserved;
    if (why == DLL_PROCESS_ATTACH) { DisableThreadLibraryCalls(inst); SlamSdk_OnAttach(inst); }
    return TRUE;
}
