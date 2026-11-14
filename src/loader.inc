/* loader.inc - finds, loads and starts mods
 *
 * WHEN: right before the game's own code starts (SLAM holds the exe's entry point for that
 *       moment, like BodyHook's loader did), so every mod is in before the game runs.
 * WHERE (in this order, each file name once):
 *       SLAM\  and  SLAM\<Mod>\            (2 levels)  - the home of SLAM mods
 *       scripts\  plugins\  asi\           (2 levels)  - classic ASI folders
 *       the game folder itself             (no subfolders)
 *       [Mods] LoadFirst=a.asi,b.asi in SLAM.ini moves files to the front, Disabled= skips them.
 * HOW:  LoadLibraryEx with LOAD_WITH_ALTERED_SEARCH_PATH: a DLL the mod needs is found in the
 *       mod's own folder first (SLAM\<Mod>\helper.dll works). Before loading, the mod's imports
 *       are read from the file: if it brings a DLL whose name is already loaded from somewhere
 *       else, Windows will hand it that one instead - SLAM reports this ("DLL clash").
 * SLAM mods (export SlamModStart) are started after ALL files are loaded, in load order.
 *       Plain .asi mods start themselves when loaded and simply show up as "not SLAM".
 */

typedef const SLAM_ModInfo *(SLAM_CALL *ModInfo_t)(void);
typedef int (SLAM_CALL *ModStart_t)(const SLAM_API *, SlamMod *);

static char g_found[256][MAX_PATH];             /* relative paths, load order */
static int g_foundN;
static const SLAM_API g_api;                    /* api.inc */
static void ready_install(void);

static int already_found(const char *file)
{
    int i;
    for (i = 0; i < g_foundN; i++) if (s_ieq(s_base(g_found[i]), file)) return 1;
    return 0;
}

static void scan_folder(const char *sub, int depth)
{
    char pat[MAX_PATH], sub2[MAX_PATH]; WIN32_FIND_DATAA fd; HANDLE h;
    s_cpy(pat, g_gameDir, MAX_PATH); s_cat(pat, sub, MAX_PATH); s_cat(pat, "*.asi", MAX_PATH);
    h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            if (already_found(fd.cFileName)) { slog("mods: skipped %s%s (a file with that name is already loaded)", sub, fd.cFileName); continue; }
            if (s_inlist(g_cfgDisabled, fd.cFileName)) { slog("mods: skipped %s%s (Disabled in SLAM.ini)", sub, fd.cFileName); continue; }
            if (g_foundN < 256) { s_cpy(g_found[g_foundN], sub, MAX_PATH); s_cat(g_found[g_foundN], fd.cFileName, MAX_PATH); g_foundN++; }
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
    if (depth <= 0) return;
    s_cpy(pat, g_gameDir, MAX_PATH); s_cat(pat, sub, MAX_PATH); s_cat(pat, "*", MAX_PATH);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || fd.cFileName[0] == '.') continue;
        if (s_ieq(sub, "SLAM\\") && s_ieq(fd.cFileName, "crash")) continue;
        s_cpy(sub2, sub, MAX_PATH); s_cat(sub2, fd.cFileName, MAX_PATH); s_cat(sub2, "\\", MAX_PATH);
        scan_folder(sub2, depth - 1);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* [Mods] LoadFirst=a.asi,b.asi : stable move to the front, in the order given */
static void apply_load_first(void)
{
    char one[MAX_PATH], tmp[MAX_PATH]; const char *l = g_cfgLoadFirst; int n, at = 0, i, j;
    while (*l) {
        while (*l == ' ' || *l == ',' || *l == ';') l++;
        for (n = 0; *l && *l != ',' && *l != ';' && n < MAX_PATH - 1; l++) one[n++] = *l;
        while (n && one[n - 1] == ' ') n--;
        one[n] = 0;
        if (!n) continue;
        for (i = at; i < g_foundN; i++)
            if (s_ieq(s_base(g_found[i]), one)) {
                s_cpy(tmp, g_found[i], MAX_PATH);
                for (j = i; j > at; j--) s_cpy(g_found[j], g_found[j - 1], MAX_PATH);
                s_cpy(g_found[at++], tmp, MAX_PATH);
                break;
            }
    }
}

