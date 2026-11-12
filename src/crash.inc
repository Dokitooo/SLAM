/* crash.inc - crash reporter for ALL nba2k14.exe crashes (moved here from BodyHook 1.11.8/1.17.5)
 *
 * WHAT IT DOES
 *   - Serious CPU exceptions (access violation, illegal instruction, divide by zero, ...) are
 *     noted in SLAM.log the moment they happen ("first chance": the game may still handle them).
 *     Each place is logged once; they are also kept for the crash report.
 *   - When the game really dies, SLAM writes
 *         SLAM\crash\crash_<date>_<time>.log   readable report + a [facts] block
 *         SLAM\crash\crash_<date>_<time>.dmp   small minidump (for developers)
 *     and starts SLAM\SLAMCrash.exe <report>, a separate small program that shows the window
 *     with the report and the plain-language cause. The cause is worked out by SLAMCrash.exe
 *     from the [facts] block, so the guesses can improve without touching dinput8.dll.
 *   - Every 30 s SLAM.log gets the game's address-space use (32-bit game: leaks show as a trend).
 *   - Test key (SLAM.ini [Crash] TestKey=Ctrl+F11, any combination, 0 = off) writes a TEST report and shows
 *     the window; the game keeps running. TestCloses=1: the test really ends the game, through the same
 *     path as a real crash. The keys are checked every 50 ms.
 *   Nothing in the game is changed: the handlers only look, then pass the exception on.
 *
 * WHY THE [facts] BLOCK: one "key=value" per line between "[facts]" and "[/facts]", written
 *   first in the report. SLAMCrash.exe reads only that block; people read the rest.
 *
 * NEEDS (slam.c): g_slamDir, g_gameDir, g_mods/g_modN (SlamMod: h, name, version, file, isSlam,
 *   crashNote), g_exeBase, g_exeSize, g_exeTs, g_t0, g_ready, g_log, SLAM_VERSION,
 *   g_conflictN (hooks.inc). Called: crash_config(), crash_init() from DllMain; crash_late_init() at the
 *   game's entry point (load_all);
 *   crash_tick() every second from SLAM's background thread.
 *
 * CRASH-TIME RULES: no heap, no locks that the crashing thread may hold (SLAM.log lines are
 *   written without g_logLock), dbghelp.dll is loaded at start, a stack overflow is reported
 *   from a fresh thread (the crashed one has no stack left).
 */

#define SLAM_TEST_CRASH 0xE0534C4Du        /* 'SLM' + E0: the test-key report */
static void crash_report_entry(PEXCEPTION_POINTERS ep);

/* ---- settings: SLAM.ini [Crash] -------------------------------------------------------------- */
static int g_crEnabled = 1;                /* Enabled=1     whole crash reporter            */
static int g_crPopup = 1;                  /* Popup=1       start SLAMCrash.exe             */
static int g_crDump = 1;                   /* Dump=1        write the .dmp                  */
static int g_crLogExc = 1;                 /* LogExceptions=1 first-chance lines in SLAM.log */
static int g_crTestKey = 1;                /* TestKey=Ctrl+F11  test report (0 = off)         */
static int g_crTestVk = VK_F11, g_crTestMods = 1;   /* mods: 1 Ctrl, 2 Shift, 4 Alt */
static int g_crTestCloses;                 /* TestCloses=0  1 = the test really ends the game  */
static volatile LONG g_crRealTest;
static char g_crTestName[32] = "Ctrl+F11";
static int g_crKeep = 20;                  /* Keep=20       reports kept in SLAM\crash\     */
static int g_crFileTrail = 1;              /* FileTrail=1   remember which game files were opened (for the report) */

static void crash_config(const char *ini)
{
    g_crEnabled = GetPrivateProfileIntA("Crash", "Enabled", 1, ini);
    g_crPopup   = GetPrivateProfileIntA("Crash", "Popup", 1, ini);
    g_crDump    = GetPrivateProfileIntA("Crash", "Dump", 1, ini);
    g_crLogExc  = GetPrivateProfileIntA("Crash", "LogExceptions", 1, ini);
    {   /* TestKey = "Ctrl+F11", "Alt+T", "Ctrl+Shift+F12" ... ; "1" = the default, "0" / "" = off */
        char k[32], one[16]; const char *p = k; int mods = 0, vk = 0, n;
        GetPrivateProfileStringA("Crash", "TestKey", "Ctrl+F11", k, sizeof(k), ini);
        if (s_ieq(k, "1")) s_cpy(k, "Ctrl+F11", sizeof(k));
        g_crTestKey = !(k[0] == 0 || s_ieq(k, "0") || s_ieq(k, "off"));
        while (g_crTestKey && *p) {
            for (n = 0; *p && *p != '+' && n < 15; p++) if (*p != ' ') one[n++] = *p;
            one[n] = 0; if (*p == '+') p++;
            if (s_ieq(one, "Ctrl") || s_ieq(one, "Control")) mods |= 1;
            else if (s_ieq(one, "Shift")) mods |= 2;
            else if (s_ieq(one, "Alt")) mods |= 4;
            else if ((one[0] == 'F' || one[0] == 'f') && one[1] >= '1' && one[1] <= '9') { int f = one[1] - '0'; if (one[2] >= '0' && one[2] <= '9') f = f * 10 + one[2] - '0'; if (f <= 24) vk = VK_F1 + f - 1; }
            else if (n == 1 && s_lc(one[0]) >= 'a' && s_lc(one[0]) <= 'z') vk = s_lc(one[0]) - 'a' + 'A';
            else if (n == 1 && one[0] >= '0' && one[0] <= '9') vk = one[0];
            else if (s_ieq(one, "Pause")) vk = VK_PAUSE;
            else if (s_ieq(one, "Insert") || s_ieq(one, "Ins")) vk = VK_INSERT;
            else if (s_ieq(one, "Home")) vk = VK_HOME;
            else if (s_ieq(one, "End")) vk = VK_END;
            else if (s_ieq(one, "ScrollLock")) vk = VK_SCROLL;
        }
        if (g_crTestKey && vk) { g_crTestVk = vk; g_crTestMods = mods; s_cpy(g_crTestName, k, sizeof(g_crTestName)); }
        else if (g_crTestKey) s_cpy(g_crTestName, "Ctrl+F11 (TestKey not understood)", sizeof(g_crTestName));
    }
    g_crTestCloses = GetPrivateProfileIntA("Crash", "TestCloses", 0, ini);
    g_crKeep    = GetPrivateProfileIntA("Crash", "Keep", 20, ini);
    if (g_crKeep < 1) g_crKeep = 1;
    g_crFileTrail = GetPrivateProfileIntA("Crash", "FileTrail", 1, ini);
}

