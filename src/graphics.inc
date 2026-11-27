/* graphics.inc - 1.1.1: one graphics wrapper only.
 *
 * NBA 2K14 is a Direct3D 9 game. ReShade (and similar) as d3d9.dll in the game folder works. Players often
 * also have a ReShade dxgi.dll there (from another game's install, or a newer ReShade that installs as
 * dxgi.dll). Windows' d3d9.dll loads dxgi.dll while it starts Direct3D, so the second ReShade loads too and
 * both hook the same device: the game crashes (seen in crash reports: d3d9.dll ReShade 4.9 + dxgi.dll 6.8).
 *
 * When BOTH are graphics wrappers (d3d9.dll = ReShade / ENB / SweetFX / FXAA injector, dxgi.dll = ReShade /
 * ENB), SLAM loads Windows' own dxgi.dll first, by its full path, at the game's entry point - before
 * Direct3D starts. Windows then gives that one to everybody who asks for "dxgi.dll", so the game folder's
 * dxgi.dll is never loaded. Nothing is deleted or renamed; d3d9.dll (the wrapper made for this game) is kept.
 * Anything else (an ASI loader named d3d9.dll, DXVK, only one wrapper) is left exactly as it is.
 *   SLAM.ini  [Graphics] OneWrapper=1  (0 = off: both load, as before)
 */

/* the file contains text t (case-insensitive)? reads the file once, up to 64 MB */
static int gx_file_has(const char *path, const char *const *t, char *which, int whichLen)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD size, got = 0; u8 *b; int hit = 0, i;
    if (which && whichLen) which[0] = 0;
    if (f == INVALID_HANDLE_VALUE) return 0;
    size = GetFileSize(f, NULL);
    if (size == INVALID_FILE_SIZE || size < 64 || size > 64u * 1024 * 1024) { CloseHandle(f); return 0; }
    b = (u8 *)VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!b) { CloseHandle(f); return 0; }
    if (ReadFile(f, b, size, &got, NULL) && got == size) {
        for (i = 0; t[i] && !hit; i++) {
            int L = s_len(t[i]); DWORD k;
            for (k = 0; k + (DWORD)L <= size; k++)
                if (s_lc((char)b[k]) == t[i][0] && !memcmp_i((const char *)b + k, t[i], L)) { hit = 1; break; }
            if (hit && which) s_cpy(which, t[i], whichLen);
        }
    }
    VirtualFree(b, 0, MEM_RELEASE);
    CloseHandle(f);
    return hit;
}

static int g_cfgOneWrapper = 1;
static void graphics_config(const char *ini) { g_cfgOneWrapper = GetPrivateProfileIntA("Graphics", "OneWrapper", 1, ini) != 0; }

/* 1.1.4: what a game-folder d3d9.dll / dxgi.dll is, for the log ("" = not a known graphics tool) */
static const char *gx_kind(const char *path)
{
    static const char *const kReshade[] = { "reshade", 0 }, *const kEnb[] = { "enbseries", 0 }, *const kDxvk[] = { "dxvk", 0 };
    static const char *const kSweet[] = { "injector.ini", "sweetfx", "injectfx", "shaderinjector", "injectsmaa", 0 };
    static const char *const kFxaa[] = { "fxaa", "d3d9sweet", 0 };
    if (!file_exists(path)) return 0;
    if (gx_file_has(path, kDxvk, 0, 0)) return "DXVK";
    if (gx_file_has(path, kReshade, 0, 0)) return "ReShade";
    if (gx_file_has(path, kEnb, 0, 0)) return "ENB";
    if (gx_file_has(path, kSweet, 0, 0)) return "SweetFX (SMAA/FXAA injector)";
    if (gx_file_has(path, kFxaa, 0, 0)) return "FXAA injector";
    return "";
}