/* "DLL clash": the mod ships xyz.dll in its folder but another xyz.dll is already loaded */
static void check_dll_clash(const char *rel, const char *full)
{
    u32 size = 0, imp, o, k; u8 *img = file_read(full, &size, 64u << 20);
    char dir[MAX_PATH], own[MAX_PATH], loaded[MAX_PATH];
    if (!img) return;
    s_cpy(dir, full, MAX_PATH); dir[s_base(dir) - dir] = 0;
    if (size > 0x200 && img[0] == 'M' && img[1] == 'Z' && *(u32 *)(img + 0x3C) + 0x100 < size) {
        u32 pe = *(u32 *)(img + 0x3C);
        imp = *(u32 *)(img + pe + 24 + 96 + 8);                     /* import directory (PE32) */
        o = imp ? pe_rva2off(img, size, imp) : 0;
        for (k = 0; o && o + 20 * (k + 1) <= size && k < 64; k++) {
            u32 nameRva = *(u32 *)(img + o + 20 * k + 12), no; const char *nm; HMODULE h;
            if (!nameRva) break;
            no = pe_rva2off(img, size, nameRva);
            if (!no) continue;
            nm = (const char *)img + no;
            s_cpy(own, dir, MAX_PATH); s_cat(own, nm, MAX_PATH);
            if (!file_exists(own)) continue;                        /* not shipped by the mod */
            h = GetModuleHandleA(nm);
            if (!h || !GetModuleFileNameA(h, loaded, MAX_PATH) || s_ieq(loaded, own)) continue;
            {
                char c[300];
                s_fmt(c, sizeof(c), "DLL clash: %s brings its own %s, but %s is already loaded and Windows will give it that one",
                      rel, nm, loaded);
                add_conflict(c);
            }
        }
    }
    VirtualFree(img, 0, MEM_RELEASE);
}

static SlamMod *mod_by_module(HMODULE h)
{
    int i;
    for (i = 0; i < g_modN; i++) if (g_mods[i].h == h) return &g_mods[i];
    return 0;
}

/* fills in a SlamMod for a loaded module; SLAM mods get name/version from SlamModInfo */
static SlamMod *mod_register_locked(HMODULE h, const char *rel);
static SlamMod *mod_register(HMODULE h, const char *rel)    /* loader thread and SDK stub threads */
{
    SlamMod *m;
    EnterCriticalSection(&g_hookLock);
    m = mod_register_locked(h, rel);
    LeaveCriticalSection(&g_hookLock);
    return m;
}
static SlamMod *mod_register_locked(HMODULE h, const char *rel)
{
    SlamMod *m; ModInfo_t info; const SLAM_ModInfo *mi = 0; char full[MAX_PATH];
    if ((m = mod_by_module(h)) != 0 || g_modN >= 128) return m;
    m = &g_mods[g_modN];
    m->h = h;
    GetModuleFileNameA(h, full, MAX_PATH);
    s_cpy(m->file, full, MAX_PATH);
    s_cpy(m->rel, rel ? rel : s_base(full), MAX_PATH);
    s_cpy(m->dir, full, MAX_PATH); m->dir[s_base(m->dir) - m->dir] = 0;
    info = (ModInfo_t)GetProcAddress(h, "SlamModInfo");
    m->start = (void *)GetProcAddress(h, "SlamModStart");
    if (info) mi = info();
    if (mi && mi->name && m->start) {
        m->isSlam = 1;
        s_cpy(m->name, mi->name, sizeof(m->name));
        s_cpy(m->version, mi->version ? mi->version : "?", sizeof(m->version));
        s_cpy(m->author, mi->size >= 16 && mi->author ? mi->author : "", sizeof(m->author));
        m->apiNeed = mi->size >= 20 ? mi->apiVersion : 1;
    } else {
        int n;
        s_cpy(m->name, s_base(full), sizeof(m->name));
        n = s_len(m->name); if (n > 4 && m->name[n - 4] == '.') m->name[n - 4] = 0;
        s_cpy(m->version, "", sizeof(m->version));
    }
    s_cpy(m->ini, m->dir, MAX_PATH); s_cat(m->ini, m->name, MAX_PATH); s_cat(m->ini, ".ini", MAX_PATH);
    g_modN++;                                   /* visible to others only when complete */
    return m;
}

