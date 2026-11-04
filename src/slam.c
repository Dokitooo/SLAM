/* slam.c - SLAM (Script Loader & Add-on Manager) for NBA 2K14 (PC, 32-bit)
 *
 * Built two ways from this one file:
 *   dinput8.dll   (default)    the game loads it as DirectInput; SLAM forwards DirectInput to the
 *                              real one (or to dinput8_next.dll, another mod that came as dinput8.dll)
 *   SLAM.asi      (-DSLAM_ASI) for players who already use another ASI loader
 * If both are installed, the second copy notices the first and only forwards DirectInput.
 *
 * What it does, in order:
 *   DllMain        paths, SLAM\SLAM.ini, SLAM\SLAM.log, crash handlers, "SLAM is here" record for
 *                  the mods' SDK stub, then holds the game's entry point (loader.inc)
 *   entry point    dinput8_next.dll, then every mod (loader.inc), then the game starts
 *   game ready     mods' on_game_ready callbacks (loader.inc)
 *   every second   crash.inc housekeeping; every 10 s: patch check (hooks.inc)
 * The only thing SLAM draws: the list of loaded mods, top left, for 3 s at start (overlay.inc).
 * Everything else it has to say goes to SLAM\SLAM.log.
 *
 * Folder layout (next to nba2k14.exe):
 *   dinput8.dll                     SLAM
 *   SLAM\SLAM.ini  SLAM.log  SLAM.previous.log  SLAMCrash.exe  crash\
 *   SLAM\<Mod>\<Mod>.asi (+ its own DLLs, ini, data, <Mod>.log)
 *
 * License: BSD-2-Clause. Hook engine: MinHook (BSD-2), vendored in third_party\minhook.
 */
#define SLAM_VERSION "1.0.0"
#define SLAM_KNOWN_EXE_TS 0x52202C7Fu          /* the nba2k14.exe build SLAM was made with */

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <stdarg.h>
#if defined(_MSC_VER) && !defined(__clang__)
#  include <intrin.h>
#  define RET_ADDR() _ReturnAddress()
#else
#  define RET_ADDR() __builtin_return_address(0)
#endif
#include "slam.h"

typedef unsigned char u8; typedef unsigned short u16; typedef unsigned int u32;

/* one loaded .asi (SLAM mod or plain); g_core = SLAM itself, owner of SLAM's own hooks */
struct SlamMod {
    HMODULE h;
    char file[MAX_PATH];        /* full path */
    char rel[MAX_PATH];         /* path below the game folder, as found */
    char dir[MAX_PATH];         /* folder of the .asi, with trailing backslash */
    char ini[MAX_PATH];         /* <dir><name>.ini */
    char logPath[MAX_PATH];
    char name[64], version[32], author[64], status[96];
    char crashNote[96];         /* api->crash_note */
    void *start;                /* SlamModStart */
    unsigned apiNeed;
    int isSlam, started, failed;
    volatile LONG nHooks, nPatches;
    volatile LONG claim;        /* set once by whoever starts the mod */
    HANDLE logFile;
};

/* ---- globals used by the .inc files ---------------------------------------------------------- */
static HMODULE g_self;
static char g_gameDir[MAX_PATH];        /* folder of nba2k14.exe, trailing backslash */
static char g_slamDir[MAX_PATH];        /* <game>\SLAM\ */
static char g_iniPath[MAX_PATH];
static char g_cfgDisabled[1024], g_cfgLoadFirst[1024];
static SlamMod g_mods[128], g_core;
static int g_modN, g_ownFolders, g_failedN;
static char g_failed[16][MAX_PATH];
static u32 g_exeBase, g_exeSize, g_exeTs, g_t0;
static int g_passive;                   /* second copy of SLAM: only forwards DirectInput */

/* NBA 2K12 builds BodyHook supports (A 1.0x RLD, B 1.0, C other client) */
static int exe_is_2k12(u32 ts) { return ts == 0x4F219A21u || ts == 0x4E63057Du || ts == 0x4E84D35Fu; }
static void crash_late_init(void);
static void dinput_load_real(void);
static void overlay_install(void);

#include "util.inc"
#include "log.inc"
#include "hooks.inc"
#include "loader.inc"
#include "crash.inc"
#include "overlay.inc"
#include "api.inc"

