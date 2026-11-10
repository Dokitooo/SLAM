/* hooks.inc - shared hook + patch registry
 *
 * Function hooks use MinHook (vendored, BSD-2) for the actual code redirection. SLAM puts one
 * MinHook hook on a function and points it at a tiny relay "jmp [head]"; every mod that hooks
 * the same function becomes one link of a chain:
 *
 *     game calls target -> relay -> newest mod's detour -> (its *original) -> older mod's detour
 *                      -> ... -> MinHook trampoline -> the rest of the original function
 *
 * so mods never overwrite each other's hooks, and removing one link keeps the others working.
 * Byte patches are recorded with their owner and the true original bytes. A second mod writing
 * DIFFERENT bytes over the same addresses is refused and reported (both mod names) instead of
 * silently breaking the first mod.
 */
#include "MinHook.h"

typedef struct HookNode {
    u8 thunk[8];                    /* FF 25 &next : what the mod gets as *original */
    void *volatile next;            /* next link: older detour or the MinHook trampoline */
    void *detour;
    SlamMod *owner;
    int live;
    struct HookNode *link;          /* all links of this target, newest first */
} HookNode;

typedef struct HookTarget {
    u8 entry[8];                    /* FF 25 &head : MinHook sends the target here */
    void *volatile head;
    void *target;
    void *tramp;
    int foreign;                    /* target already started with a jump: someone outside SLAM hooked it first */
    HookNode *nodes;
    int iat;                        /* 1 = an import-table slot of nba2k14.exe (target = slot address) */
    int off;                        /* 1 = no live link left: the original code / slot value is back */
    char what[64];                  /* iat: "KERNEL32.dll!CreateFileA" */
} HookTarget;

typedef struct PatchRec {
    u32 addr, len;
    SlamMod *owner;
    int live, overwritten;
    u8 *orig, *bytes;
} PatchRec;

static CRITICAL_SECTION g_hookLock;
static u8 *g_pool; static u32 g_poolUsed;
#define POOL_SIZE 0x40000
static HookTarget *g_targets[1024]; static int g_targetN;
static PatchRec g_patches[8192]; static int g_patchN;
static char g_conflicts[32][300]; static int g_conflictN;
static int g_mhReady;