/* 1.0.2: a plain .asi that exports InitializeASI (the Ultimate ASI Loader convention) only starts when its loader
 * calls it - e.g. CurvedShotMeter.asi, which shipped its own dinput8.dll just to do that (and that dinput8.dll pushed
 * SLAM aside: SLAM then started late, go-to shots never came). SLAM calls it once, in load order, for the plain .asi
 * files SLAM itself loaded (one that another loader loaded first was already started by that loader).
 * WHEN: like that dinput8.dll did - right after the game's first DirectInput8Create (the game window and Direct3D
 * exist by then). Called at process start, the shot meter's rendering core crashed inside d3d9.dll (crash 15-09-50).
 * SLAM.asi (under another loader, no dinput8 export): at once, as before. */
typedef void (__cdecl *InitAsi_t)(void);
static volatile LONG g_plainPending, g_plainDone;
static void plain_init(SlamMod *m)
{
    InitAsi_t f;
    if (m->isSlam || !m->fresh || !m->h) return;
    if (!(f = (InitAsi_t)(void *)GetProcAddress(m->h, "InitializeASI"))) return;
    if (InterlockedExchange(&m->claim, 1)) return;
    f();
    m->started = 1;
    slog("mods: started %s (plain .asi: InitializeASI called)", m->rel[0] ? m->rel : m->name);
}
static void plain_start(SlamMod *m)
{
    if (m->isSlam || !m->fresh || !m->h || !GetProcAddress(m->h, "InitializeASI")) return;
#ifdef SLAM_ASI
    plain_init(m);
#else
    if (g_plainDone) { plain_init(m); return; }
    g_plainPending = 1;
    slog("mods: %s exports InitializeASI - called when the game sets up DirectInput (after its window and Direct3D)", m->rel[0] ? m->rel : m->name);
#endif
}
/* the game's first DirectInput8Create (SLAM's own export): start the waiting plain .asi mods, in load order */
static void plain_start_pending(void)
{
    int i;
    if (InterlockedExchange(&g_plainDone, 1) || !g_plainPending) return;
    for (i = 0; i < g_modN; i++) plain_init(&g_mods[i]);
}

static void mod_start(SlamMod *m)
{
    int r;
    if (!m->isSlam) { plain_start(m); return; }
    if (m->failed) return;
    if (InterlockedExchange(&m->claim, 1)) return;          /* loader and SDK stub may both ask */
    if (m->apiNeed > SLAM_API_VERSION) {
        m->failed = 1;
        s_fmt(m->status, sizeof(m->status), "needs a newer SLAM (API %u)", m->apiNeed);
        slog("mods: %s %s NOT started - it needs SLAM API %u, this SLAM %s has %u. Update SLAM.",
             m->name, m->version, m->apiNeed, SLAM_VERSION, SLAM_API_VERSION);
        return;
    }
    m->started = 1;
    r = ((ModStart_t)m->start)(&g_api, m);
    if (r != 0) { m->failed = 1; s_fmt(m->status, sizeof(m->status), "did not start (code %d)", r); }
    slog("mods: started %s %s%s%s", m->name, m->version, r ? "  - FAILED, code " : "", r ? m->status : "");
}

/* a SLAM mod that another ASI loader loaded: the SDK stub in the mod asks for it */
static int SLAM_CALL core_adopt(void *hmodule)
{
    SlamMod *m; char full[MAX_PATH];
    if (!hmodule || !GetModuleFileNameA((HMODULE)hmodule, full, MAX_PATH)) return 0;
    m = mod_register((HMODULE)hmodule, s_base(full));
    if (!m || !m->isSlam) return 0;
    slog("mods: %s was loaded by another ASI loader - starting it", full);
    mod_start(m);
    return m->started;
}
/* "SLAM takes care of this mod": loaded by SLAM's loader (it starts it after all files are in),
   or already adopted. The SDK stub then does nothing. */
static volatile LONG g_loadDone;                /* load_all finished: every mod it found is started */
static int SLAM_CALL core_started(void *hmodule)
{
    int i;
    /* the SDK stub asks right after the mod is loaded: answer only once SLAM's own loading is over,
       otherwise a mod SLAM is about to start would be started early (out of load order) */
    for (i = 0; i < 600 && !g_loadDone; i++) Sleep(50);
    return mod_by_module((HMODULE)hmodule) != 0;
}

