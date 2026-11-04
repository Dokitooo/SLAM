/* HelloSLAM - example SLAM mod for NBA 2K14
 *
 * Shows every part of the SLAM API v1 without changing anything in the game:
 *   - who am I (SLAM_MOD), start-up (SlamModStart), the "SLAM not installed" window (SlamSdk_OnAttach)
 *   - its own folder, ini and log:  SLAM\HelloSLAM\HelloSLAM.ini  ->  SLAM\HelloSLAM\HelloSLAM.log
 *   - other mods (find_mod), the game build (exe_timestamp), reading memory (read)
 *   - waiting for the game's code (on_game_ready)
 *   - a hook (hook): logs the first game files opened. Other mods may hook the same function
 *     (BodyHook does, for hair styles): SLAM chains them, and SLAM.log says so.
 *   - a crash note (crash_note): shows up in the crash window if the game crashes
 *
 * Install: SLAM\HelloSLAM\HelloSLAM.asi (+ HelloSLAM.ini). Build: see SDK.md.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#define SLAM_SDK_IMPLEMENTATION          /* in exactly one .c file of a mod */
#include "slam.h"

SLAM_MOD("HelloSLAM", "1.0.0", "SLAM example")

static const SLAM_API *api;
static SlamMod *me;
static int g_logFiles, g_maxFiles, g_seen;

/* ---- the hook: CreateFileA ---------------------------------------------------------------------- */
typedef HANDLE (WINAPI *CreateFileA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CreateFileA_t CreateFileA_next;   /* "the rest of the chain": another mod's hook or Windows */

static HANDLE WINAPI my_CreateFileA(LPCSTR name, DWORD access, DWORD share, LPSECURITY_ATTRIBUTES sa,
                                    DWORD disp, DWORD flags, HANDLE tmpl)
{
    /* always call the rest of the chain - never skip it, other mods may be behind us */
    HANDLE h = CreateFileA_next(name, access, share, sa, disp, flags, tmpl);
    if (name && g_seen < g_maxFiles) {
        int n = InterlockedIncrement((LONG *)&g_seen);
        if (n <= g_maxFiles)
            api->log(me, "file %d: %s%s", n, name, h == INVALID_HANDLE_VALUE ? "  (not found)" : "");
        if (n == g_maxFiles) api->log(me, "(stopping the file list after %d files - see MaxFiles in HelloSLAM.ini)", g_maxFiles);
    }
    return h;
}

/* ---- once the game's code is ready ------------------------------------------------------------- */
static void SLAM_CALL on_ready(void *user)
{
    (void)user;
    api->log(me, "the game's code is ready");
    api->crash_note(me, "watching file opens");
    if (g_logFiles) {
        void *target = (void *)GetProcAddress(GetModuleHandleA("kernel32.dll"), "CreateFileA");
        int r = api->hook(me, target, (void *)my_CreateFileA, (void **)&CreateFileA_next);
        api->log(me, r == SLAM_OK ? "hooked CreateFileA - logging the first %d files the game opens"
                                  : "could not hook CreateFileA (error %d)", r == SLAM_OK ? g_maxFiles : r);
    }
}

/* ---- start: SLAM calls this once, after all mods are loaded, before the game runs -------------- */
SLAM_EXPORT int SLAM_CALL SlamModStart(const SLAM_API *a, SlamMod *self)
{
    char ver[32]; unsigned short mz = 0;
    api = a; me = self;

    api->log(me, "HelloSLAM 1.0.0 running on SLAM %s (API %u)", api->slamVersion, api->apiVersion);
    api->log(me, "my folder: %s", api->mod_dir(me));
    api->log(me, "my ini:    %s", api->mod_ini(me));
    api->log(me, "nba2k14.exe build %08X%s", api->exe_timestamp(), api->exe_timestamp() == 0x52202C7F ? " (the usual one)" : "");

    if (api->read(api->game_base(), &mz, 2)) api->log(me, "first bytes of nba2k14.exe in memory: %c%c", mz & 0xFF, mz >> 8);
    if (api->find_mod("BodyHook", ver, sizeof(ver))) api->log(me, "BodyHook %s is running too", ver);
    else api->log(me, "BodyHook is not running");

    g_logFiles = GetPrivateProfileIntA("HelloSLAM", "LogFileOpens", 1, api->mod_ini(me));
    g_maxFiles = GetPrivateProfileIntA("HelloSLAM", "MaxFiles", 30, api->mod_ini(me));

    /* nba2k14.exe unpacks itself at start: game hooks belong in on_game_ready */
    api->on_game_ready(me, on_ready, 0);

    /* a function newer than API 1 would be checked like this before use:
       if (SLAM_HAS(api, some_new_function)) api->some_new_function(...); */
    return 0;                                   /* 0 = started fine */
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID reserved)
{
    (void)reserved;
    if (why == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        SlamSdk_OnAttach(inst);                 /* shows "SLAM not installed" if needed */
    }
    return TRUE;
}