static void *pool_alloc(u32 n)
{
    void *p;
    if (!g_pool) g_pool = (u8 *)VirtualAlloc(NULL, POOL_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    n = (n + 15) & ~15u;
    if (!g_pool || g_poolUsed + n > POOL_SIZE) return 0;
    p = g_pool + g_poolUsed; g_poolUsed += n;
    return p;
}
static int write_bytes(u32 a, const u8 *b, u32 n);
static PatchRec *patch_overlap(u32 a, u32 n, SlamMod *notOwner);
static int in_pool(u32 a) { return g_pool && a >= (u32)g_pool && a < (u32)g_pool + POOL_SIZE; }

static void jmp_ind(u8 *at, void *slot) { at[0] = 0xFF; at[1] = 0x25; *(u32 *)(at + 2) = (u32)slot; at[6] = 0xCC; at[7] = 0xCC; }

static const char *owner_name(SlamMod *m) { return m ? m->name : "?"; }

static void add_conflict(const char *text)
{
    slog("CONFLICT %s", text);
    if (g_conflictN < 32) s_cpy(g_conflicts[g_conflictN++], text, 300);
}

static void hooks_init(void)
{
    InitializeCriticalSection(&g_hookLock);
    g_mhReady = MH_Initialize() == MH_OK;
    if (!g_mhReady) slog("hooks: ERROR the hook engine (MinHook) did not start - mods cannot hook functions");
}

static HookTarget *find_target(void *target)
{
    int i;
    for (i = 0; i < g_targetN; i++) if (g_targets[i]->target == target) return g_targets[i];
    return 0;
}

/* a live patch of another mod overlapping [a, a+n) */
static PatchRec *patch_overlap(u32 a, u32 n, SlamMod *notOwner)
{
    int i;
    for (i = 0; i < g_patchN; i++) {
        PatchRec *p = &g_patches[i];
        if (p->live && p->owner != notOwner && p->addr < a + n && a < p->addr + p->len) return p;
    }
    return 0;
}

/* does the function already start with a jump that looks like someone else's hook? Windows' own
   stubs (kernel32 -> kernelbase) also start with jumps; those land in a system DLL and are fine.
   Foreign = the jump lands in loose memory (a hook trampoline) or in a DLL from the game folder. */
static int jump_is_foreign(const u8 *p)
{
    u32 dest = 0, slot; HMODULE h; char path[MAX_PATH];
    if (p[0] == 0xE9) dest = (u32)p + 5 + *(const u32 *)(p + 1);
    else if (p[0] == 0xEB) dest = (u32)p + 2 + (u32)(int)(signed char)p[1];
    else if (p[0] == 0xFF && p[1] == 0x25) { slot = *(const u32 *)(p + 2); if (!mem_readable((void *)slot, 4)) return 1; dest = *(const u32 *)slot; }
    else if (p[0] == 0x68 && p[5] == 0xC3) dest = *(const u32 *)(p + 1);
    else return 0;
    h = mod_of(dest);
    if (!h) return 1;
    if (!GetModuleFileNameA(h, path, MAX_PATH)) return 1;
    path[s_base(path) - path] = 0;
    return s_ieq(path, g_gameDir) || (s_len(path) > s_len(g_gameDir) && !memcmp_i(path, g_gameDir, s_len(g_gameDir)));
}

/* put one more detour in front of a target's chain (g_hookLock held) */
static int chain_add(HookTarget *t, SlamMod *m, void *detour, void **original)
{
    HookNode *n, *o;
    for (o = t->nodes; o; o = o->link)
        if (o->live && o->detour == detour) return SLAM_ERR_ARGS;
    if (!(n = (HookNode *)pool_alloc(sizeof(HookNode)))) return SLAM_ERR_MEMORY;
    n->detour = detour; n->owner = m; n->live = 1;
    n->next = t->head;
    jmp_ind(n->thunk, (void *)&n->next);
    n->link = t->nodes; t->nodes = n;
    *original = n->thunk;                                   /* before the chain goes live */
    InterlockedExchange((volatile LONG *)&t->head, (LONG)detour);
    if (t->off) {                                           /* the redirect was taken out: put it back */
        if (t->iat) { u32 nv = (u32)t->entry; write_bytes((u32)t->target, (const u8 *)&nv, 4); }
        else MH_EnableHook(t->target);
        t->off = 0;
    }
    for (o = n->link; o; o = o->link)
        if (o->live && o->owner != &g_core) {      /* SLAM's own links (file trail) are not worth a line */
            if (t->iat) slog("hooks: %s (import of nba2k14.exe) is hooked by %s and %s - chained, %s runs first", t->what, owner_name(m), owner_name(o->owner), owner_name(m));
            else slog("hooks: %08X is hooked by %s and %s - chained, %s runs first", (u32)t->target, owner_name(m), owner_name(o->owner), owner_name(m));
            break;
        }
    return SLAM_OK;
}

static int hook_add(SlamMod *m, void *target, void *detour, void **original)
{
    HookTarget *t; PatchRec *p; MH_STATUS st;
    if (!target || !detour || !original) return SLAM_ERR_ARGS;
    if (!g_mhReady) return SLAM_ERR_HOOK;
    if (!mem_readable(target, 16)) return SLAM_ERR_MEMORY;
    EnterCriticalSection(&g_hookLock);
    if ((p = patch_overlap((u32)target, 5, m)) != 0) {
        char c[300];
        s_fmt(c, sizeof(c), "%s wants to hook %08X, but %s patched bytes there (%08X, %u bytes) - hook refused",
              owner_name(m), (u32)target, owner_name(p->owner), p->addr, p->len);
        add_conflict(c);
        LeaveCriticalSection(&g_hookLock);
        return SLAM_ERR_CONFLICT;
    }
    t = find_target(target);
    if (t && t->iat) { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_ARGS; }
    if (!t) {
        if (g_targetN >= 1024 || !(t = (HookTarget *)pool_alloc(sizeof(HookTarget)))) { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_MEMORY; }
        t->target = target;
        t->foreign = jump_is_foreign((u8 *)target);
        jmp_ind(t->entry, (void *)&t->head);
        st = MH_CreateHook(target, t->entry, &t->tramp);
        if (st != MH_OK) {
            slog("hooks: %s could not hook %08X (MinHook: %s)", owner_name(m), (u32)target, MH_StatusToString(st));
            LeaveCriticalSection(&g_hookLock);
            return SLAM_ERR_HOOK;
        }
        t->head = t->tramp;
        st = MH_EnableHook(target);
        if (st != MH_OK) {
            slog("hooks: %s could not enable the hook at %08X (MinHook: %s)", owner_name(m), (u32)target, MH_StatusToString(st));
            MH_RemoveHook(target);
            LeaveCriticalSection(&g_hookLock);
            return SLAM_ERR_HOOK;
        }
        g_targets[g_targetN++] = t;
        if (t->foreign) slog("hooks: %08X was already redirected by something outside SLAM - %s is chained in front of it", (u32)target, owner_name(m));
    }
    {
        int r = chain_add(t, m, detour, original);
        LeaveCriticalSection(&g_hookLock);
        return r;
    }
}

/* the import-table slot of nba2k14.exe for dll!fn (normal imports; 0 if not imported) */
static u32 *exe_import_slot(const char *dll, const char *fn)
{
    u8 *base = (u8 *)g_exeBase; u32 pe = *(u32 *)(base + 0x3C), imp = *(u32 *)(base + pe + 0x80), *d;
    if (!imp) return 0;
    for (d = (u32 *)(base + imp); d[3] && d[4]; d += 5) {
        u32 *names = (u32 *)(base + (d[0] ? d[0] : d[4])), *slots = (u32 *)(base + d[4]), k;
        if (!s_ieq((const char *)(base + d[3]), dll)) continue;
        for (k = 0; names[k]; k++)
            if (!(names[k] & 0x80000000u) && s_ieq((const char *)(base + names[k] + 2), fn)) return &slots[k];
    }
    return 0;
}

/* Hook what nba2k14.exe calls through its import table (e.g. KERNEL32.dll!CreateFileA): only the
   game's own calls go through the chain, not other DLLs'. Several mods chain like api->hook. */
static int hook_import(SlamMod *m, const char *dll, const char *fn, void *detour, void **original)
{
    u32 *slot; HookTarget *t;
    if (!dll || !fn || !detour || !original) return SLAM_ERR_ARGS;
    if (!(slot = exe_import_slot(dll, fn))) return SLAM_ERR_NOT_FOUND;
    EnterCriticalSection(&g_hookLock);
    t = find_target(slot);
    if (!t) {
        PatchRec *p = patch_overlap((u32)slot, 4, 0);
        if (p) {
            char c[300];
            s_fmt(c, sizeof(c), "%s wants to hook the import %s!%s, but %s patched that slot (%08X) - hook refused", owner_name(m), dll, fn, owner_name(p->owner), (u32)slot);
            add_conflict(c);
            LeaveCriticalSection(&g_hookLock);
            return SLAM_ERR_CONFLICT;
        }
        if (g_targetN >= 1024 || !(t = (HookTarget *)pool_alloc(sizeof(HookTarget)))) { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_MEMORY; }
        t->target = slot; t->iat = 1;
        s_cpy(t->what, dll, 64); s_cat(t->what, "!", 64); s_cat(t->what, fn, 64);
        t->tramp = (void *)*slot;                         /* what the game called until now */
        t->head = t->tramp;
        t->foreign = !(mod_of(*slot) && GetProcAddress(GetModuleHandleA(dll), fn) == (FARPROC)*slot);
        jmp_ind(t->entry, (void *)&t->head);
        {
            u32 nv = (u32)t->entry;
            if (!write_bytes((u32)slot, (const u8 *)&nv, 4)) { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_MEMORY; }
        }
        g_targets[g_targetN++] = t;
        if (t->foreign) slog("hooks: the import %s already pointed somewhere else (something outside SLAM) - %s is chained in front of it", t->what, owner_name(m));
    } else if (!t->iat) { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_ARGS; }
    {
        int r = chain_add(t, m, detour, original);
        LeaveCriticalSection(&g_hookLock);
        return r;
    }
}

static int hook_remove(SlamMod *m, void *target, void *detour)
{
    HookTarget *t; HookNode *n, *o; int r = SLAM_ERR_NOT_FOUND;
    EnterCriticalSection(&g_hookLock);
    t = find_target(target);
    for (n = t ? t->nodes : 0; n; n = n->link) {
        if (!n->live || n->detour != detour || n->owner != m) continue;
        if (t->head == detour) InterlockedExchange((volatile LONG *)&t->head, (LONG)n->next);
        else for (o = t->nodes; o; o = o->link) if (o->live && o->next == detour) { InterlockedExchange((volatile LONG *)&o->next, (LONG)n->next); break; }
        n->live = 0;                    /* its thunk stays valid for calls already inside it */
        r = SLAM_OK;
        for (o = t->nodes; o && !o->live; o = o->link) ;
        if (!o) {                       /* nobody hooks it any more: give the game its original code back */
            if (t->iat) { u32 ov = (u32)t->tramp; write_bytes((u32)t->target, (const u8 *)&ov, 4); }
            else MH_DisableHook(t->target);
            t->off = 1;
        }
        break;
    }
    LeaveCriticalSection(&g_hookLock);
    return r;
}

static int write_bytes(u32 a, const u8 *b, u32 n)
{
    DWORD old;
    if (!VirtualProtect((LPVOID)a, n, PAGE_EXECUTE_READWRITE, &old)) return 0;
    memcpy((void *)a, b, n);
    VirtualProtect((LPVOID)a, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), (LPCVOID)a, n);
    return 1;
}

