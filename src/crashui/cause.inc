/* cause.inc - turns the [facts] block of a SLAM crash report into a plain-language explanation
 *
 * Pure C, no Windows calls: the same file is built into SLAMCrash.exe and into the Linux test
 * (tools/test_cause). Rules are checked from most to least specific; the first rule that fits
 * gives the headline, later rules may add "also" lines.
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
typedef struct { int sure; char headline[300]; char body[4000]; char blame[300]; } CauseResult;

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

/* ---- module families ---- */
static int is_gpu_module(const char *m)
{
    static const char *g[] = { "d3d9.dll", "d3dx9_", "nvd3dum", "nvwgf2um", "nvldumd", "atiumdag", "aticfx32", "atidxx32",
                               "amdxc32", "atiu9pag", "igdumd32", "igdumdim32", "igd9dxva32", "igdusc32", "dxgi.dll", "d3d11.dll", 0 };
    int i;
    for (i = 0; g[i]; i++) if (c_istarts(m, g[i])) return 1;
    return 0;
}
static int is_overlay_module(const char *m)   /* recorders, overlays, injectors that hook Direct3D */
{
    static const char *o[] = { "rtsshooks", "gameoverlayrenderer", "discordhook", "nvspcap", "obs-", "graphics-hook",
                               "fraps", "bdcamvk", "dxtory", "reshade", "enbseries", "overlay", 0 };
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
static void head(CauseResult *r, int sure, const char *s1, const char *s2, const char *s3)
{
    r->sure = sure; r->headline[0] = 0;
    c_cat(r->headline, s1, sizeof(r->headline)); if (s2) c_cat(r->headline, s2, sizeof(r->headline)); if (s3) c_cat(r->headline, s3, sizeof(r->headline));
}
static void blame(CauseResult *r, const char *list) { if (!r->blame[0]) c_cat(r->blame, list, sizeof(r->blame)); }

static void cause_guess(const CauseFacts *fx, CauseResult *r)
{
    const char *mod = fact(fx, "module"), *modMod = fact(fx, "module_mod"), *kind = fact(fx, "kind");
    const char *nearEip = fact(fx, "hooked_near_eip"), *stackMods = fact(fx, "stack_mods"), *nearStack = fact(fx, "hooked_near_stack");
    const char *access = fact(fx, "access"), *notes = fact(fx, "notes");
    unsigned code = fact_x(fx, "code"), target = fact_x(fx, "target");
    unsigned bigMb = fact_u(fx, "mem_largest_free_mb"), lowMb = fact_u(fx, "mem_lowest_largest_free_mb");
    unsigned totalMb = fact_u(fx, "mem_total_mb"), usedMb = fact_u(fx, "mem_used_mb");
    unsigned conflicts = fact_u(fx, "conflicts"), earlier = fact_u(fx, "earlier_exceptions");
    int ready = (int)fact_u(fx, "game_ready"), slamMod = (int)fact_u(fx, "module_is_slam_mod");
    int inSlam = (int)fact_u(fx, "module_is_slam"), inModule = !fact(fx, "eip_in_module")[0] || fact_u(fx, "eip_in_module");  /* missing = unknown */
    const char *ts = fact(fx, "exe_ts");
    int knownExe = !ts[0] || c_ieq(ts, "52202C7F") || c_ieq(ts, "4F219A21") || c_ieq(ts, "4E63057D") || c_ieq(ts, "4E84D35F");
    int nullPtr = access[0] && target < 0x10000;
    int lowMem = (bigMb && bigMb < 64) || (lowMb && lowMb < 32) || code == 0xC0000017u;
    char t[700], note[200];
    int i;

    r->sure = 0; r->headline[0] = 0; r->body[0] = 0; r->blame[0] = 0;

    if (fact_u(fx, "test") == 2) {
        head(r, 2, "This was a test crash (test key, TestCloses=1). Nothing is wrong.", 0, 0);
        say(r, "SLAM closed the game on purpose to test the whole crash path. If the game really" NL
               "crashes, a window like this one will show what happened and which mod was most likely involved." NL);
        return;
    }
    if (fact_u(fx, "test")) {
        head(r, 2, "This is only a test report (test key). Nothing is wrong.", 0, 0);
        say(r, "SLAM's crash reporter works. If the game really crashes, a window like this one" NL
               "will show what happened and which mod was most likely involved." NL);
        return;
    }

    /* 1. out of memory: everything else is a symptom */
    if (lowMem) {
        char a[12], b[12];
        c_utoa(a, bigMb); c_utoa(b, lowMb ? lowMb : bigMb);
        head(r, 2, "The game ran out of memory.", 0, 0);
        say(r, "NBA 2K14 is a 32-bit game: it can use at most 4 GB, however much RAM your PC has." NL
               "When the crash happened, the biggest free block of memory was only ");
        say(r, a); say(r, " MB (lowest this session: "); say(r, b); say(r, " MB)." NL
               "The game then fails to load the next big thing (a court, a crowd, a high-res texture)." NL NL
               "What you can try:" NL);
        if (totalMb && totalMb < 2100) tip(r, "Your nba2k14.exe can only use 2 GB. Use a large-address-aware (4 GB) nba2k14.exe.");
        tip(r, "Use fewer very high-resolution texture/model mods (courts, jerseys, faces, crowds).");
        tip(r, "Restart the game between long sessions (memory adds up over several games).");
        tip(r, "If it always happens after the same number of games, send the report to the mod authors: it may be a leak.");
        goto extras;
    }

    /* 2. inside a mod's own code */
    if (modMod[0] && !inSlam) {
        head(r, 2, "The crash happened inside the mod \"", modMod, slamMod ? "\"." : "\" (a plain .asi, not a SLAM mod).");
        c_note_of(note, notes, modMod, sizeof(note));
        say(r, "The game was running code from "); say(r, mod); say(r, " when it crashed.");
        if (note[0]) { say(r, NL "The mod said it was: "); say(r, note); say(r, "."); }
        say(r, NL NL "What you can try:" NL);
        tip(r, "Update that mod, or check that you have the version that matches your other mods.");
        tip(r, "Remove it to confirm: if the crashes stop, send this report to its author.");
        if (!knownExe) tip(r, "Your nba2k14.exe is not the usual version - the mod may not support it.");
        blame(r, modMod);
        goto extras;
    }

    /* 3. game code that a mod changed at that exact spot */
    if (nearEip[0]) {
        c_nice_list(t, nearEip, sizeof(t));
        head(r, 2, "The crash is at a spot in the game's code that ", t, " changed.");
        say(r, "The game crashed in "); say(r, mod[0] ? mod : "game code");
        say(r, " right where "); say(r, t); say(r, " hooked or patched it." NL
               "That mod's change is the most likely cause." NL NL "What you can try:" NL);
        tip(r, "Update that mod. If two mods are named, they may not work together - try each one alone.");
        tip(r, "Send this report to the mod's author.");
        blame(r, nearEip);
        goto extras;
    }

    /* 4. in SLAM itself */
    if (inSlam) {
        head(r, 1, "The crash happened in SLAM's own code.", 0, 0);
        if (stackMods[0]) {
            c_nice_list(t, stackMods, sizeof(t));
            say(r, "SLAM was passing a call between mods. "); say(r, t);
            say(r, " was involved, so it is the most likely cause." NL);
            blame(r, stackMods);
        } else say(r, "No mod was clearly involved." NL);
        say(r, NL "What you can try:" NL);
        tip(r, "Update SLAM and your SLAM mods.");
        tip(r, "Send this report to the SLAM author (and to the mod named above, if any).");
        goto extras;
    }

    /* 5. jumped to nowhere */
    if (!inModule) {
        head(r, 1, "The game tried to run code that doesn't exist.", 0, 0);
        say(r, "This happens when a pointer to a function is broken, or when code that was" NL
               "loaded by a mod or an overlay program is gone." NL);
        if (stackMods[0]) {
            c_nice_list(t, stackMods, sizeof(t));
            say(r, t); say(r, " was running just before - check it first." NL);
            blame(r, stackMods);
        }
        say(r, NL "What you can try:" NL);
        tip(r, "Close overlay/recording programs (Discord, Steam overlay, MSI Afterburner/RivaTuner, OBS, ReShade).");
        tip(r, "Remove recently added mods one at a time.");
        goto extras;
    }

    /* 6. graphics driver / overlay */
    if (is_overlay_module(mod)) {
        head(r, 2, "The crash happened in an overlay or recording program (", mod, ").");
        say(r, "Programs that draw over games or record them load their own code into NBA 2K14." NL NL "What you can try:" NL);
        tip(r, "Turn off that program's in-game overlay (Discord, Steam, MSI Afterburner/RivaTuner, OBS game capture...).");
        tip(r, "If you use ReShade or ENB, remove it to test.");
        goto extras;
    }
    if (is_gpu_module(mod)) {
        head(r, 1, "The crash happened in the graphics driver or Direct3D (", mod, ").");
        say(r, "This is usually the graphics driver, a graphics mod (shaders, textures, models)," NL
               "or a d3d9.dll placed in the game folder (ReShade, ENB, a wrapper)." NL);
        if (stackMods[0]) { c_nice_list(t, stackMods, sizeof(t)); say(r, t); say(r, " was running at that moment." NL); blame(r, stackMods); }
        say(r, NL "What you can try:" NL);
        tip(r, "Update your graphics driver.");
        tip(r, "If there is a d3d9.dll in the game folder, move it out and test.");
        tip(r, "Remove the newest texture/model/shader mod and test.");
        goto extras;
    }
    if (is_audio_module(mod)) {
        head(r, 1, "The crash happened in Windows' sound system (", mod, ").");
        say(r, NL "What you can try:" NL);
        tip(r, "Set your playback device to stereo 44.1/48 kHz in Windows sound settings.");
        tip(r, "Unplug/replug headsets or Bluetooth audio before starting the game.");
        tip(r, "Remove sound/commentary mods to test.");
        goto extras;
    }
    if (c_istarts(mod, "binkw32")) {
        head(r, 1, "The crash happened while playing a video (", mod, ").");
        say(r, NL "What you can try:" NL);
        tip(r, "A replaced intro/menu video (.bik) may be broken - put the original back.");
        goto extras;
    }

    /* 7. a mod's code was running (on the stack) or hooks a running function */
    if (stackMods[0] || nearStack[0]) {
        const char *who = stackMods[0] ? stackMods : nearStack;
        c_nice_list(t, who, sizeof(t));
        head(r, 1, t, " was involved in the crash.", 0);
        say(r, "The crash itself is in "); say(r, mod[0] ? mod : "game code"); say(r, ", but ");
        say(r, t);
        say(r, stackMods[0] ? " had called into it just before." NL : " hooks one of the game functions that were running." NL);
        say(r, "Mods often cause crashes this way: they hand the game data it doesn't expect." NL);
        {   char first[64]; int k = 0;
            while (who[k] && who[k] != ',' && k < 63) { first[k] = who[k]; k++; }
            first[k] = 0;
            c_note_of(note, notes, first, sizeof(note)); }
        if (note[0]) { say(r, "The mod said it was: "); say(r, note); say(r, "." NL); }
        say(r, NL "What you can try:" NL);
        tip(r, "Remove that mod to test. If the crashes stop, send this report to its author.");
        blame(r, who);
        goto extras;
    }

    /* 8. the game's own code, no mod nearby */
    if (c_ieq(mod, "nba2k14.exe")) {
        if (!knownExe) {
            head(r, 1, "The game crashed, and your nba2k14.exe is not the usual version.", 0, 0);
            say(r, "Mods are made for one exact nba2k14.exe. On another version they change the wrong places." NL NL "What you can try:" NL);
            tip(r, "Use the nba2k14.exe your mods were made for (see their README).");
            goto extras;
        }
        if (!ready) {
            head(r, 1, "The game crashed while starting.", 0, 0);
            say(r, "This is usually a missing or broken game file, or a mod that loads too early." NL NL "What you can try:" NL);
            tip(r, "Check the game's files (Steam: Properties > Installed Files > Verify).");
            tip(r, "Remove recently added .asi/.dll mods to test.");
            goto extras;
        }
        if (nullPtr) {
            head(r, 1, "The game used data that wasn't there.", 0, 0);
            say(r, "It tried to ");
            say(r, access[0] == 'w' ? "write to" : "read");
            say(r, " an empty (null) pointer. In NBA 2K14 this is most often a modded file the game" NL
                   "can't use: a roster, a player's body/face/hair, a jersey, an arena or a crowd (.iff)." NL NL "What you can try:" NL);
            tip(r, "Think about what you were doing (which team, arena, player) and undo the newest file mod for it.");
            tip(r, "Try the default roster to see whether the roster is the problem.");
            goto extras;
        }
        head(r, 0, "The game crashed in its own code; no mod was directly involved.", 0, 0);
        say(r, "Nothing points at a specific mod. It may be a broken game/mod file, or a mod that" NL
               "changed something earlier." NL NL "What you can try:" NL);
        tip(r, "Remove recently added mods and files one at a time.");
        tip(r, "Send this report to the author of the mod you added last.");
        goto extras;
    }

    /* 9. Windows / other DLLs */
    if (code == 0xE06D7363u) {
        head(r, 0, "A program error was not handled (in ", mod[0] ? mod : "an unknown module", ").");
    } else if (code == 0xC00000FDu) {
        head(r, 1, "The game ran out of stack (endless loop of calls).", 0, 0);
        say(r, "Often two hooks call each other forever, or a mod hooks a function it also calls." NL);
    } else if (code == 0xC0000374u) {
        head(r, 1, "The game's memory was damaged (heap corruption).", 0, 0);
        say(r, "Something wrote past the end of a block of memory earlier; the crash shows up later" NL
               "somewhere else. A mod is the most common cause." NL);
    } else {
        head(r, 0, "The crash happened in ", mod[0] ? mod : "an unknown module", " - no clear cause.");
    }
    say(r, NL "What you can try:" NL);
    tip(r, "Remove recently added mods one at a time until the crash stops.");
    tip(r, "Send this report to the author of the mod you suspect.");

extras:
    if (conflicts) {
        char n[12]; c_utoa(n, conflicts);
        say(r, NL "Also: SLAM found "); say(r, n);
        say(r, conflicts == 1 ? " conflict between mods" : " conflicts between mods");
        say(r, " when the game started (listed below and in SLAM\\SLAM.log)." NL);
    }
    if (earlier && r->sure < 2) {
        char n[12]; c_utoa(n, earlier);
        say(r, NL "Also: "); say(r, n); say(r, " smaller error(s) happened earlier this session - the first one may be where it began." NL);
    }
    if (!lowMem && bigMb && bigMb < 128) say(r, NL "Also: memory was getting low (" ), c_utoa(t, bigMb), say(r, t), say(r, " MB largest free block)." NL);
    (void)usedMb; (void)kind; (void)i;
}
#undef NL