static volatile LONG g_crashOnce;          /* a report is being / was written */

/* ---- file trail: which game files (.iff .ros ...) the game opened, and when ---------------------
 * SLAM puts the innermost link on the game's CreateFileA import (mods that redirect file names chain
 * in front of it, so this sees the file that is really opened). One entry per file name: first and
 * last time it was opened, by which thread, how often. Static table, no heap; a crash report reads
 * it without the lock. Cost: a short name check per CreateFileA call. */
#define FT_MAX 1024
#define FT_PATH 180
typedef struct { u32 hash, first, last, tid, opens; int ok; char path[FT_PATH]; } FtEntry;
static FtEntry g_ft[FT_MAX];
static volatile LONG g_ftN, g_ftLock, g_ftLost;
typedef HANDLE (WINAPI *CFA_t)(LPCSTR, DWORD, DWORD, LPSECURITY_ATTRIBUTES, DWORD, DWORD, HANDLE);
static CFA_t g_ftNext;

static int ft_wanted(const char *p, int n)
{
    static const char *ext[] = { ".iff", ".ros", ".fra", ".cmg", ".rda", ".bik", ".dat", 0 };
    int i, k;
    if (n < 5) return 0;
    for (i = 0; ext[i]; i++) {
        for (k = 0; k < 4 && s_lc(p[n - 4 + k]) == ext[i][k]; k++) ;
        if (k == 4) return 1;
    }
    return 0;
}
static void ft_note(const char *name, int ok)
{
    u32 h = 2166136261u, now = GetTickCount(), tid = GetCurrentThreadId(); int n = 0, i, spin;
    const char *p = name;
    if (s_len(g_gameDir) && !memcmp_i(name, g_gameDir, s_len(g_gameDir))) p = name + s_len(g_gameDir);   /* shorter: below the game folder */
    while (p[0] == '.' && (p[1] == '\\' || p[1] == '/')) p += 2;
    for (; p[n] && n < 400; n++) h = (h ^ (u8)s_lc(p[n])) * 16777619u;
    if (!ft_wanted(p, n)) return;
    for (spin = 0; InterlockedCompareExchange(&g_ftLock, 1, 0); spin++) { if (spin > 2000) { InterlockedIncrement(&g_ftLost); return; } Sleep(0); }
    for (i = 0; i < g_ftN; i++)                                  /* long paths: compare what was kept of them */
        if (g_ft[i].hash == h && !memcmp_i(g_ft[i].path, p, n < FT_PATH - 1 ? n : FT_PATH - 1) && (n >= FT_PATH - 1 || !g_ft[i].path[n])) break;
    if (i == g_ftN) {
        if (g_ftN < FT_MAX) { g_ft[i].hash = h; g_ft[i].first = now; g_ft[i].opens = 0; s_cpy(g_ft[i].path, p, FT_PATH); g_ftN++; }
        else { InterlockedIncrement(&g_ftLost); InterlockedExchange(&g_ftLock, 0); return; }
    }
    g_ft[i].last = now; g_ft[i].tid = tid; g_ft[i].opens++; g_ft[i].ok = ok;
    InterlockedExchange(&g_ftLock, 0);
}
static HANDLE WINAPI ft_CreateFileA(LPCSTR name, DWORD acc, DWORD share, LPSECURITY_ATTRIBUTES sa, DWORD disp, DWORD flags, HANDLE tmpl)
{
    HANDLE r = g_ftNext(name, acc, share, sa, disp, flags, tmpl);
    if (name && !g_crashOnce) {
        DWORD e = GetLastError();
        if (r != INVALID_HANDLE_VALUE || mem_readable(name, 1)) ft_note(name, r != INVALID_HANDLE_VALUE);   /* a bad name pointer: leave it */
        SetLastError(e);
    }
    return r;
}
static void ft_install(void)
{
    int r;
    if (!g_crFileTrail) { slog("crash report: file trail off (SLAM.ini [Crash] FileTrail=0)"); return; }
    r = hook_import(&g_core, "KERNEL32.dll", "CreateFileA", (void *)ft_CreateFileA, (void **)&g_ftNext);
    if (r != SLAM_OK) slog("crash report: file trail not available (import hook: %d)", r);
    else slog("crash report: file trail on - crash reports list the game files (.iff .ros ...) opened this session");
}
/* newest first: indexes into g_ft (crash time: no lock, the table only grows) */
static int g_ftOrder[FT_MAX], g_ftSorted;
static int ft_sorted(void)
{
    int n = g_ftN < FT_MAX ? g_ftN : FT_MAX, i, j;
    g_ftSorted = n;
    for (i = 0; i < n; i++) {
        int k = i; u32 t = g_ft[i].last;
        for (j = i; j > 0 && (u32)(t - g_ft[g_ftOrder[j - 1]].last) < 0x80000000u && t != g_ft[g_ftOrder[j - 1]].last; j--) g_ftOrder[j] = g_ftOrder[j - 1];
        g_ftOrder[j] = k;
    }
    return n;
}