static int patch_add(SlamMod *m, void *addrp, const void *bytesp, unsigned len)
{
    u32 a = (u32)addrp, i; const u8 *bytes = (const u8 *)bytesp; PatchRec *r; int k; HANDLE heap = GetProcessHeap();
    if (!a || !bytes || !len || len > 0x100000) return SLAM_ERR_ARGS;
    if (!mem_readable(addrp, len)) return SLAM_ERR_MEMORY;
    EnterCriticalSection(&g_hookLock);
    /* different bytes of another mod on the same addresses? */
    for (k = 0; k < g_patchN; k++) {
        PatchRec *p = &g_patches[k];
        if (!p->live || p->owner == m || p->addr >= a + len || a >= p->addr + p->len) continue;
        for (i = 0; i < len; i++) {
            u32 x = a + i;
            if (x >= p->addr && x < p->addr + p->len && p->bytes[x - p->addr] != bytes[i]) {
                char c[300];
                s_fmt(c, sizeof(c), "%s and %s both change %08X (%s: %u bytes at %08X, %s: %u bytes at %08X) - %s's change refused",
                      owner_name(p->owner), owner_name(m), x, owner_name(p->owner), p->len, p->addr, owner_name(m), len, a, owner_name(m));
                add_conflict(c);
                LeaveCriticalSection(&g_hookLock);
                return SLAM_ERR_CONFLICT;
            }
        }
    }
    /* bytes another mod's hook relies on? */
    for (k = 0; k < g_targetN; k++) {
        HookTarget *t = g_targets[k]; HookNode *n; u32 ta = (u32)t->target;
        if (ta >= a + len || a >= ta + 5) continue;
        for (n = t->nodes; n; n = n->link)
            if (n->live && n->owner != m) {
                char c[300];
                s_fmt(c, sizeof(c), "%s wants to patch %u bytes at %08X, but %s hooks the function at %08X - patch refused",
                      owner_name(m), len, a, owner_name(n->owner), ta);
                add_conflict(c);
                LeaveCriticalSection(&g_hookLock);
                return SLAM_ERR_CONFLICT;
            }
    }
    /* the same mod writing the same place again (e.g. re-applying after the game reset it): update in place */
    for (k = 0; k < g_patchN; k++) {
        PatchRec *p = &g_patches[k];
        if (p->live && p->owner == m && p->addr == a && p->len == len) {
            if (!write_bytes(a, bytes, len)) { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_MEMORY; }
            memcpy(p->bytes, bytes, len);
            p->overwritten = 0;
            LeaveCriticalSection(&g_hookLock);
            return SLAM_OK;
        }
    }
    if (g_patchN >= 8192) { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_MEMORY; }
    r = &g_patches[g_patchN];
    r->orig = (u8 *)HeapAlloc(heap, 0, len); r->bytes = (u8 *)HeapAlloc(heap, 0, len);
    if (!r->orig || !r->bytes) { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_MEMORY; }
    memcpy(r->orig, addrp, len);
    memcpy(r->bytes, bytes, len);
    /* the TRUE original of bytes that are already patched lives in the older record */
    for (k = 0; k < g_patchN; k++) {
        PatchRec *p = &g_patches[k];
        if (!p->live || p->addr >= a + len || a >= p->addr + p->len) continue;
        for (i = 0; i < len; i++) if (a + i >= p->addr && a + i < p->addr + p->len) r->orig[i] = p->orig[a + i - p->addr];
        if (p->owner == m && p->addr == a) p->live = 0;     /* same mod, same place: replaced */
    }
    if (!write_bytes(a, bytes, len)) { LeaveCriticalSection(&g_hookLock); return SLAM_ERR_MEMORY; }
    r->addr = a; r->len = len; r->owner = m; r->live = 1; r->overwritten = 0;
    g_patchN++;
    LeaveCriticalSection(&g_hookLock);
    return SLAM_OK;
}

