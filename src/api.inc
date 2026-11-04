/* api.inc - the SLAM_API table every SLAM mod receives in SlamModStart
 *
 * Rule that keeps old mods working with every SLAM 1.x: fields are only ever ADDED at the end
 * of SLAM_API (slam.h), never removed, reordered or changed. A mod built for API 1 runs on any
 * later SLAM; a mod that needs a newer API is refused with a SLAM.log line (loader.inc).
 */

/* a SlamMod* coming from a mod: only ours are accepted */
static int mod_ok(SlamMod *m)
{
    return m == &g_core || (m >= &g_mods[0] && m < &g_mods[g_modN]);
}

static const char *SLAM_CALL api_game_dir(void) { return g_gameDir; }
static const char *SLAM_CALL api_mod_dir(SlamMod *m) { return mod_ok(m) ? m->dir : g_gameDir; }
static const char *SLAM_CALL api_mod_ini(SlamMod *m) { return mod_ok(m) ? m->ini : ""; }

static void SLAM_CALL api_log(SlamMod *m, const char *fmt, ...)
{
    char text[1100]; va_list ap;
    if (!fmt) return;
    va_start(ap, fmt); s_vfmt(text, sizeof(text), fmt, ap); va_end(ap);
    mod_log_line(mod_ok(m) && m != &g_core ? m : 0, text);
}

static int SLAM_CALL api_hook(SlamMod *m, void *target, void *detour, void **original)
{
    int r;
    if (!mod_ok(m)) return SLAM_ERR_ARGS;
    r = hook_add(m, target, detour, original);
    if (r == SLAM_OK) InterlockedIncrement(&m->nHooks);
    else slog("hooks: %s: hook at %08X failed (%d)", m->name, (u32)target, r);
    return r;
}
static int SLAM_CALL api_unhook(SlamMod *m, void *target, void *detour)
{
    int r;
    if (!mod_ok(m)) return SLAM_ERR_ARGS;
    r = hook_remove(m, target, detour);
    if (r == SLAM_OK) InterlockedDecrement(&m->nHooks);
    return r;
}
static int SLAM_CALL api_patch(SlamMod *m, void *addr, const void *bytes, unsigned len)
{
    int r;
    if (!mod_ok(m)) return SLAM_ERR_ARGS;
    r = patch_add(m, addr, bytes, len);
    if (r == SLAM_OK) InterlockedIncrement(&m->nPatches);
    else if (r != SLAM_ERR_CONFLICT) slog("patches: %s: %u bytes at %08X failed (%d)", m->name, len, (u32)addr, r);
    return r;
}
static int SLAM_CALL api_unpatch(SlamMod *m, void *addr)
{
    int r;
    if (!mod_ok(m)) return SLAM_ERR_ARGS;
    r = patch_remove(m, addr);
    if (r == SLAM_OK) InterlockedDecrement(&m->nPatches);
    return r;
}
static int SLAM_CALL api_read(const void *addr, void *out, unsigned len)
{
    if (!addr || !out || !mem_readable(addr, len)) return 0;
    memcpy(out, addr, len);
    return 1;
}

static int SLAM_CALL api_on_game_ready(SlamMod *m, SlamReadyCb cb, void *user)
{
    int now = 0;
    if (!mod_ok(m) || !cb) return SLAM_ERR_ARGS;
    EnterCriticalSection(&g_hookLock);
    if (g_ready) now = 1;
    else if (g_readyN < 128) { g_readyCb[g_readyN] = cb; g_readyUser[g_readyN] = user; g_readyMod[g_readyN] = m; g_readyN++; }
    else { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_MEMORY; }
    LeaveCriticalSection(&g_hookLock);
    if (now) cb(user);
    return SLAM_OK;
}

static int SLAM_CALL api_find_mod(const char *name, char *versionOut, unsigned cap)
{
    int i;
    if (versionOut && cap) versionOut[0] = 0;
    if (!name) return 0;
    for (i = 0; i < g_modN; i++) {
        SlamMod *m = &g_mods[i];
        if (m->isSlam && m->started && !m->failed && s_ieq(m->name, name)) {
            if (versionOut && cap) s_cpy(versionOut, m->version, (int)cap);
            return 1;
        }
    }
    return 0;
}
static void *SLAM_CALL api_game_base(void) { return (void *)g_exeBase; }
static unsigned SLAM_CALL api_exe_timestamp(void) { return g_exeTs; }
static int SLAM_CALL api_game_is_ready(void) { return g_ready != 0; }
static void SLAM_CALL api_crash_note(SlamMod *m, const char *text)
{
    if (!mod_ok(m)) return;
    if (!text) { m->crashNote[0] = 0; return; }
    s_cpy(m->crashNote, text, sizeof(m->crashNote));
}

static int SLAM_CALL api_hook_import(SlamMod *m, const char *dll, const char *fn, void *detour, void **original)
{
    int r;
    if (!mod_ok(m)) return SLAM_ERR_ARGS;
    r = hook_import(m, dll, fn, detour, original);
    if (r == SLAM_OK) InterlockedIncrement(&m->nHooks);
    else if (r != SLAM_ERR_CONFLICT) slog("hooks: %s: import hook %s!%s failed (%d)", m->name, dll ? dll : "?", fn ? fn : "?", r);
    return r;
}
static int SLAM_CALL api_unhook_import(SlamMod *m, const char *dll, const char *fn, void *detour)
{
    u32 *slot; int r;
    if (!mod_ok(m) || !dll || !fn) return SLAM_ERR_ARGS;
    if (!(slot = exe_import_slot(dll, fn))) return SLAM_ERR_NOT_FOUND;
    r = hook_remove(m, slot, detour);
    if (r == SLAM_OK) InterlockedDecrement(&m->nHooks);
    return r;
}

static const SLAM_API g_api = {
    sizeof(SLAM_API), SLAM_API_VERSION, SLAM_VERSION,
    api_game_dir, api_mod_dir, api_mod_ini,
    api_log,
    api_hook, api_unhook, api_patch, api_unpatch, api_read,
    api_on_game_ready,
    api_find_mod, api_game_base, api_exe_timestamp, api_game_is_ready,
    api_crash_note,
    api_hook_import, api_unhook_import,
};