/* ---- SLAM.ini ---------------------------------------------------------------------------------- */
static const char kDefaultIni[] =
    "; SLAM - Script Loader & Add-on Manager for NBA 2K14\r\n"
    "; Delete this file to get the defaults back.\r\n"
    "\r\n"
    "[Mods]\r\n"
    "; .asi file names (comma separated) to load before all others, in this order\r\n"
    "LoadFirst=\r\n"
    "; .asi file names SLAM must not load\r\n"
    "Disabled=\r\n"
    "\r\n"
    "[Crash]\r\n"
    "; 1 = write SLAM\\crash\\crash_<date>.log when the game crashes and show the crash window\r\n"
    "Enabled=1\r\n"
    "; 1 = show the crash window (SLAMCrash.exe)\r\n"
    "Popup=1\r\n"
    "; 1 = also write a .dmp file for mod authors\r\n"
    "Dump=1\r\n"
    "; 1 = note smaller errors the game survives in SLAM.log\r\n"
    "LogExceptions=1\r\n"
    "; test key: writes a test report and shows the crash window. Any combination, e.g. Ctrl+F11, Alt+T,\r\n"
    "; Ctrl+Shift+F12. 0 = off\r\n"
    "TestKey=Ctrl+F11\r\n"
    "; 0 = the game keeps running after the test, 1 = the test really closes the game (like a crash)\r\n"
    "TestCloses=0\r\n"
    "; how many crash reports to keep in SLAM\\crash\\\r\n"
    "Keep=20\r\n"
    "\r\n"
    "[Overlay]\r\n"
    "; 1 = show the list of loaded mods at the top left when the game starts\r\n"
    "Show=1\r\n"
    "; for how many seconds\r\n"
    "Seconds=3\r\n";

static void ini_add(const char *sec, const char *key, const char *val)
{
    char b[8];
    GetPrivateProfileStringA(sec, key, "\x01", b, sizeof(b), g_iniPath);
    if (b[0] == 1 && !b[1]) WritePrivateProfileStringA(sec, key, val, g_iniPath);
}

static void config_load(void)
{
    s_cpy(g_iniPath, g_slamDir, MAX_PATH); s_cat(g_iniPath, "SLAM.ini", MAX_PATH);
    if (!file_exists(g_iniPath)) {
        HANDLE f = CreateFileA(g_iniPath, GENERIC_WRITE, 0, NULL, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, kDefaultIni, sizeof(kDefaultIni) - 1, &w, NULL); CloseHandle(f); }
    }
    /* an SLAM.ini from an older SLAM: add settings it doesn't have yet (existing values stay) */
    ini_add("Overlay", "Show", "1"); ini_add("Overlay", "Seconds", "3");
    ini_add("Crash", "TestKey", "Ctrl+F11"); ini_add("Crash", "TestCloses", "0");
    GetPrivateProfileStringA("Mods", "LoadFirst", "", g_cfgLoadFirst, sizeof(g_cfgLoadFirst), g_iniPath);
    GetPrivateProfileStringA("Mods", "Disabled", "", g_cfgDisabled, sizeof(g_cfgDisabled), g_iniPath);
    crash_config(g_iniPath);
    overlay_config(g_iniPath);
}

/* ---- dinput8 proxy ------------------------------------------------------------------------------ */
static HMODULE g_realDinput;
static FARPROC g_real[5];
static const char *const kExports[5] = { "DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject",
                                         "DllRegisterServer", "DllUnregisterServer" };

/* dinput8_next.dll (another mod that also came as dinput8.dll, renamed) or Windows' own dinput8.dll.
   Called at the entry point (so a dinput8-only mod is in before the game starts) and on first use. */
static void dinput_load_real(void)
{
    static volatile LONG once; char path[MAX_PATH]; int i;
    if (g_realDinput || InterlockedExchange(&once, 1)) return;
    s_cpy(path, g_gameDir, MAX_PATH); s_cat(path, "dinput8_next.dll", MAX_PATH);
    if (file_exists(path)) {
        g_realDinput = LoadLibraryA(path);
        slog("dinput8_next.dll %s (a mod that came as dinput8.dll)", g_realDinput ? "loaded" : "FAILED to load");
    }
    if (!g_realDinput) {
        GetSystemDirectoryA(path, MAX_PATH); s_cat(path, "\\dinput8.dll", MAX_PATH);
        g_realDinput = LoadLibraryA(path);
        if (!g_realDinput) slog("ERROR Windows' dinput8.dll could not be loaded (%s) - keyboard/controller input will fail", path);
    }
    for (i = 0; i < 5; i++) g_real[i] = g_realDinput ? GetProcAddress(g_realDinput, kExports[i]) : 0;
}

#ifndef SLAM_ASI
typedef long (WINAPI *DI8Create_t)(HINSTANCE, DWORD, const void *, LPVOID *, LPVOID);
typedef long (WINAPI *NoArg_t)(void);
typedef long (WINAPI *GetClass_t)(const void *, const void *, LPVOID *);
long WINAPI Proxy_DirectInput8Create(HINSTANCE h, DWORD v, const void *riid, LPVOID *out, LPVOID unk)
{ dinput_load_real(); return g_real[0] ? ((DI8Create_t)g_real[0])(h, v, riid, out, unk) : 0x80004005L; }
long WINAPI Proxy_DllCanUnloadNow(void) { dinput_load_real(); return g_real[1] ? ((NoArg_t)g_real[1])() : 1; }
long WINAPI Proxy_DllGetClassObject(const void *a, const void *b, LPVOID *c)
{ dinput_load_real(); return g_real[2] ? ((GetClass_t)g_real[2])(a, b, c) : 0x80040111L; }
long WINAPI Proxy_DllRegisterServer(void) { dinput_load_real(); return g_real[3] ? ((NoArg_t)g_real[3])() : 0x80004005L; }
long WINAPI Proxy_DllUnregisterServer(void) { dinput_load_real(); return g_real[4] ? ((NoArg_t)g_real[4])() : 0x80004005L; }
#endif