static int patch_remove(SlamMod *m, void *addrp)
{
    u32 a = (u32)addrp, i; int k, j, r = SLAM_ERR_NOT_FOUND;
    EnterCriticalSection(&g_hookLock);
    for (k = g_patchN - 1; k >= 0; k--) {
        PatchRec *p = &g_patches[k];
        if (!p->live || p->owner != m || p->addr != a) continue;
        p->live = 0;
        for (i = 0; i < p->len; i++) {
            u32 x = a + i; int covered = 0;
            for (j = 0; j < g_patchN && !covered; j++)
                covered = g_patches[j].live && x >= g_patches[j].addr && x < g_patches[j].addr + g_patches[j].len;
            if (!covered) write_bytes(x, &p->orig[i], 1);
        }
        r = SLAM_OK;
        break;
    }
    LeaveCriticalSection(&g_hookLock);
    return r;
}

/* every 10 s: has something outside SLAM written over a mod's patch or an import hook? (logged once each) */
static void patches_verify(void)
{
    int k;
    EnterCriticalSection(&g_hookLock);
    for (k = 0; k < g_targetN; k++) {
        HookTarget *t = g_targets[k]; u32 now;
        if (!t->iat || t->off || t->foreign == 2) continue;
        now = *(volatile u32 *)t->target;
        if (now != (u32)t->entry) {
            u32 nv = (u32)t->entry;
            slog("WARNING the import %s was changed by something outside SLAM (now %08X) - put back", t->what, now);
            write_bytes((u32)t->target, (const u8 *)&nv, 4);
            t->foreign = 2;                                   /* report once */
        }
    }
    for (k = 0; k < g_patchN; k++) {
        PatchRec *p = &g_patches[k]; u32 i;
        if (!p->live || p->overwritten) continue;
        for (i = 0; i < p->len; i++)
            if (((u8 *)p->addr)[i] != p->bytes[i]) {
                p->overwritten = 1;
                slog("WARNING %s's patch at %08X (%u bytes) was changed by something outside SLAM (byte +%X is %02X, expected %02X)",
                     owner_name(p->owner), p->addr, p->len, i, ((u8 *)p->addr)[i], p->bytes[i]);
                break;
            }
    }
    LeaveCriticalSection(&g_hookLock);
}

