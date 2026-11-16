/* cause.inc - turns the [facts] block of a SLAM crash report into a plain-language explanation
 *
 * Pure C, no Windows calls: the same file is built into SLAMCrash.exe and into the Linux test
 * (tools/test_cause). Rules are checked from most to least specific; the first rule that fits
 * gives the headline, later rules may add "also" lines.
 *
 * The report can also hold a [findings] block, added by SLAMCrash.exe when it opens the
 * report: the changed game files the game loaded this session (RED MC-edited, or changed after the
 * game was installed), recent problem lines from the mods' logs, and probe/test builds of mods.
 * cause_extra() reads it; cause_guess() uses it when it is there (it may be missing: older report,
 * SLAMCrash.exe not run, FileTrail=0).
 *
 * Output (cause_guess):
 *     sure        2 = likely, 1 = possible, 0 = unclear
 *     headline    one short sentence
 *     body        a few sentences, \r\n line breaks, then "What you can try:" bullets
 *     blame       mod name(s) to send the report to ("" = none in particular)
 */

#define CAUSE_MAX_FACTS 64

typedef struct { char key[40]; char val[600]; } CauseFact;
typedef struct { CauseFact f[CAUSE_MAX_FACTS]; int n; } CauseFacts;
typedef struct { int sure; char headline[300]; char body[7000]; char blame[300]; } CauseResult;

/* ---- tiny string helpers (no CRT) ---- */
static int  c_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static char c_lc(char c) { return (char)(c >= 'A' && c <= 'Z' ? c + 32 : c); }
static int  c_ieq(const char *a, const char *b) { while (*a && c_lc(*a) == c_lc(*b)) { a++; b++; } return c_lc(*a) == c_lc(*b); }
static int  c_istarts(const char *s, const char *p) { while (*p) { if (c_lc(*s) != c_lc(*p)) return 0; s++; p++; } return 1; }
static void c_cat(char *d, const char *s, int cap) { int n = c_len(d); while (*s && n < cap - 1) d[n++] = *s++; d[n] = 0; }
static unsigned c_hex(const char *s) { unsigned v = 0; for (; *s; s++) { char c = c_lc(*s); if (c >= '0' && c <= '9') v = v * 16 + (unsigned)(c - '0'); else if (c >= 'a' && c <= 'f') v = v * 16 + (unsigned)(c - 'a' + 10); else break; } return v; }
static unsigned c_dec(const char *s) { unsigned v = 0; for (; *s >= '0' && *s <= '9'; s++) v = v * 10 + (unsigned)(*s - '0'); return v; }
static void c_utoa(char *d, unsigned v) { char t[12]; int n = 0, i = 0; do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v); while (n) d[i++] = t[--n]; d[i] = 0; }

/* ---- parse "[facts] ... [/facts]" out of the whole report text ---- */
static void cause_parse(const char *text, CauseFacts *fx)
{
    const char *p = text; int in = 0;
    fx->n = 0;
    while (*p) {
        const char *e = p; int n;
        while (*e && *e != '\n' && *e != '\r') e++;
        n = (int)(e - p);
        if (n == 7 && c_istarts(p, "[facts]")) in = 1;
        else if (n == 8 && c_istarts(p, "[/facts]")) break;
        else if (in && fx->n < CAUSE_MAX_FACTS) {
            const char *eq = p; while (eq < e && *eq != '=') eq++;
            if (eq < e && eq - p < 40) {
                CauseFact *f = &fx->f[fx->n++]; int k = 0;
                for (; p + k < eq; k++) f->key[k] = p[k];
                f->key[k] = 0;
                for (k = 0, eq++; eq < e && k < 599; eq++) f->val[k++] = *eq;
                f->val[k] = 0;
            }
        }
        while (*e == '\r' || *e == '\n') e++;
        p = e;
    }
}
static const char *fact(const CauseFacts *fx, const char *key)
{
    int i;                                      /* the last one wins */
    for (i = fx->n - 1; i >= 0; i--) if (c_ieq(fx->f[i].key, key)) return fx->f[i].val;
    return "";
}
static unsigned fact_u(const CauseFacts *fx, const char *key) { return c_dec(fact(fx, key)); }
static unsigned fact_x(const CauseFacts *fx, const char *key) { return c_hex(fact(fx, key)); }

/* ---- [findings] (SLAMCrash.exe) ---- */
#define CX_FILES 48
#define CX_LOGS 16
typedef struct { unsigned ageS; int redmc, changed, missing, crashThread; char when[24]; char path[180]; } CxFile;
typedef struct { char mod[64]; unsigned ageS; char text[200]; } CxLog;
typedef struct { CxFile f[CX_FILES]; int nf; CxLog l[CX_LOGS]; int nl; char probes[300]; int have; } CauseExtra;