/* SLAM mods already in the process (loaded by another loader before SLAM's turn) */
static void adopt_loaded(void)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, GetCurrentProcessId());
    MODULEENTRY32 me;
    if (snap == INVALID_HANDLE_VALUE) return;
    me.dwSize = sizeof(me);
    if (Module32First(snap, &me)) do {
        if (me.hModule != g_self && GetProcAddress(me.hModule, "SlamModStart") && !mod_by_module(me.hModule)) {
            SlamMod *m = mod_register(me.hModule, me.szModule);
            if (m) slog("mods: found   %s (loaded by another ASI loader)", me.szExePath);
        }
    } while (Module32Next(snap, &me));
    CloseHandle(snap);
}

/* the shared "only one loader loads the folders" lock used by the other 2K14 ASI loaders,
   SigExt/CrowdTune's built-in loaders and BodyHook's old loader */
static int first_folder_loader(void)
{
    char name[64];
    wsprintfA(name, "Local\\NBA2K14_asi_folder_%08X", GetCurrentProcessId());
    return !(CreateMutexA(NULL, FALSE, name) && GetLastError() == ERROR_ALREADY_EXISTS);
}

static volatile LONG g_loadedAll;
static void load_all(void)
{
    char full[MAX_PATH]; int i, plain = 0, slam = 0;
    if (InterlockedExchange(&g_loadedAll, 1)) return;
    folder_check();           /* 1.0.1: game in Program Files - before any mod writes a file */
    ready_install();          /* game-ready detection, before any mod can register for it */
    crash_late_init();        /* dbghelp.dll + old report cleanup: not allowed in DllMain */
    dinput_load_real();       /* dinput8_next.dll (a dinput8-only mod) gets in before the other mods */
    /* SLAM creates no folders but its own SLAM\ - scripts\ etc. are only read if they exist */
    if (!first_folder_loader()) slog("mods: another ASI loader is loading scripts\\ etc. - SLAM still loads SLAM\\ and starts SLAM mods");
    else g_ownFolders = 1;
    scan_folder("SLAM\\", 2);
    if (g_ownFolders) { scan_folder("scripts\\", 2); scan_folder("plugins\\", 2); scan_folder("asi\\", 2); scan_folder("", 0); }
    apply_load_first();
    adopt_loaded();
    for (i = 0; i < g_foundN; i++) {
        HMODULE h, pre; SlamMod *m;
        s_cpy(full, g_gameDir, MAX_PATH); s_cat(full, g_found[i], MAX_PATH);
        check_dll_clash(g_found[i], full);
        pre = GetModuleHandleA(full);                 /* 1.0.2: already loaded (and started) by another loader? */
        h = LoadLibraryExA(full, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (!h) {
            DWORD e = GetLastError();
            slog("mods: FAILED  %s (Windows error %u%s)", g_found[i], e,
                 e == 126 ? ": a DLL it needs is missing - put it next to the .asi" :
                 e == 193 ? ": not a 32-bit DLL" : e == 1114 ? ": the mod's own start-up code failed" : "");
            if (g_failedN < 16) s_cpy(g_failed[g_failedN++], g_found[i], MAX_PATH);
            continue;
        }
        if (h == g_self) { slog("mods: skipped %s (that is SLAM itself)", g_found[i]); continue; }
        m = mod_register(h, g_found[i]);
        if (m && !pre) m->fresh = 1;
        if (m) slog("mods: loaded  %-40s %s %s%s", g_found[i], m->isSlam ? m->name : "", m->isSlam ? m->version : "(plain .asi, not a SLAM mod)",
                    m->isSlam && m->author[0] ? "" : "");
    }
    for (i = 0; i < g_modN; i++) mod_start(&g_mods[i]);
    for (i = 0; i < g_modN; i++) { if (g_mods[i].isSlam) slam++; else plain++; }
    slog("mods: %d SLAM mod(s), %d plain .asi mod(s), %d failed", slam, plain, g_failedN);
    InterlockedExchange(&g_loadDone, 1);
}

/* ---- entry hold: run load_all() right before the exe's (packed) entry point ---------------- */
static u8 g_epOrig[5]; static u32 g_ep; static volatile LONG g_epFired;
static void ep_restore(void)
{
    DWORD old; u8 *p = (u8 *)g_ep; int i;
    VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old);
    for (i = 0; i < 5; i++) p[i] = g_epOrig[i];
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
}
static void __cdecl ep_work(void) { g_epFired = 1; ep_restore(); load_all(); }

