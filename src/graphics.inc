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

static void graphics_guard(void)
{
    static const char *const kD3d9[] = { "reshade", "enbseries", "sweetfx", "d3d9sweet", "injectfx", "fxaa", 0 };
    static const char *const kDxgi[] = { "reshade", "enbseries", 0 };
    static const char *const kDxvk[] = { "dxvk", 0 };
    char d3d9[MAX_PATH], dxgi[MAX_PATH], sys[MAX_PATH], have[MAX_PATH], w1[32], w2[32];
    HMODULE m;
    s_cpy(d3d9, g_gameDir, MAX_PATH); s_cat(d3d9, "d3d9.dll", MAX_PATH);
    s_cpy(dxgi, g_gameDir, MAX_PATH); s_cat(dxgi, "dxgi.dll", MAX_PATH);
    if (!file_exists(d3d9) || !file_exists(dxgi)) return;
    if (!gx_file_has(dxgi, kDxgi, w2, sizeof w2)) { slog("graphics: dxgi.dll in the game folder is not ReShade/ENB - left alone"); return; }
    if (gx_file_has(d3d9, kDxvk, 0, 0) || !gx_file_has(d3d9, kD3d9, w1, sizeof w1)) {
        slog("graphics: d3d9.dll in the game folder is not a graphics wrapper (ASI loader / DXVK?) - dxgi.dll (%s) left alone", w2);
        return;
    }
    if (!g_cfgOneWrapper) {
        slog("graphics: TWO wrappers in the game folder - d3d9.dll (%s) and dxgi.dll (%s). Two ReShades crash the game. "
             "[Graphics] OneWrapper=0: both are used", w1, w2);
        return;
    }
    m = GetModuleHandleA("dxgi.dll");
    if (m) {
        have[0] = 0; GetModuleFileNameA(m, have, MAX_PATH);
        if (s_ieq(have, dxgi)) slog("graphics: TWO wrappers - d3d9.dll (%s) and dxgi.dll (%s); dxgi.dll was already loaded before SLAM could keep it out. Move dxgi.dll out of the game folder", w1, w2);
        else slog("graphics: two wrappers in the game folder, but Windows' dxgi.dll is already in use (%s) - the game folder's dxgi.dll will not load", have);
        return;
    }
    GetSystemDirectoryA(sys, MAX_PATH); s_cat(sys, "\\dxgi.dll", MAX_PATH);
    m = LoadLibraryA(sys);
    if (m) slog("graphics: TWO wrappers in the game folder - d3d9.dll (%s) and dxgi.dll (%s). Two ReShades crash the game: "
                "kept d3d9.dll (made for Direct3D 9 games like NBA 2K14); the game folder's dxgi.dll is NOT used "
                "(Windows' own dxgi.dll was loaded in its place). Nothing was moved or deleted. [Graphics] OneWrapper=0 = use both", w1, w2);
    else slog("graphics: TWO wrappers (d3d9.dll %s, dxgi.dll %s) - Windows' dxgi.dll could not be loaded (%s), both stay. Move dxgi.dll out of the game folder", w1, w2, sys);
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