/* copy field k (0-based, '|'-separated) of the line p..e; the last field takes the rest of the line */
static void cx_field(const char *p, const char *e, int k, int last, char *out, int cap)
{
    int n = 0;
    while (k && p < e) { if (*p == '|') k--; p++; }
    while (p < e && (last || *p != '|') && n < cap - 1) out[n++] = *p++;
    out[n] = 0;
}
static void cause_extra(const char *text, CauseExtra *cx)
{
    const char *p = text; int in = 0;
    cx->nf = cx->nl = cx->have = 0; cx->probes[0] = 0;
    while (*p) {
        const char *e = p; int n;
        while (*e && *e != '\n' && *e != '\r') e++;
        n = (int)(e - p);
        if (n == 10 && c_istarts(p, "[findings]")) { in = 1; cx->have = 1; }
        else if (n == 11 && c_istarts(p, "[/findings]")) break;
        else if (in && n > 5) {
            char t[24];
            if (c_istarts(p, "file|") && cx->nf < CX_FILES) {
                CxFile *f = &cx->f[cx->nf++]; char fl[16]; int i;
                cx_field(p, e, 1, 0, t, sizeof(t)); f->ageS = c_dec(t);
                cx_field(p, e, 2, 0, fl, sizeof(fl));
                f->redmc = f->changed = f->missing = f->crashThread = 0;
                for (i = 0; fl[i]; i++) { if (fl[i] == 'R') f->redmc = 1; if (fl[i] == 'C') f->changed = 1; if (fl[i] == 'M') f->missing = 1; if (fl[i] == 'T') f->crashThread = 1; }
                cx_field(p, e, 3, 0, f->when, sizeof(f->when));
                cx_field(p, e, 4, 1, f->path, sizeof(f->path));
            } else if (c_istarts(p, "log|") && cx->nl < CX_LOGS) {
                CxLog *l = &cx->l[cx->nl++];
                cx_field(p, e, 1, 0, l->mod, sizeof(l->mod));
                cx_field(p, e, 2, 0, t, sizeof(t)); l->ageS = c_dec(t);
                cx_field(p, e, 3, 1, l->text, sizeof(l->text));
            } else if (c_istarts(p, "probe|")) {
                char m[80]; cx_field(p, e, 1, 1, m, sizeof(m));
                if (cx->probes[0]) c_cat(cx->probes, ",", sizeof(cx->probes));
                c_cat(cx->probes, m, sizeof(cx->probes));
            }
        }
        while (*e == '\r' || *e == '\n') e++;
        p = e;
    }
}
static const char *c_basename(const char *p) { const char *b = p; for (; *p; p++) if (*p == '\\' || *p == '/') b = p + 1; return b; }
static void c_age(char *out, unsigned s)          /* "12 s" / "3 min" / "2 h" */
{
    char n[12];
    if (s < 120) { c_utoa(n, s); c_cat(out, n, 40); c_cat(out, " s", 40); }
    else if (s < 7200) { c_utoa(n, s / 60); c_cat(out, n, 40); c_cat(out, " min", 40); }
    else { c_utoa(n, s / 3600); c_cat(out, n, 40); c_cat(out, " h", 40); }
}

/* "A,B,C" -> "A, B and C" */
static void c_nice_list(char *out, const char *list, int cap)
{
    char items[8][64]; int n = 0, k;
    while (*list && n < 8) {
        while (*list == ' ' || *list == ',') list++;
        for (k = 0; *list && *list != ',' && k < 63; list++) items[n][k++] = *list;
        while (k && items[n][k - 1] == ' ') k--;
        items[n][k] = 0;
        if (k) n++;
    }
    out[0] = 0;
    for (k = 0; k < n; k++) {
        if (k) c_cat(out, k == n - 1 ? " and " : ", ", cap);
        c_cat(out, items[k], cap);
    }
}
/* the note a mod left with api->crash_note ("BodyHook: loading body | CrowdTune: ...") */
static void c_note_of(char *out, const char *notes, const char *mod, int cap)
{
    const char *p = notes; int n = c_len(mod);
    out[0] = 0;
    while (*p) {
        if (c_istarts(p, mod) && p[n] == ':' && p[n + 1] == ' ') {
            int k = 0; p += n + 2;
            while (*p && !(p[0] == ' ' && p[1] == '|') && k < cap - 1) out[k++] = *p++;
            out[k] = 0;
            return;
        }
        while (*p && *p != '|') p++;
        while (*p == '|' || *p == ' ') p++;
    }
}