/* ---- "SLAM is here": read by the SDK stub inside every SLAM mod (slam.h) -------------------------- */
static HANDLE g_coreMap;
static int core_publish(void)
{
    char name[64]; SLAM_CoreInfo *ci;
    wsprintfA(name, "Local\\SLAM_core_%08X", GetCurrentProcessId());
    g_coreMap = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE, 0, sizeof(SLAM_CoreInfo), name);
    if (!g_coreMap) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;           /* another SLAM got here first */
    ci = (SLAM_CoreInfo *)MapViewOfFile(g_coreMap, FILE_MAP_WRITE, 0, 0, sizeof(SLAM_CoreInfo));
    if (!ci) return 1;
    ci->apiVersion = SLAM_API_VERSION;
    s_cpy(ci->version, SLAM_VERSION, sizeof(ci->version));
    ci->adopt = core_adopt;
    ci->started = core_started;
    ci->magic = SLAM_CORE_MAGIC;                                    /* last: marks the record complete */
    return 1;
}

/* ---- background thread ----------------------------------------------------------------------- */
static DWORD WINAPI slam_thread(LPVOID unused)
{
    u32 sec = 0, readyAt = 0; int summary = 0;
    (void)unused;
    for (;;) {
        int k;
        for (k = 0; k < 20; k++) { Sleep(50); crash_key_poll(); }      /* test key: checked every 50 ms */
        sec++;
        crash_tick();
        if (sec % 10 == 0) patches_verify();
        if (g_ready && !readyAt) readyAt = sec;
        /* 15 s after the game's code is ready: what each mod hooked/patched (hooks are usually in by then) */
        if (readyAt && !summary && sec - readyAt >= 15) {
            int i;
            summary = 1;
            for (i = 0; i < g_modN; i++)
                if (g_mods[i].isSlam && (g_mods[i].nHooks || g_mods[i].nPatches))
                    slog("mods: %s uses %d hook(s), %d patch(es)", g_mods[i].name, (int)g_mods[i].nHooks, (int)g_mods[i].nPatches);
            slog("summary: %d conflict(s)%s", g_conflictN, g_conflictN ? " - see the CONFLICT lines above" : "");
        }
    }
}

/* ---- start ---------------------------------------------------------------------------------- */
static void slam_start(HMODULE self)
{
    char exe[MAX_PATH], selfPath[MAX_PATH]; u8 *base; u32 nt;
    g_t0 = GetTickCount();
    g_self = self;
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    s_cpy(g_gameDir, exe, MAX_PATH); g_gameDir[s_base(g_gameDir) - g_gameDir] = 0;
    s_cpy(g_slamDir, g_gameDir, MAX_PATH); s_cat(g_slamDir, "SLAM\\", MAX_PATH);
    CreateDirectoryA(g_slamDir, NULL);
    base = (u8 *)GetModuleHandleA(NULL); nt = *(u32 *)(base + 0x3C);
    g_exeBase = (u32)base; g_exeSize = *(u32 *)(base + nt + 0x50); g_exeTs = *(u32 *)(base + nt + 8);

    s_cpy(g_core.name, "SLAM", sizeof(g_core.name));
    s_cpy(g_core.version, SLAM_VERSION, sizeof(g_core.version));
    g_core.h = self; g_core.isSlam = 1; g_core.started = 1;

    if (!core_publish()) { g_passive = 1; return; }                  /* SLAM is already running */

    config_load();
    log_open();
    GetModuleFileNameA(self, selfPath, MAX_PATH);
    slog("SLAM %s (Script Loader & Add-on Manager for NBA 2K14), API %u", SLAM_VERSION, SLAM_API_VERSION);
    slog("SLAM file: %s", selfPath);
    slog("game:      %s", exe);
    slog("exe build: %08X%s, %s", g_exeTs, g_exeTs == SLAM_KNOWN_EXE_TS ? " (known)" : exe_is_2k12(g_exeTs) ? " (NBA 2K12)" : " - UNKNOWN BUILD: mods made for the usual nba2k14.exe may not work",
         (*(u16 *)(base + nt + 0x16) & 0x20) ? "can use 4 GB (large-address-aware)" : "limited to 2 GB (not large-address-aware)");
    hooks_init();
    crash_init();
    entry_hold_install();           /* mods load right before the game's own code starts */
    CreateThread(NULL, 0, slam_thread, NULL, 0, NULL);
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD why, LPVOID reserved)
{
    (void)reserved;
    if (why == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(inst);
        slam_start((HMODULE)inst);
    } else if (why == DLL_PROCESS_DETACH && !g_passive) {
        slog("game closed after %u s", (GetTickCount() - g_t0) / 1000);
        log_flush();
    }
    return TRUE;
}