/* crash report: which mods changed code within `range` bytes of address a ("" if none) */
static void owners_near(u32 a, u32 range, char *out, int cap)
{
    int k; out[0] = 0;
    for (k = 0; k < g_patchN; k++) {
        PatchRec *p = &g_patches[k];
        if (p->live && p->addr <= a + range && a <= p->addr + p->len + range && !s_inlist(out, p->owner->name)) {
            if (out[0]) s_cat(out, ", ", cap);
            s_cat(out, p->owner->name, cap);
        }
    }
    for (k = 0; k < g_targetN; k++) {
        HookTarget *t = g_targets[k]; HookNode *n; u32 ta = (u32)t->target;
        if (ta > a + range || a > ta + range) continue;
        for (n = t->nodes; n; n = n->link)
            if (n->live && !s_inlist(out, n->owner->name)) { if (out[0]) s_cat(out, ", ", cap); s_cat(out, n->owner->name, cap); }
    }
}

/* crash report: list of all hooks and patches */
static void hooks_report(void (*line)(const char *))
{
    int k; char b[400], who[300];
    for (k = 0; k < g_targetN; k++) {
        HookTarget *t = g_targets[k]; HookNode *n;
        who[0] = 0;
        for (n = t->nodes; n; n = n->link) if (n->live) { if (who[0]) s_cat(who, " -> ", 300); s_cat(who, n->owner->name, 300); }
        if (!who[0]) continue;
        {
            char where[120];
            HMODULE h = mod_of((u32)t->target); char path[MAX_PATH]; path[0] = 0;
            if (h) GetModuleFileNameA(h, path, MAX_PATH);
            s_fmt(where, sizeof(where), "%s+%X", h ? s_base(path) : "?", (u32)t->target - (u32)h);
            if (t->iat) s_fmt(b, sizeof(b), "    import %-34s %s%s", t->what, who, t->foreign ? "  (+ a hook from outside SLAM)" : "");
            else s_fmt(b, sizeof(b), "    hook  %08X %-28s %s%s", (u32)t->target, where, who, t->foreign ? "  (+ a hook from outside SLAM)" : "");
        }
        line(b);
    }
    for (k = 0; k < g_patchN; k++) {
        PatchRec *p = &g_patches[k];
        if (!p->live) continue;
        s_fmt(b, sizeof(b), "    patch %08X %5u bytes  %s%s", p->addr, p->len, p->owner->name, p->overwritten ? "  (later changed by something outside SLAM)" : "");
        line(b);
    }
}