/* ---- 1.1.0: versions ("BodyHook 2.2.1,SigExt 1.10.4,...") and the extra DLLs ---- */
static int c_ver_cmp(const char *a, const char *b)     /* "1.10.4" vs "1.10.3": <0, 0, >0 (missing parts = 0) */
{
    while (*a || *b) {
        unsigned x = c_dec(a), y = c_dec(b);
        if (x != y) return x < y ? -1 : 1;
        while (*a >= '0' && *a <= '9') a++;
        while (*b >= '0' && *b <= '9') b++;
        if (*a == '.') a++; else if (*a) break;
        if (*b == '.') b++; else if (*b) break;
    }
    return 0;
}
static int c_mod_ver(const char *mods, const char *name, char *ver, int cap)   /* 1 = the mod is in the list */
{
    const char *p = mods; int n = c_len(name);
    ver[0] = 0;
    while (*p) {
        if (c_istarts(p, name) && p[n] == ' ') {
            int k = 0; p += n + 1;
            while (*p == 'v' || *p == 'V') p++;
            while (*p && *p != ',' && k < cap - 1) ver[k++] = *p++;
            ver[k] = 0;
            return 1;
        }
        while (*p && *p != ',') p++;
        while (*p == ',') p++;
    }
    return 0;
}
/* the mod is there and older than 'fixed' (a version without digits counts as old: "TESTBUILD ...") */
static int c_mod_older(const char *mods, const char *name, const char *fixed)
{
    char v[40]; const char *d;
    if (!c_mod_ver(mods, name, v, sizeof(v))) return 0;
    for (d = v; *d && (*d < '0' || *d > '9'); d++) ;
    return !*d || c_ver_cmp(d, fixed) < 0;
}
#define CX_DLLS 48
typedef struct { char name[48]; unsigned size; char where; } CxDll;
typedef struct { CxDll d[CX_DLLS]; int n; } CauseDlls;
static void cause_dlls(const char *list, CauseDlls *dl)
{
    dl->n = 0;
    while (*list && dl->n < CX_DLLS) {
        CxDll *d = &dl->d[dl->n]; int k = 0;
        while (*list && *list != ':' && *list != ',' && k < 47) d->name[k++] = *list++;
        d->name[k] = 0; d->size = 0; d->where = 'o';
        if (*list == ':') { list++; d->size = c_hex(list); while (*list && *list != ':' && *list != ',') list++; }
        if (*list == ':') { list++; d->where = *list; }
        while (*list && *list != ',') list++;
        while (*list == ',') list++;
        if (k) dl->n++;
    }
}
static const CxDll *dll_find(const CauseDlls *dl, const char *name, char where)   /* where 0 = anywhere */
{
    int i;
    for (i = 0; i < dl->n; i++) if (c_ieq(dl->d[i].name, name) && (!where || dl->d[i].where == where)) return &dl->d[i];
    return 0;
}
static int c_in_list(const char *list, const char *item)   /* "A,B,C" contains item (case-insensitive) */
{
    int n = c_len(item);
    while (*list) {
        while (*list == ',' || *list == ' ') list++;
        if (c_istarts(list, item) && (list[n] == 0 || list[n] == ',')) return 1;
        while (*list && *list != ',') list++;
    }
    return 0;
}

/* ---- module families ---- */
static int is_gpu_module(const char *m)
{
    static const char *g[] = { "d3d9.dll", "d3dx9_", "nvd3dum", "nvwgf2um", "nvldumd", "nvgpucomp", "atiumdag", "aticfx32", "atidxx32",
                               "amdxc32", "atiu9pag", "igdumd32", "igdumdim32", "igd9dxva32", "igd9trinity", "igdusc32", "dxgi.dll", "d3d11.dll", 0 };
    int i;
    for (i = 0; g[i]; i++) if (c_istarts(m, g[i])) return 1;
    return 0;
}
static int is_overlay_module(const char *m)   /* recorders, overlays, injectors that hook Direct3D */
{
    static const char *o[] = { "rtsshooks", "gameoverlayrenderer", "discordhook", "nvspcap", "obs-", "graphics-hook", "ow-graphics",
                               "owclient", "bdcam", "fraps", "dxtory", "reshade", "enbseries", "overlay", "medal", 0 };
    int i;
    for (i = 0; o[i]; i++) if (c_istarts(m, o[i])) return 1;
    return 0;
}
static int is_audio_module(const char *m)
{
    return c_istarts(m, "xaudio") || c_istarts(m, "x3daudio") || c_istarts(m, "dsound") || c_istarts(m, "mmdevapi") || c_istarts(m, "audioses");
}

/* ---- the rules ---- */
#define NL "\r\n"
static void say(CauseResult *r, const char *s) { c_cat(r->body, s, sizeof(r->body)); }
static void tip(CauseResult *r, const char *s) { c_cat(r->body, "  - ", sizeof(r->body)); c_cat(r->body, s, sizeof(r->body)); c_cat(r->body, NL, sizeof(r->body)); }
static void tryit(CauseResult *r) { say(r, NL "Try:" NL); }
static void head(CauseResult *r, int sure, const char *s1, const char *s2, const char *s3)
{
    r->sure = sure; r->headline[0] = 0;
    c_cat(r->headline, s1, sizeof(r->headline)); if (s2) c_cat(r->headline, s2, sizeof(r->headline)); if (s3) c_cat(r->headline, s3, sizeof(r->headline));
}
static void blame(CauseResult *r, const char *list) { if (!r->blame[0]) c_cat(r->blame, list, sizeof(r->blame)); }