/* 1.1.1: two wrappers -> keep d3d9.dll. 1.1.4: ANY graphics wrapper as dxgi.dll is kept out.
 * NBA 2K14 is Direct3D 9 and never uses dxgi.dll itself - but Windows' d3d9.dll (Windows 10/11) loads dxgi.dll while
 * Direct3D starts, so a dxgi.dll in the game folder ends up inside Direct3D 9: a second ReShade, or SweetFX's 2012
 * SMAA injector dxgi.dll (it rewrites CreateDXGIFactory for Direct3D 10/11 games - SweetFX packs ship it next to
 * their d3d9.dll and players copy both) - and the game crashes or never shows its window. So SLAM loads Windows' own
 * dxgi.dll first, by its full path, at the game's entry point: the game folder's dxgi.dll is never loaded, nothing
 * is moved or deleted. d3d9.dll (ReShade / ENB / SweetFX / FXAA injector made for D3D9 games) works as without SLAM.
 * DXVK and unknown dxgi.dll files are left alone. [Graphics] OneWrapper=0 = off. */
static void graphics_guard(void)
{
    char d3d9[MAX_PATH], dxgi[MAX_PATH], sys[MAX_PATH], have[MAX_PATH];
    const char *k9, *kx;
    HMODULE m;
    s_cpy(d3d9, g_gameDir, MAX_PATH); s_cat(d3d9, "d3d9.dll", MAX_PATH);
    s_cpy(dxgi, g_gameDir, MAX_PATH); s_cat(dxgi, "dxgi.dll", MAX_PATH);
    k9 = gx_kind(d3d9);
    if (k9) slog("graphics: d3d9.dll in the game folder: %s", k9[0] ? k9 : "not a known graphics tool (ASI loader?)");
    kx = gx_kind(dxgi);
    if (!kx) return;
    if (!kx[0]) { slog("graphics: dxgi.dll in the game folder is not a known graphics tool - left alone"); return; }
    if (s_ieq(kx, "DXVK")) { slog("graphics: dxgi.dll in the game folder is DXVK - left alone"); return; }
    if (!g_cfgOneWrapper) {
        slog("graphics: dxgi.dll in the game folder is %s - a Direct3D 10/11 tool inside this Direct3D 9 game%s. "
             "[Graphics] OneWrapper=0: it is used", kx, k9 && k9[0] ? " (next to the d3d9.dll one)" : "");
        return;
    }
    m = GetModuleHandleA("dxgi.dll");
    if (m) {
        have[0] = 0; GetModuleFileNameA(m, have, MAX_PATH);
        if (s_ieq(have, dxgi)) slog("graphics: dxgi.dll (%s) was already loaded before SLAM could keep it out. Move dxgi.dll out of the game folder", kx);
        else slog("graphics: dxgi.dll in the game folder (%s) will not load - Windows' dxgi.dll is already in use (%s)", kx, have);
        return;
    }
    GetSystemDirectoryA(sys, MAX_PATH); s_cat(sys, "\\dxgi.dll", MAX_PATH);
    m = LoadLibraryA(sys);
    if (m) slog("graphics: dxgi.dll in the game folder is %s - NBA 2K14 is Direct3D 9 and never needs it, but Windows' d3d9 "
                "would load it into Direct3D 9 (crash / no window). It is NOT used: Windows' own dxgi.dll was loaded in its place. "
                "Nothing was moved or deleted; d3d9.dll%s%s%s works as usual. [Graphics] OneWrapper=0 = use it anyway", kx,
                k9 && k9[0] ? " (" : "", k9 && k9[0] ? k9 : "", k9 && k9[0] ? ")" : "");
    else slog("graphics: dxgi.dll in the game folder is %s - Windows' dxgi.dll could not be loaded (%s), it stays. Move dxgi.dll out of the game folder", kx, sys);
}

/* at game ready: which dxgi.dll is really in use (for the log) */
static void graphics_check(void)
{
    char have[MAX_PATH], dxgi[MAX_PATH]; HMODULE m = GetModuleHandleA("dxgi.dll");
    if (!m) return;
    have[0] = 0; GetModuleFileNameA(m, have, MAX_PATH);
    s_cpy(dxgi, g_gameDir, MAX_PATH); s_cat(dxgi, "dxgi.dll", MAX_PATH);
    if (s_ieq(have, dxgi)) slog("graphics: dxgi.dll in use is the game folder's (%s)", have);
}