/* ---- state ----------------------------------------------------------------------------------- */
typedef BOOL (WINAPI *MiniDump_t)(HANDLE, DWORD, HANDLE, DWORD, void *, void *, void *);
static MiniDump_t g_miniDump;
static LPTOP_LEVEL_EXCEPTION_FILTER g_prevUef;
static DWORD g_mainTid;
static struct { u32 code, eip, tid, tick; } g_exc[48];   /* first-chance exceptions seen */
static volatile LONG g_excN;
static u32 g_lowFreeMax = 0xFFFFFFFFu;     /* smallest "largest free block" seen this run */

static HANDLE g_crFile = INVALID_HANDLE_VALUE;
static char g_crPath[MAX_PATH], g_crDmp[MAX_PATH];
static char g_crBuf[1200];                 /* crash-time formatting: static, never the heap */

/* SLAM.log without the lock (the crashed thread may own it) */
static void crash_slog(const char *fmt, ...)
{
    va_list ap; char line[1100]; SYSTEMTIME t; DWORD w; int n;
    if (g_log == INVALID_HANDLE_VALUE) return;
    GetLocalTime(&t);
    wsprintfA(line, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    n = s_len(line);
    va_start(ap, fmt); s_vfmt(line + n, sizeof(line) - n - 2, fmt, ap); va_end(ap);
    s_cat(line, "\r\n", sizeof(line));
    WriteFile(g_log, line, s_len(line), &w, NULL);
}

/* one line of the crash report */
static void crash_put(const char *text)
{
    DWORD w;
    if (g_crFile == INVALID_HANDLE_VALUE) return;
    WriteFile(g_crFile, text, s_len(text), &w, NULL);
    WriteFile(g_crFile, "\r\n", 2, &w, NULL);
}
static void crash_putf(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt); s_vfmt(g_crBuf, sizeof(g_crBuf), fmt, ap); va_end(ap);
    crash_put(g_crBuf);
}

static int read32(u32 a, u32 *v) { if (!mem_readable((const void *)a, 4)) return 0; *v = *(const u32 *)a; return 1; }

static const char *crash_name(u32 code)
{
    switch (code) {
    case 0xC0000005u: return "access violation";
    case 0xC000001Du: return "illegal instruction";
    case 0xC0000094u: return "integer divide by zero";
    case 0xC0000095u: return "integer overflow";
    case 0xC00000FDu: return "stack overflow";
    case 0xC0000409u: return "stack buffer overrun";
    case 0xC0000374u: return "heap corruption";
    case 0xC0000096u: return "privileged instruction";
    case 0xC0000006u: return "in-page error (file could not be read)";
    case 0xC000008Cu: return "array bounds exceeded";
    case 0xC000008Du: case 0xC000008Eu: case 0xC000008Fu: case 0xC0000090u:
    case 0xC0000091u: case 0xC0000092u: case 0xC0000093u: return "floating point error";
    case 0x80000003u: return "breakpoint";
    case 0xE06D7363u: return "C++ exception nobody caught";
    case 0xC0000017u: return "out of memory";
    case SLAM_TEST_CRASH: return "TEST report (test key)";
    }
    return 0;
}
/* exceptions worth a first-chance line in SLAM.log (C++ throws and breakpoints are normal) */
static int crash_serious(u32 code) { return code != 0xE06D7363u && code != 0x80000003u && code != SLAM_TEST_CRASH && crash_name(code); }
/* exceptions after which Windows will NOT call the unhandled-exception filter */
static int crash_fatal_now(u32 code) { return code == 0xC0000409u || code == 0xC0000374u; }