/* the changed files (the crashing thread's first, then most recently changed), as bullet lines */
static void cx_file_lines(CauseResult *r, const CauseExtra *cx, int max)
{
    int i, pass, shown = 0; char t[400], a[40];
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < cx->nf && shown < max; i++) {
            const CxFile *f = &cx->f[i];
            if (pass == 0 ? !f->crashThread : f->crashThread) continue;
            t[0] = 0; a[0] = 0; c_age(a, f->ageS);
            c_cat(t, c_basename(f->path), sizeof(t));
            c_cat(t, f->redmc ? " (RED MC-edited" : " (changed", sizeof(t));
            if (f->changed && f->when[0]) { c_cat(t, " ", sizeof(t)); c_cat(t, f->when, sizeof(t)); }
            c_cat(t, "), loaded ", sizeof(t)); c_cat(t, a, sizeof(t)); c_cat(t, " before", sizeof(t));
            if (f->crashThread) c_cat(t, " by the crashing thread", sizeof(t));
            tip(r, t); shown++;
        }
    if (cx->nf > shown) { char n[12]; c_utoa(n, (unsigned)(cx->nf - shown)); say(r, "  - ... "); say(r, n); say(r, " more at the end of the report" NL); }
}

/* ---- known crashes of nba2k14.exe 52202C7F (from real reports) ---- */
static int known_crash(const CauseFacts *fx, const CauseExtra *cx, CauseResult *r, unsigned eip, int *filesShown)
{
    const char *mods = fact(fx, "mods");
    char v[40];
    /* MyCareer per-game player table full (40/40) - post-game interview / recap */
    if (eip >= 0x617280 && eip < 0x617600) {
        int fixed = c_mod_ver(mods, "SigExt", v, sizeof(v)) && !c_mod_older(mods, "SigExt", "1.10.4");
        head(r, 2, "Known NBA 2K14 bug: MyCareer's player table for the game was full (post-game interview / recap).", 0, 0);
        if (fixed) {
            say(r, "Your SigExt already has the fix for this, so this is a new variant." NL);
            tryit(r); tip(r, "Send this report (and the .dmp) to the SigExt author.");
            blame(r, "SigExt");
        } else {
            tryit(r); tip(r, "Update the NBA2K27 ANIMATIONS pack (SigExt 1.10.4 or newer fixes it).");
        }
        return 1;
    }
    /* the game's file loader on a record that was overwritten - broken (often RED MC-edited) file */
    if ((eip >= 0xDD3000 && eip < 0xDD3200) || eip == 0xDA83E3 || eip == 0xDA6EC0 || eip == 0xDA51EE || (eip >= 0xD9B600 && eip < 0xD9B700)) {
        head(r, 2, "The game loaded a broken game file (its file loader crashed).", 0, 0);
        say(r, "Usually an edited HUD, arena/floor, shoe, player-create or face file." NL);
        if (cx->nf) { say(r, "Changed files loaded this session:" NL); cx_file_lines(r, cx, 4); *filesShown = 1; }
        tryit(r);
        tip(r, "Put back the original of the first file above, then the next, until it stops.");
        tip(r, "Restart the game every 1-2 hours.");
        return 1;
    }
    /* cloth simulation on a broken cloth object */
    if (eip >= 0x459E00 && eip < 0x45B000) {
        if (c_mod_older(mods, "BodyHook", "2.2.1")) {
            head(r, 2, "Known BodyHook bug (bench cloth), fixed in BodyHook 2.2.1.", 0, 0);
            tryit(r); tip(r, "Update BodyHook.");
        } else if (c_mod_ver(mods, "BodyHook", v, sizeof(v))) {
            head(r, 1, "The cloth simulation got a broken cloth object (BodyHook's own cloth or the bench).", 0, 0);
            tryit(r);
            tip(r, "BodyHook.ini [Bench] Lite=1, and put back the original cloth_*.iff files.");
            tip(r, "Send this report (and the .dmp) to the BodyHook author.");
            blame(r, "BodyHook");
        } else {
            head(r, 1, "The cloth simulation got a broken cloth object.", 0, 0);
            tryit(r); tip(r, "Put back the original cloth_*.iff files and jersey files.");
        }
        return 1;
    }
    /* uniform cloth freed twice (game teardown) */
    if (eip >= 0xBDF300 && eip < 0xBDFC00) {
        if (c_mod_older(mods, "BodyHook", "2.2.1")) {
            head(r, 1, "Uniform cloth crash - likely the BodyHook bench cloth bug fixed in 2.2.1.", 0, 0);
            tryit(r); tip(r, "Update BodyHook.");
        } else {
            head(r, 1, "NBA 2K14 bug with a uniform's cloth (also happens without mods).", 0, 0);
            tryit(r); tip(r, "Another roster or other uniforms for the teams in that game.");
        }
        return 1;
    }
    if (eip == 0xC59E6F) {
        head(r, 1, "The game crashed building a player's pose (player create / face files).", 0, 0);
        tryit(r);
        tip(r, "Put back the original of edited png*.iff files.");
        if (c_mod_ver(mods, "BodyHook", v, sizeof(v))) tip(r, "BodyHook.ini [Wingspan] Enabled=0 to test, then without BodyHook.");
        return 1;
    }
    if (eip == 0x56EC46) {
        head(r, 2, "global.iff is broken: a row the game needs at start is missing.", 0, 0);
        tryit(r); tip(r, "Put back the original global.iff (or the global your mods came with).");
        return 1;
    }
    if (eip >= 0xDB9400 && eip < 0xDB9800) {
        head(r, 2, "A game file (.iff) was saved with cut rows (old IFF Mod Studio compact).", 0, 0);
        if (cx->nf) { say(r, "Changed files loaded this session:" NL); cx_file_lines(r, cx, 3); *filesShown = 1; }
        tryit(r); tip(r, "Put back the original file, or re-save it with the current IFF Mod Studio.");
        return 1;
    }
    if (eip == 0xA88190) {
        head(r, 1, "A team has no point guard on its roster (often after Edit Player / trades).", 0, 0);
        tryit(r); tip(r, "Check every team has a PG in its lineup, or load the roster from before the edit.");
        return 1;
    }
    if (eip == 0xCF9F00) {
        head(r, 1, "A created/custom shoe record is broken (shoedata.DAT).", 0, 0);
        tryit(r); tip(r, "Rename Documents/AppData ...\\NBA 2K14\\shoes\\shoedata.DAT and start again.");
        return 1;
    }
    if (eip == 0xA48718) {
        head(r, 1, "The MyCareer save has a broken entry (crash right after loading it).", 0, 0);
        tryit(r); tip(r, "Load an older save (or its .bak copy).");
        return 1;
    }
    return 0;
}