#if defined(_MSC_VER)
__declspec(naked) static void ep_stub(void)
{
    __asm {
        pushad
        pushfd
        call ep_work
        popfd
        popad
        jmp dword ptr [g_ep]
    }
}
#endif

static DWORD WINAPI ep_late(LPVOID unused)
{
    (void)unused;
    Sleep(3000);
    if (!g_epFired && !InterlockedExchange(&g_epFired, 1)) {
        ep_restore();
        slog("mods: SLAM was loaded after the game had started - loading mods now");
        load_all();
    }
    return 0;
}

static void entry_hold_install(void)
{
    u8 *base = (u8 *)GetModuleHandleA(NULL), *p; DWORD old; int i;
    u32 nt = *(u32 *)(base + 0x3C), ep = *(u32 *)(base + nt + 0x28);
    if (!ep) { load_all(); return; }
    g_ep = (u32)base + ep; p = (u8 *)g_ep;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) { load_all(); return; }
    for (i = 0; i < 5; i++) g_epOrig[i] = p[i];
    p[0] = 0xE9; *(u32 *)(p + 1) = (u32)ep_stub - (g_ep + 5);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    CreateThread(NULL, 0, ep_late, NULL, 0, NULL);
}

/* ---- game ready: nba2k14.exe is encrypted on disk; its code exists once it unpacked itself.
 * The first call of the C runtime's _initterm/_initterm_e that comes FROM the exe is the game's
 * own start-up code running = everything is unpacked. Fallback: 60 s. -------------------------- */
typedef int (__cdecl *InitTerm_t)(void *, void *);
static InitTerm_t g_origInitterm, g_origIntitermE;
static volatile LONG g_ready;
static SlamReadyCb g_readyCb[128]; static void *g_readyUser[128]; static SlamMod *g_readyMod[128]; static int g_readyN;

static void fire_ready(const char *how)
{
    int i;
    if (InterlockedExchange(&g_ready, 1)) return;
    slog("game code ready (%s), %u ms after start - %d callback(s)", how, GetTickCount() - g_t0, g_readyN);
    overlay_install();
    for (i = 0; i < g_readyN; i++) g_readyCb[i](g_readyUser[i]);
}
static int in_exe(u32 a) { return a >= g_exeBase && a < g_exeBase + g_exeSize; }

static int __cdecl my_initterm_e(void *a, void *b)
{
    if (!g_ready && in_exe((u32)RET_ADDR())) fire_ready("C runtime start-up");
    return g_origIntitermE(a, b);
}
static int __cdecl my_initterm(void *a, void *b)
{
    if (!g_ready && in_exe((u32)RET_ADDR())) fire_ready("C runtime start-up");
    return g_origInitterm(a, b);
}
static DWORD WINAPI ready_fallback(LPVOID unused)
{
    int i;
    (void)unused;
    for (i = 0; i < 600 && !g_ready; i++) Sleep(100);
    if (!g_ready) {
        slog("WARNING the game's start-up was not seen within 60 s (unknown nba2k14.exe build?) - starting game-ready callbacks now");
        fire_ready("60 s fallback");
    }
    return 0;
}
static void ready_install(void)
{
    /* NBA 2K14 uses msvcr100; NBA 2K12 an older Visual C++ runtime */
    static const char *const crts[] = { "msvcr100.dll", "msvcr90.dll", "msvcr80.dll", "msvcr110.dll", 0 };
    HMODULE crt = 0; void *fe, *f; int ok = 0, i;
    for (i = 0; crts[i] && !crt; i++) crt = GetModuleHandleA(crts[i]);
    fe = crt ? (void *)GetProcAddress(crt, "_initterm_e") : 0; f = crt ? (void *)GetProcAddress(crt, "_initterm") : 0;
    if (fe && hook_add(&g_core, fe, (void *)my_initterm_e, (void **)&g_origIntitermE) == SLAM_OK) ok++;
    if (f && hook_add(&g_core, f, (void *)my_initterm, (void **)&g_origInitterm) == SLAM_OK) ok++;
    if (!ok) slog("WARNING the game's C runtime (msvcr100.dll) was not found - game-ready will use the 60 s fallback");
    CreateThread(NULL, 0, ready_fallback, NULL, 0, NULL);
}