/* ---- "where is this address" --------------------------------------------------------------- */
static SlamMod *crash_mod_at(u32 a)
{
    HMODULE h = mod_of(a); int i;
    if (!h) return 0;
    for (i = 0; i < g_modN; i++) if (g_mods[i].h == h) return &g_mods[i];
    return 0;
}
/* "nba2k14.exe+7F13AF" / "BodyHook.asi+1234 [SLAM mod BodyHook 1.19.9]" / "SLAM hook relay" / "not in any module" */
static void crash_where(u32 a, char *out, int cap)
{
    char path[MAX_PATH]; HMODULE h; SlamMod *m; int k;
    if (in_pool(a)) { s_cpy(out, "SLAM hook relay", cap); return; }
    for (k = 0; k < g_targetN; k++) {
        u32 tr = g_targets[k]->iat ? 0 : (u32)g_targets[k]->tramp;
        if (tr && a >= tr && a < tr + 32) { s_fmt(out, cap, "hook trampoline of %08X", (u32)g_targets[k]->target); return; }
    }
    h = mod_of(a);
    if (!h || !GetModuleFileNameA(h, path, MAX_PATH)) { s_cpy(out, "not in any module", cap); return; }
    s_fmt(out, cap, "%s+%X", s_base(path), a - (u32)h);
    if ((HMODULE)h == g_self) s_cat(out, " [SLAM]", cap);
    else if ((m = crash_mod_at(a)) != 0) {
        char t[120];
        s_fmt(t, sizeof(t), m->isSlam ? " [SLAM mod %s %s]" : " [plain .asi %s]", m->name, m->version);
        s_cat(out, t, cap);
    }
}
/* short module name for the [facts] block ("" = not in a module) */
static void crash_modname(u32 a, char *out, int cap)
{
    char path[MAX_PATH]; HMODULE h = mod_of(a);
    out[0] = 0;
    if (in_pool(a)) { s_cpy(out, "SLAM-relay", cap); return; }
    if (h && GetModuleFileNameA(h, path, MAX_PATH)) s_cpy(out, s_base(path), cap);
}
static int crash_is_code(u32 a)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi))) return 0;
    return mbi.State == MEM_COMMIT && (mbi.Protect & 0xF0) && (mbi.Type == MEM_IMAGE || in_pool(a));
}
static void list_add(char *list, const char *items, int cap)   /* items: "A" or "A, B" */
{
    char one[64]; int n;
    while (*items) {
        while (*items == ' ' || *items == ',') items++;
        for (n = 0; *items && *items != ',' && n < 63; items++) one[n++] = *items;
        one[n] = 0;
        if (!n || s_inlist(list, one)) continue;
        if (list[0]) s_cat(list, ",", cap);
        s_cat(list, one, cap);
    }
}

/* ---- memory ---------------------------------------------------------------------------------- */
static void crash_mem(u32 *usedMb, u32 *totalMb, u32 *freeMb, u32 *bigMb)
{
    MEMORY_BASIC_INFORMATION mbi; MEMORYSTATUSEX ms; u32 a = 0x10000, freeMax = 0, freeTot = 0, n = 0;
    while (a < 0xFFFF0000u && n++ < 200000) {
        if (!VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi)) || !mbi.RegionSize) break;
        if (mbi.State == MEM_FREE) { freeTot += (u32)mbi.RegionSize; if ((u32)mbi.RegionSize > freeMax) freeMax = (u32)mbi.RegionSize; }
        if ((u32)mbi.BaseAddress + (u32)mbi.RegionSize <= a) break;
        a = (u32)mbi.BaseAddress + (u32)mbi.RegionSize;
    }
    ms.dwLength = sizeof(ms);
    if (GlobalMemoryStatusEx(&ms)) { *totalMb = (u32)(ms.ullTotalVirtual >> 20); *usedMb = (u32)((ms.ullTotalVirtual - ms.ullAvailVirtual) >> 20); }
    else { *totalMb = 0; *usedMb = 0; }
    *freeMb = freeTot >> 20; *bigMb = freeMax >> 20;
}

/* ---- first-chance exceptions ----------------------------------------------------------------- */
static LONG CALLBACK crash_veh(PEXCEPTION_POINTERS ep)
{
    const EXCEPTION_RECORD *r = ep->ExceptionRecord;
    u32 code = r->ExceptionCode, eip = (u32)r->ExceptionAddress; LONG i, n; char where[160];
    if (!crash_serious(code)) return EXCEPTION_CONTINUE_SEARCH;
    n = g_excN;
    for (i = 0; i < n && i < 48; i++) if (g_exc[i].code == code && g_exc[i].eip == eip) goto fatal;
    i = InterlockedIncrement(&g_excN) - 1;
    if (i < 48) { g_exc[i].code = code; g_exc[i].eip = eip; g_exc[i].tid = GetCurrentThreadId(); g_exc[i].tick = GetTickCount(); }
    if (g_crLogExc && i < 48) {
        crash_where(eip, where, sizeof(where));
        if (code == 0xC0000005u && r->NumberParameters >= 2)
            crash_slog("EXCEPTION %s at %08X %s: %s %08X (thread %u; the game may still recover)", crash_name(code), eip, where,
                       r->ExceptionInformation[0] == 1 ? "writing" : r->ExceptionInformation[0] == 8 ? "executing" : "reading",
                       (u32)r->ExceptionInformation[1], GetCurrentThreadId());
        else
            crash_slog("EXCEPTION %s (%08X) at %08X %s (thread %u; the game may still recover)", crash_name(code), code, eip, where, GetCurrentThreadId());
    }
fatal:
    /* stack-cookie and heap-corruption failures end the game without the filter: report now */
    if (crash_fatal_now(code)) { static LONG once; if (!InterlockedExchange(&once, 1)) crash_report_entry(ep); }
    return EXCEPTION_CONTINUE_SEARCH;
}