static void cause_guess(const CauseFacts *fx, const CauseExtra *cx, CauseResult *r)
{
    static const CauseExtra none;
    static CauseDlls dl;
    const char *mod = fact(fx, "module"), *modMod = fact(fx, "module_mod"), *kind = fact(fx, "kind");
    const char *nearEip = fact(fx, "hooked_near_eip"), *stackMods = fact(fx, "stack_mods"), *nearStack = fact(fx, "hooked_near_stack");
    const char *access = fact(fx, "access"), *notes = fact(fx, "notes"), *failed = fact(fx, "files_failed"), *mods = fact(fx, "mods");
    unsigned code = fact_x(fx, "code"), target = fact_x(fx, "target"), eip = fact_x(fx, "eip");
    unsigned bigMb = fact_u(fx, "mem_largest_free_mb"), lowMb = fact_u(fx, "mem_lowest_largest_free_mb");
    unsigned totalMb = fact_u(fx, "mem_total_mb"), usedMb = fact_u(fx, "mem_used_mb");
    unsigned conflicts = fact_u(fx, "conflicts"), earlier = fact_u(fx, "earlier_exceptions");
    int ready = (int)fact_u(fx, "game_ready"), slamMod = (int)fact_u(fx, "module_is_slam_mod");
    int inSlam = (int)fact_u(fx, "module_is_slam"), inModule = !fact(fx, "eip_in_module")[0] || fact_u(fx, "eip_in_module");  /* missing = unknown */
    const char *ts = fact(fx, "exe_ts");
    int exe52 = c_ieq(ts, "52202C7F");
    int knownExe = !ts[0] || exe52 || c_ieq(ts, "4F219A21") || c_ieq(ts, "4E63057D") || c_ieq(ts, "4E84D35F");
    int nullPtr = access[0] && target < 0x10000;
    int lowMem = (bigMb && bigMb < 64) || (lowMb && lowMb < 32) || code == 0xC0000017u;
    const CxDll *meter, *wrapD3d, *wrapDxgi, *dinp;
    int meterMixed, inMeter, wrapper;
    char t[700], note[200];
    int filesShown = 0;
    if (!cx) cx = &none;

    r->sure = 0; r->headline[0] = 0; r->body[0] = 0; r->blame[0] = 0;
    cause_dlls(fact(fx, "extra_dlls"), &dl);
    meter = dll_find(&dl, "meter_core.dll", 0);
    dinp = dll_find(&dl, "dinput8.dll", 'g');                       /* another loader's dinput8.dll (SLAM's is not listed) */
    wrapD3d = dll_find(&dl, "d3d9.dll", 'g'); wrapDxgi = dll_find(&dl, "dxgi.dll", 'g');
    wrapper = wrapD3d || wrapDxgi || dll_find(&dl, "d3d9sweet.dll", 0);
    meterMixed = (meter && meter->size != 0x79000 && meter->size != 0x80000) || dll_find(&dl, "dinput8_next.dll", 0) ||
                 dll_find(&dl, "dinput8_shotmeter_previous.dll", 0) || dll_find(&dl, "CurvedShotMeter.asi", 'g') ||
                 (dinp && dinp->size == 0x36A000) || (meter && dll_find(&dl, "MeterStart.asi", 0) && dll_find(&dl, "CurvedShotMeter.asi", 0));
    inMeter = c_istarts(mod, "meter_core") || (dinp && c_ieq(mod, "dinput8.dll") && !inSlam) || c_in_list(stackMods, "CurvedShotMeter");

    if (fact_u(fx, "test") == 2) {
        head(r, 2, "This was a test crash (test key, TestCloses=1). Nothing is wrong.", 0, 0);
        say(r, "SLAM closed the game on purpose to test its crash reporter." NL);
        return;
    }
    if (fact_u(fx, "test")) {
        head(r, 2, "This is only a test report (test key). Nothing is wrong.", 0, 0);
        say(r, "SLAM's crash reporter works." NL);
        return;
    }

    /* 1. out of memory: everything else is a symptom */
    if (lowMem) {
        char a[12];
        c_utoa(a, bigMb);
        head(r, 2, "The game ran out of memory.", 0, 0);
        say(r, "NBA 2K14 is 32-bit (4 GB at most, whatever your RAM). Largest free block: "); say(r, a); say(r, " MB." NL);
        tryit(r);
        if (totalMb && totalMb < 2100) tip(r, "Use a 4 GB (large-address-aware) nba2k14.exe - yours can only use 2 GB.");
        tip(r, "Fewer very high-resolution mods (courts, crowds, faces, jerseys).");
        tip(r, "Restart the game between long sessions.");
        goto extras;
    }

    /* 2. known crashes of the usual exe (before "a mod changed code here": the fixes sit next to these spots) */
    if (exe52 && known_crash(fx, cx, r, eip, &filesShown)) goto extras;

    /* 3. shot meter installed twice / old meter loader */
    if (inMeter && (meterMixed || meter)) {
        head(r, 2, "The shot meter crashed - it is installed more than once / with old loader files.", 0, 0);
        say(r, "Old meter files (root CurvedShotMeter.asi, dinput8_next.dll, another dinput8.dll) run next to the new one." NL);
        tryit(r);
        tip(r, "Remove all shot meter files, then install only the current ShotMeter SLAM zip into SLAM\\.");
        tip(r, "Use SLAM's dinput8.dll, not the 3.5 MB one from a mod pack.");
        goto extras;
    }

    /* 4. a graphics wrapper in the game folder (ReShade / ENB / DXVK ...) */
    if (wrapper && (c_ieq(mod, "d3d9.dll") || c_ieq(mod, "dxgi.dll") || c_istarts(mod, "d3d9sweet") || (!inModule && !stackMods[0]))) {
        head(r, 1, "The crash is in the graphics wrapper in your game folder (ReShade / ENB / similar d3d9.dll or dxgi.dll).", 0, 0);
        if (wrapD3d && wrapDxgi) say(r, "There are two of them (d3d9.dll and dxgi.dll) - two ReShades crash the game." NL);
        tryit(r);
        tip(r, "Keep only one wrapper. To test, move d3d9.dll / dxgi.dll out of the game folder.");
        goto extras;
    }

    /* 5. a plain .asi built with a newer Visual Studio than the PC's C++ runtime (std::mutex crash) */
    if ((c_istarts(mod, "msvcp140") || c_istarts(mod, "vcruntime140")) && nullPtr) {
        head(r, 1, "A mod needs a newer Microsoft Visual C++ runtime than the one on this PC.", 0, 0);
        if (stackMods[0]) { c_nice_list(t, stackMods, sizeof(t)); say(r, "Called from: "); say(r, t); say(r, "." NL); blame(r, stackMods); }
        tryit(r);
        tip(r, "Install the latest Visual C++ 2015-2022 Redistributable (x86).");
        tip(r, "Turn off compatibility mode for nba2k14.exe.");
        goto extras;
    }

    /* 6. inside a mod's own code */
    if (modMod[0] && !inSlam) {
        head(r, 2, "The crash happened inside the mod \"", modMod, slamMod ? "\"." : "\" (a plain .asi, not a SLAM mod).");
        c_note_of(note, notes, modMod, sizeof(note));
        if (note[0]) { say(r, "It was: "); say(r, note); say(r, "." NL); }
        tryit(r);
        tip(r, "Update it. If the crashes stop without it, send this report to its author.");
        if (!knownExe) tip(r, "Your nba2k14.exe is not the usual version - the mod may not support it.");
        blame(r, modMod);
        goto extras;
    }

    /* 7. game code that a mod changed at that exact spot */
    if (nearEip[0]) {
        c_nice_list(t, nearEip, sizeof(t));
        head(r, 2, "The crash is at a spot in the game's code that ", t, " changed.");
        tryit(r);
        tip(r, "Update that mod (two names = they may clash: try each alone).");
        tip(r, "Send this report to its author.");
        blame(r, nearEip);
        goto extras;
    }

    /* 8. in SLAM itself */
    if (inSlam) {
        head(r, 1, "The crash happened in SLAM's own code.", 0, 0);
        if (stackMods[0]) { c_nice_list(t, stackMods, sizeof(t)); say(r, "SLAM was passing a call for "); say(r, t); say(r, "." NL); blame(r, stackMods); }
        tryit(r);
        tip(r, "Update SLAM and your SLAM mods; send this report to the SLAM author.");
        goto extras;
    }

    /* 9. a game file could not be opened, then the game used the empty result */
    if (failed[0] && nullPtr && c_ieq(mod, "nba2k14.exe")) {
        c_nice_list(t, failed, sizeof(t));
        head(r, 2, "A game file is missing or could not be opened: ", t, ".");
        say(r, "The game then used the empty result and crashed." NL);
        tryit(r); tip(r, "Put that file back (from the original game), check its name and that no program has it open.");
        goto extras;
    }

    /* 10. jumped to nowhere */
    if (!inModule) {
        head(r, 1, "The game jumped to code that doesn't exist.", 0, 0);
        say(r, "A broken function pointer, or code from a mod/overlay that was unloaded." NL);
        if (stackMods[0]) { c_nice_list(t, stackMods, sizeof(t)); say(r, t); say(r, " ran just before - check it first." NL); blame(r, stackMods); }
        tryit(r);
        tip(r, "Close overlays and recorders (Discord, Steam, RivaTuner, OBS, Overwolf, Bandicam).");
        tip(r, "Remove recently added mods one at a time.");
        goto extras;
    }

    /* 11. drivers, overlays, sound, video */
    if (is_overlay_module(mod)) {
        head(r, 2, "The crash happened in an overlay or recording program (", mod, ").");
        tryit(r); tip(r, "Turn off its in-game overlay (Discord, Steam, RivaTuner, OBS, Overwolf...).");
        goto extras;
    }
    if (is_gpu_module(mod)) {
        head(r, 1, "The crash happened in the graphics driver (", mod, ").");
        if (stackMods[0]) { c_nice_list(t, stackMods, sizeof(t)); say(r, t); say(r, " was running at that moment." NL); blame(r, stackMods); }
        tryit(r);
        tip(r, "Update your graphics driver.");
        tip(r, "Remove the newest texture/model/shader mod.");
        goto extras;
    }
    if (is_audio_module(mod)) {
        head(r, 1, "The crash happened in Windows' sound system (", mod, ").");
        tryit(r);
        tip(r, "Playback device: stereo, 44.1/48 kHz. Plug in headsets before starting the game.");
        tip(r, "Remove sound/commentary mods to test.");
        goto extras;
    }
    if (c_istarts(mod, "binkw32")) {
        head(r, 1, "The crash happened while playing a video (", mod, ").");
        tryit(r); tip(r, "Put back the original intro/menu videos (.bik).");
        goto extras;
    }

    /* 12. a mod's code was running (on the stack) or hooks a running function */
    if (stackMods[0] || nearStack[0]) {
        const char *who = stackMods[0] ? stackMods : nearStack;
        c_nice_list(t, who, sizeof(t));
        head(r, 1, t, " was involved in the crash.", 0);
        say(r, stackMods[0] ? "It had called into the game just before." NL : "It hooks a game function that was running." NL);
        {   char first[64]; int k = 0;
            while (who[k] && who[k] != ',' && k < 63) { first[k] = who[k]; k++; }
            first[k] = 0;
            c_note_of(note, notes, first, sizeof(note)); }
        if (note[0]) { say(r, "It was: "); say(r, note); say(r, "." NL); }
        tryit(r);
        tip(r, "Remove that mod to test. If the crashes stop, send this report to its author.");
        blame(r, who);
        goto extras;
    }

    /* 13. the game's own code, no mod nearby */
    if (c_ieq(mod, "nba2k14.exe")) {
        if (!knownExe) {
            head(r, 1, "The game crashed, and your nba2k14.exe is not the usual version.", 0, 0);
            tryit(r); tip(r, "Use the nba2k14.exe your mods were made for (see their README).");
            goto extras;
        }
        if (!ready) {
            head(r, 1, "The game crashed while starting.", 0, 0);
            tryit(r);
            tip(r, "Put back missing/edited game files (global.iff first).");
            tip(r, "Remove recently added .asi/.dll mods to test.");
            goto extras;
        }
        if (cx->nf) {
            head(r, 1, "The game crashed in its own code - most likely a changed game file.", 0, 0);
            say(r, "No mod was running. Changed files loaded this session:" NL);
            cx_file_lines(r, cx, 4); filesShown = 1;
            tryit(r);
            tip(r, "Put back the original of the first file, do the same thing again; then the next file.");
            goto extras;
        }
        if (nullPtr) {
            head(r, 1, "The game used data that wasn't there (null pointer).", 0, 0);
            say(r, "Usually a modded file the game can't use: roster, body/face/hair, jersey, arena, crowd." NL);
            tryit(r);
            tip(r, "Undo the newest file mod for what you were doing (team, arena, player).");
            tip(r, "Try the default roster.");
            goto extras;
        }
        head(r, 0, "The game crashed in its own code; no mod was directly involved.", 0, 0);
        tryit(r);
        tip(r, "Remove recently added mods and files one at a time.");
        goto extras;
    }

    /* 14. Windows / other DLLs */
    if (code == 0xE06D7363u) head(r, 0, "A program error was not handled (in ", mod[0] ? mod : "an unknown module", ").");
    else if (code == 0xC00000FDu) { head(r, 1, "The game ran out of stack (endless loop of calls).", 0, 0); say(r, "Often two hooks calling each other." NL); }
    else if (code == 0xC0000374u) { head(r, 1, "The game's memory was damaged (heap corruption).", 0, 0);
                                    say(r, "Something wrote past a memory block earlier; the crash shows up later, elsewhere." NL); }
    else head(r, 0, "The crash happened in ", mod[0] ? mod : "an unknown module", " - no clear cause.");
    tryit(r);
    tip(r, "Remove recently added mods one at a time until it stops.");

extras:
    if (cx->nf && !filesShown) { say(r, NL "Changed game files loaded this session:" NL); cx_file_lines(r, cx, 3); }
    if (failed[0] && !c_istarts(r->headline, "A game file is missing")) { c_nice_list(t, failed, sizeof(t)); say(r, NL "Could not be opened: "); say(r, t); say(r, "." NL); }
    if (meterMixed && !c_istarts(r->headline, "The shot meter")) say(r, NL "Also: old/duplicate shot meter files are installed - keep only the ShotMeter SLAM zip." NL);
    if (wrapD3d && wrapDxgi && !c_istarts(r->headline, "The crash is in the graphics wrapper")) say(r, NL "Also: two graphics wrappers (d3d9.dll + dxgi.dll) - keep one." NL);
    if (dinp && !meterMixed) say(r, NL "Also: dinput8.dll in the game folder is not SLAM's - use SLAM's dinput8.dll." NL);
    {   /* our mods with crash fixes in newer versions */
        char old[200]; old[0] = 0;
        if (c_mod_older(mods, "BodyHook", "2.2.1")) c_cat(old, "BodyHook,", sizeof(old));
        if (c_mod_older(mods, "SigExt", "1.10.2")) c_cat(old, "SigExt,", sizeof(old));
        if (c_mod_older(mods, "GotoShotExt", "2.35")) c_cat(old, "GotoShotExt,", sizeof(old));
        if (c_mod_older(mods, "RosterLoader", "1.2")) c_cat(old, "RosterLoader,", sizeof(old));
        if (old[0]) { c_nice_list(t, old, sizeof(t)); say(r, NL "Also: update "); say(r, t); say(r, " (newer versions fix crashes)." NL); }
    }
    if (cx->nl) {
        int k, who = -1, sameMod = 1;                   /* the one mod (not SLAM) that logged problems */
        for (k = 0; k < cx->nl; k++) if (!c_ieq(cx->l[k].mod, "SLAM")) {
            if (who < 0) who = k; else if (!c_ieq(cx->l[k].mod, cx->l[who].mod)) sameMod = 0;
        }
        if (r->sure == 0 && who >= 0 && sameMod) {      /* nothing else to go on: the mod that complained */
            char h[200]; h[0] = 0;
            c_cat(h, cx->l[who].mod, sizeof(h)); c_cat(h, " reported a problem in its log just before the crash.", sizeof(h));
            r->sure = 1; r->headline[0] = 0; c_cat(r->headline, h, sizeof(r->headline));
            blame(r, cx->l[who].mod);
        }
        say(r, NL "Problems in the mods' logs just before:" NL);
        for (k = 0; k < cx->nl && k < 3; k++) {
            char a[40], line[400]; a[0] = 0; line[0] = 0; c_age(a, cx->l[k].ageS);
            c_cat(line, cx->l[k].mod, sizeof(line)); c_cat(line, " (", sizeof(line)); c_cat(line, a, sizeof(line));
            c_cat(line, "): ", sizeof(line)); c_cat(line, cx->l[k].text, sizeof(line));
            tip(r, line);
        }
    }
    if (cx->probes[0]) {
        c_nice_list(t, cx->probes, sizeof(t));
        say(r, NL "Also: "); say(r, t); say(r, " is a probe/test build - remove it when not testing." NL);
    }
    if (conflicts) {
        char n[12]; c_utoa(n, conflicts);
        say(r, NL "Also: "); say(r, n); say(r, conflicts == 1 ? " conflict" : " conflicts"); say(r, " between mods at start (see below / SLAM.log)." NL);
    }
    if (earlier && r->sure < 2) {
        char n[12]; c_utoa(n, earlier);
        say(r, NL "Also: "); say(r, n); say(r, " earlier error(s) this session - the first may be where it began." NL);
    }
    if (!lowMem && bigMb && bigMb < 128) { c_utoa(t, bigMb); say(r, NL "Also: memory was low ("); say(r, t); say(r, " MB largest free block)." NL); }
    (void)usedMb; (void)kind;
}
#undef NL