/* ---- the report ------------------------------------------------------------------------------ */
static void crash_open_files(int test)
{
    SYSTEMTIME t; char dir[MAX_PATH], name[80];
    GetLocalTime(&t);
    s_cpy(dir, g_slamDir, MAX_PATH); s_cat(dir, "crash", MAX_PATH);
    CreateDirectoryA(dir, NULL);
    wsprintfA(name, "\\%s_%04u-%02u-%02u_%02u-%02u-%02u", test ? "test" : "crash", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    s_cpy(g_crPath, dir, MAX_PATH); s_cat(g_crPath, name, MAX_PATH);
    s_cpy(g_crDmp, g_crPath, MAX_PATH);
    s_cat(g_crPath, ".log", MAX_PATH); s_cat(g_crDmp, ".dmp", MAX_PATH);
    g_crFile = CreateFileA(g_crPath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

static void crash_write(PEXCEPTION_POINTERS ep)
{
    const EXCEPTION_RECORD *r = ep->ExceptionRecord; const CONTEXT *c = ep->ContextRecord;
    u32 code = r->ExceptionCode, eip = (u32)r->ExceptionAddress, i, n, v;
    int test = code == SLAM_TEST_CRASH, isAv = code == 0xC0000005u && r->NumberParameters >= 2;
    u32 usedMb, totalMb, freeMb, bigMb, up = (GetTickCount() - g_t0) / 1000;
    char where[160], mod[64], stackMods[400], nearMods[300], stackNear[300], tmp[300];
    const char *access = !isAv ? "" : r->ExceptionInformation[0] == 1 ? "write" : r->ExceptionInformation[0] == 8 ? "execute" : "read";
    u32 target = isAv ? (u32)r->ExceptionInformation[1] : 0;
    MEMORY_BASIC_INFORMATION mbi;

    crash_open_files(test);
    crash_where(eip, where, sizeof(where));
    crash_modname(eip, mod, sizeof(mod));
    crash_mem(&usedMb, &totalMb, &freeMb, &bigMb);
    owners_near(eip, 32, nearMods, sizeof(nearMods));

    /* modules on the stack (return addresses), and mods that hooked/patched near them */
    stackMods[0] = 0; stackNear[0] = 0;
    for (i = 0, n = 0; i < 2048 && n < 64; i++) {
        SlamMod *m;
        if (!read32(c->Esp + 4 * i, &v)) break;
        if (v < 0x10000 || !crash_is_code(v)) continue;
        n++;
        if (in_pool(v)) list_add(stackMods, "SLAM-relay", sizeof(stackMods));
        else if ((m = crash_mod_at(v)) != 0) list_add(stackMods, m->name, sizeof(stackMods));
        if (n <= 12) { owners_near(v, 32, tmp, sizeof(tmp)); if (tmp[0]) list_add(stackNear, tmp, sizeof(stackNear)); }
    }

    /* [facts] for SLAMCrash.exe - keep the keys stable, add new ones at the end */
    crash_put("[facts]");
    crash_putf("slam=%s", SLAM_VERSION);
    crash_putf("test=%d", test ? (g_crRealTest ? 2 : 1) : 0);
    crash_putf("code=%08X", code);
    crash_putf("kind=%s", crash_name(code) ? crash_name(code) : "unknown exception");
    crash_putf("eip=%08X", eip);
    crash_putf("module=%s", mod);
    {   SlamMod *m = crash_mod_at(eip);
        crash_putf("module_mod=%s", m ? m->name : "");
        crash_putf("module_is_slam_mod=%d", m ? m->isSlam : 0);
        crash_putf("module_is_slam=%d", mod_of(eip) == g_self || in_pool(eip)); }
    crash_putf("access=%s", access);
    crash_putf("target=%08X", target);
    crash_putf("target_mapped=%d", isAv && VirtualQuery((LPCVOID)target, &mbi, sizeof(mbi)) ? mbi.State == MEM_COMMIT : 0);
    crash_putf("eip_in_module=%d", mod_of(eip) != 0 || in_pool(eip));
    crash_putf("hooked_near_eip=%s", nearMods);
    crash_putf("stack_mods=%s", stackMods);
    crash_putf("hooked_near_stack=%s", stackNear);
    crash_putf("thread=%s", GetCurrentThreadId() == g_mainTid ? "main" : "other");
    crash_putf("uptime_s=%u", up);
    crash_putf("game_ready=%d", (int)g_ready);
    crash_putf("exe_ts=%08X", g_exeTs);
    crash_putf("mem_used_mb=%u", usedMb);
    crash_putf("mem_total_mb=%u", totalMb);
    crash_putf("mem_largest_free_mb=%u", bigMb);
    crash_putf("mem_lowest_largest_free_mb=%u", g_lowFreeMax == 0xFFFFFFFFu ? bigMb : g_lowFreeMax >> 20);
    crash_putf("earlier_exceptions=%u", (u32)(g_excN < 48 ? g_excN : 48));
    crash_putf("conflicts=%d", g_conflictN);
    {   int k; char list[900]; list[0] = 0;
        for (k = 0; k < g_modN; k++) if (g_mods[k].isSlam && g_mods[k].crashNote[0]) {
            s_fmt(tmp, sizeof(tmp), "%s: %s", g_mods[k].name, g_mods[k].crashNote);
            if (list[0]) s_cat(list, " | ", sizeof(list));
            s_cat(list, tmp, sizeof(list));
        }
        crash_putf("notes=%s", list); }
    /* files the game opened, the mods list (for SLAMCrash.exe's checks) */
    {   int k, nf = ft_sorted(), cnt = 0; char list[600]; u32 now = GetTickCount();
        list[0] = 0;
        for (k = 0; k < nf && cnt < 10; k++, cnt++) { if (list[0]) s_cat(list, ",", sizeof(list)); s_cat(list, s_base(g_ft[g_ftOrder[k]].path), sizeof(list)); }
        crash_putf("files_trail=%d", g_crFileTrail && g_ftNext ? 1 : 0);
        crash_putf("files_n=%d", nf);
        crash_putf("files_last=%s", list);
        for (k = 0; k < nf; k++) if (g_ft[g_ftOrder[k]].tid == GetCurrentThreadId()) break;
        crash_putf("thread_last_file=%s", k < nf ? g_ft[g_ftOrder[k]].path : "");
        crash_putf("thread_last_file_ms=%u", k < nf ? now - g_ft[g_ftOrder[k]].last : 0);
        list[0] = 0;
        for (k = 0; k < g_modN; k++) {
            s_fmt(tmp, sizeof(tmp), "%s %s", g_mods[k].name, g_mods[k].version);
            if (list[0]) s_cat(list, ",", sizeof(list));
            s_cat(list, tmp, sizeof(list));
        }
        crash_putf("mods=%s", list);
        crash_putf("crash_tid=%u", GetCurrentThreadId());
    }
    crash_put("[/facts]");
    crash_put("");
    /* every file of the trail, newest first: "<ms before the crash>|<thread>|<opened ok>|<times>|<path>" */
    {   int k, nf = g_ftSorted; u32 now = GetTickCount();
        crash_put("[files]");
        for (k = 0; k < nf && k < 400; k++) {
            const FtEntry *e = &g_ft[g_ftOrder[k]];
            crash_putf("%u|%u|%d|%u|%s", now - e->last, e->tid, e->ok, e->opens, e->path);
        }
        crash_put("[/files]");
        crash_put("");
    }

    /* readable part */
    crash_put("==================================================================================");
    crash_putf("%s  -  NBA 2K14 %s, SLAM %s", test ? "SLAM TEST REPORT (the game did not crash)" : "NBA 2K14 CRASH REPORT",
               g_exeTs == 0x52202C7Fu ? "(known exe)" : exe_is_2k12(g_exeTs) ? "(NBA 2K12 exe)" : "(unknown exe build)", SLAM_VERSION);
    crash_put("==================================================================================");
    crash_putf("What:   %s (%08X)", crash_name(code) ? crash_name(code) : "exception", code);
    crash_putf("Where:  %08X  %s", eip, where);
    if (isAv) crash_putf("        while %s address %08X%s", access[0] == 'w' ? "writing to" : access[0] == 'e' ? "running code at" : "reading",
                         target, target < 0x10000 ? "  (a null/empty pointer)" : "");
    crash_putf("Thread: %u (%s), %u s after start, game code %s", GetCurrentThreadId(), GetCurrentThreadId() == g_mainTid ? "main thread" : "other thread",
               up, g_ready ? "running" : "still starting");
    if (nearMods[0]) crash_putf("Mods that changed code right here: %s", nearMods);
    crash_put("");
    crash_put("Registers:");
    crash_putf("    eax=%08X ebx=%08X ecx=%08X edx=%08X esi=%08X edi=%08X ebp=%08X esp=%08X",
               c->Eax, c->Ebx, c->Ecx, c->Edx, c->Esi, c->Edi, c->Ebp, c->Esp);
    if (mem_readable((const void *)eip, 16)) {
        const u8 *b = (const u8 *)eip;
        crash_putf("    code at eip: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X",
                   b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7], b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
    }
    crash_put("");
    crash_put("Call stack (return addresses found on the stack, newest first):");
    for (i = 0, n = 0; i < 2048 && n < 32; i++) {
        if (!read32(c->Esp + 4 * i, &v)) break;
        if (v < 0x10000 || !crash_is_code(v)) continue;
        crash_where(v, where, sizeof(where));
        owners_near(v, 32, tmp, sizeof(tmp));
        crash_putf("    [esp+%04X] %08X  %s%s%s", 4 * i, v, where, tmp[0] ? "   <- hooked/patched by " : "", tmp);
        n++;
    }
    crash_put("");
    crash_put("Mods:");
    {   int k;
        for (k = 0; k < g_modN; k++) {
            SlamMod *m = &g_mods[k];
            crash_putf("    %-14s %-10s %s  %s%s%s", m->name, m->version, m->isSlam ? (m->started && !m->failed ? "SLAM mod" : "SLAM mod, NOT running") : "plain .asi",
                       m->file, m->crashNote[0] ? "  | was: " : "", m->crashNote);
        }
        if (!g_modN) crash_put("    (none)");
    }
    crash_put("");
    crash_put("Hooks and patches (hook chains: first runs first):");
    hooks_report(crash_put);
    if (g_conflictN) {
        int k;
        crash_put("");
        crash_put("Conflicts found at start:");
        for (k = 0; k < g_conflictN; k++) crash_putf("    %s", g_conflicts[k]);
    }
    crash_put("");
    crash_putf("Memory: %u MB used of %u MB, %u MB free, largest free block %u MB (lowest this run: %u MB)",
               usedMb, totalMb, freeMb, bigMb, g_lowFreeMax == 0xFFFFFFFFu ? bigMb : g_lowFreeMax >> 20);
    {   int k, nf = g_ftSorted; u32 now = GetTickCount(), tid = GetCurrentThreadId();
        crash_put("");
        if (!g_crFileTrail || !g_ftNext) crash_put("Game files opened: not recorded (SLAM.ini [Crash] FileTrail=0, or the hook was not possible)");
        else {
            crash_putf("Game files opened last (newest first, %d different files this session%s):", nf, g_ftLost ? ", some not recorded" : "");
            for (k = 0; k < nf && k < 40; k++) {
                const FtEntry *e = &g_ft[g_ftOrder[k]]; u32 ms = now - e->last;
                crash_putf("    %5u.%u s before  %s%s%s", ms / 1000, (ms % 1000) / 100, e->path, e->ok ? "" : "  (could not be opened)",
                           e->tid == tid ? "  <- by the crashing thread" : "");
            }
            if (nf > 40) crash_put("    ... (all of them in the [files] block at the top)");
        }
    }
    if (g_excN) {
        LONG k, m = g_excN < 48 ? g_excN : 48;
        crash_put("");
        crash_put("Earlier exceptions this run (the game handled them, or this is where it started):");
        for (k = 0; k < m; k++) {
            crash_where(g_exc[k].eip, where, sizeof(where));
            crash_putf("    %6u s  %-24s at %08X %s  (thread %u)", (g_exc[k].tick - g_t0) / 1000,
                       crash_name(g_exc[k].code) ? crash_name(g_exc[k].code) : "?", g_exc[k].eip, where, g_exc[k].tid);
        }
    }
    FlushFileBuffers(g_crFile);

    if (g_crDump && g_miniDump && !test) {
        HANDLE f = CreateFileA(g_crDmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (f != INVALID_HANDLE_VALUE) {
            struct { DWORD tid; void *ep; BOOL client; } mei;
            mei.tid = GetCurrentThreadId(); mei.ep = ep; mei.client = FALSE;
            /* 0x40 MiniDumpWithIndirectlyReferencedMemory: stacks + the memory they point at (a few MB) */
            crash_putf("");
            crash_putf(g_miniDump(GetCurrentProcess(), GetCurrentProcessId(), f, 0x40, &mei, 0, 0)
                       ? "Minidump: %s" : "Minidump could not be written (%s)", g_crDmp);
            CloseHandle(f);
        }
    }
    crash_put("");
    crash_put("Please send this file (and the .dmp next to it) to the mod author named above.");
    CloseHandle(g_crFile); g_crFile = INVALID_HANDLE_VALUE;

    crash_slog("%s %s at %08X %s - report: %s", test ? "TEST REPORT" : "CRASH", crash_name(code) ? crash_name(code) : "exception", eip, where, g_crPath);
    if (g_log != INVALID_HANDLE_VALUE) FlushFileBuffers(g_log);
}

/* the separate window program; falls back to a plain message box if it is missing */
static void crash_popup(int test)
{
    char exe[MAX_PATH], cmd[MAX_PATH * 2 + 8];
    STARTUPINFOA si; PROCESS_INFORMATION pi;
    if (!g_crPopup) return;
    s_cpy(exe, g_slamDir, MAX_PATH); s_cat(exe, "SLAMCrash.exe", MAX_PATH);
    s_cpy(cmd, "\"", sizeof(cmd)); s_cat(cmd, exe, sizeof(cmd)); s_cat(cmd, "\" \"", sizeof(cmd));
    s_cat(cmd, g_crPath, sizeof(cmd)); s_cat(cmd, "\"", sizeof(cmd));
    memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    if (file_exists(exe) && CreateProcessA(exe, cmd, NULL, NULL, FALSE, 0, NULL, g_slamDir, &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
        return;
    }
    if (!test) {
        char msg[MAX_PATH + 200];
        s_cpy(msg, "NBA 2K14 crashed. SLAM wrote a report:\n\n", sizeof(msg));
        s_cat(msg, g_crPath, sizeof(msg));
        s_cat(msg, "\n\n(SLAMCrash.exe is missing from the SLAM folder, so the cause can't be shown here.)", sizeof(msg));
        MessageBoxA(NULL, msg, "NBA 2K14 crashed", MB_OK | MB_ICONERROR | MB_TOPMOST | MB_SETFOREGROUND);
    }
}

/* the crashed thread may have no stack left (stack overflow): do the work on a fresh thread */
static PEXCEPTION_POINTERS g_crEp;
static DWORD WINAPI crash_worker(LPVOID unused) { (void)unused; crash_write(g_crEp); crash_popup(g_crEp->ExceptionRecord->ExceptionCode == SLAM_TEST_CRASH); return 0; }

static void crash_report_entry(PEXCEPTION_POINTERS ep)
{
    if (!g_crEnabled || InterlockedIncrement(&g_crashOnce) != 1) return;
    g_crEp = ep;
    if (ep->ExceptionRecord->ExceptionCode == 0xC00000FDu) {
        HANDLE t = CreateThread(NULL, 1 << 20, crash_worker, NULL, 0, NULL);
        if (t) { WaitForSingleObject(t, 60000); CloseHandle(t); }
    } else crash_worker(NULL);
}

static LONG WINAPI crash_uef(PEXCEPTION_POINTERS ep)
{
    LONG r = EXCEPTION_EXECUTE_HANDLER;      /* end the game quietly: our window replaces Windows' */
    crash_report_entry(ep);
    if (ep->ExceptionRecord->ExceptionCode == SLAM_TEST_CRASH) return r;   /* TestCloses=1 */
    if (g_prevUef && g_prevUef != crash_uef) {
        LONG p = g_prevUef(ep);
        if (p == EXCEPTION_CONTINUE_EXECUTION) return p;
    }
    return r;
}

/* the game (or its own crash handler) may set its filter later: stay in front, keep theirs chained */
static void crash_keep_filter(void)
{
    LPTOP_LEVEL_EXCEPTION_FILTER cur;
    if (!g_crEnabled || g_crashOnce) return;
    cur = SetUnhandledExceptionFilter(crash_uef);
    if (cur && cur != crash_uef) g_prevUef = cur;
}

/* delete old reports, keep the newest g_crKeep (names sort by date) */
static void crash_prune(void)
{
    static char names[64][40]; char pat[MAX_PATH], p[MAX_PATH]; WIN32_FIND_DATAA fd; HANDLE h; int n = 0, i, j, k;
    s_cpy(pat, g_slamDir, MAX_PATH); s_cat(pat, "crash\\crash_*.log", MAX_PATH);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do if (n < 64 && s_len(fd.cFileName) < 36) { s_cpy(names[n], fd.cFileName, 40); names[n][s_len(names[n]) - 4] = 0; n++; }
    while (FindNextFileA(h, &fd));
    FindClose(h);
    for (i = 1; i < n; i++) for (j = i; j > 0; j--) {                 /* oldest first */
        char a[40]; int less = 0;
        for (k = 0; names[j][k] || names[j - 1][k]; k++) if (names[j][k] != names[j - 1][k]) { less = (u8)names[j][k] < (u8)names[j - 1][k]; break; }
        if (!less) break;
        s_cpy(a, names[j], 40); s_cpy(names[j], names[j - 1], 40); s_cpy(names[j - 1], a, 40);
    }
    for (i = 0; i < n - g_crKeep; i++) {
        s_cpy(p, g_slamDir, MAX_PATH); s_cat(p, "crash\\", MAX_PATH); s_cat(p, names[i], MAX_PATH);
        k = s_len(p); s_cat(p, ".log", MAX_PATH); DeleteFileA(p);
        p[k] = 0; s_cat(p, ".dmp", MAX_PATH); DeleteFileA(p);
    }
}

/* from DllMain: only things that are safe under the loader lock */
static void crash_init(void)
{
    g_mainTid = GetCurrentThreadId();
    if (!g_crEnabled) return;
    AddVectoredExceptionHandler(1, crash_veh);
    crash_keep_filter();
}

/* at the game's entry point (load_all), outside the loader lock: DLL loading and file work */
static void crash_late_init(void)
{
    char dbg[MAX_PATH]; HMODULE dh = 0; UINT n;
    if (!g_crEnabled) { slog("crash report: off (SLAM.ini [Crash] Enabled=0)"); return; }
    /* the system's dbghelp.dll, loaded now: no DLL loading inside a crash */
    n = GetSystemDirectoryA(dbg, MAX_PATH);
    if (n && n < MAX_PATH - 14) { s_cat(dbg, "\\dbghelp.dll", MAX_PATH); dh = LoadLibraryA(dbg); }
    g_miniDump = dh ? (MiniDump_t)GetProcAddress(dh, "MiniDumpWriteDump") : 0;
    crash_prune();
    ft_install();             /* before the mods: SLAM's link is the innermost one (sees the final file names) */
    slog("crash report: on - reports go to SLAM\\crash\\ (%s%s%s)", g_miniDump ? "with minidump" : "no minidump: dbghelp.dll missing",
         g_crTestKey ? ", test key " : "", g_crTestKey ? g_crTestName : "");
}

/* every second from SLAM's background thread */
static void crash_tick(void)
{
    static u32 n;
    if (!g_crEnabled || g_crashOnce) return;
    crash_keep_filter();
    if (++n % 30 == 0) {
        u32 used, total, fr, big;
        crash_mem(&used, &total, &fr, &big);
        if ((big << 20) < g_lowFreeMax) g_lowFreeMax = big << 20;
        slog("memory: %u MB used of %u MB, largest free block %u MB%s", used, total, big,
             total && total < 2100 ? "  (2 GB limit: the exe is not large-address-aware)" : big < 64 ? "  - LOW: big loads will fail" : "");
    }
}

/* every 50 ms from SLAM's background thread: the test key */
static void crash_key_poll(void)
{
    static int keysDown;
    int down;
    if (!g_crEnabled || !g_crTestKey || g_crashOnce) return;
    down = (GetAsyncKeyState(g_crTestVk) & 0x8000) &&
           !!(GetAsyncKeyState(VK_CONTROL) & 0x8000) == !!(g_crTestMods & 1) &&
           !!(GetAsyncKeyState(VK_SHIFT) & 0x8000) == !!(g_crTestMods & 2) &&
           !!(GetAsyncKeyState(VK_MENU) & 0x8000) == !!(g_crTestMods & 4);
    if (down && !keysDown) {
        slog("test key %s pressed%s", g_crTestName, g_crTestCloses ? " - test crash: the game will close (TestCloses=1)" : "");
        if (g_crTestCloses) {
            InterlockedExchange(&g_crRealTest, 1);
            RaiseException(SLAM_TEST_CRASH, EXCEPTION_NONCONTINUABLE, 0, NULL);   /* -> crash_uef, like a real crash */
        } else {
            /* a report of this thread, then allow the next real crash to be reported */
            EXCEPTION_RECORD r; CONTEXT c; EXCEPTION_POINTERS ep;
            memset(&r, 0, sizeof(r)); memset(&c, 0, sizeof(c));
            c.ContextFlags = CONTEXT_FULL;
            RtlCaptureContext(&c);
            r.ExceptionCode = SLAM_TEST_CRASH; r.ExceptionAddress = (void *)c.Eip;
            ep.ExceptionRecord = &r; ep.ContextRecord = &c;
            crash_report_entry(&ep);
            InterlockedExchange(&g_crashOnce, 0);
        }
    }
    keysDown = down;
}
