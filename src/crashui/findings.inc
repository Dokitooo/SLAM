/* findings.inc - SLAMCrash.exe looks around after a crash
 *
 * The game is gone by now, so this can take its time and use the disk freely. It adds a
 * [findings] block (read by cause.inc) and a readable part to the end of the report, once:
 *
 *   1. Changed game files. The report's [files] block lists every game file (.iff .ros ...) the game
 *      opened this session. Each one is checked on disk: "edited with RED MC" (the tool writes its
 *      name into the file's header), and "changed after the game was installed" (its date is later
 *      than the date most of the game's .iff files have). Listed most recently changed first: the
 *      newest change is the first suspect, even when the crash shows up while loading another file.
 *   2. The mods' logs. Every .log in SLAM\ and SLAM\<Mod>\ written this session: lines that report a
 *      problem (error, failed, warning, ...) in the last 3 minutes before the crash, and the last
 *      lines of each log that was written in the last 10 minutes.
 *   3. Probe / research builds of mods that are still installed.
 *
 *   [findings]
 *   file|<s before the crash>|<flags: R RED MC, C changed, T opened by the crashing thread>|<changed at>|<path>
 *   log|<mod>|<s before the crash>|<line>
 *   probe|<mod version>
 *   [/findings]
 */

typedef unsigned long long u64;
static char *g_fb; static int g_fbN, g_fbCap;           /* the text that is added to the report */
static void fb_add(const char *s) { while (*s && g_fbN < g_fbCap - 1) g_fb[g_fbN++] = *s++; g_fb[g_fbN] = 0; }
static void fb_addn(const char *s, int n) { while (n-- > 0 && *s && g_fbN < g_fbCap - 1) g_fb[g_fbN++] = *s++; g_fb[g_fbN] = 0; }
static void fb_u(unsigned v) { char t[12]; c_utoa(t, v); fb_add(t); }

static int c_ifind(const char *s, int n, const char *w)          /* case-insensitive search in s[0..n) */
{
    int i, k, m = c_len(w);
    for (i = 0; i + m <= n; i++) { k = 0; while (k < m && c_lc(s[i + k]) == c_lc(w[k])) k++; if (k == m) return i; }
    return -1;
}
static u64 udiv64(u64 a, u64 b)                       /* no C runtime: no __aulldiv */
{
    u64 q = 0, r = 0; int i;
    for (i = 63; i >= 0; i--) { r = (r << 1) | ((a >> i) & 1); if (r >= b) { r -= b; q |= (u64)1 << i; } }
    return q;
}
static u64 ft64(const FILETIME *f) { return ((u64)f->dwHighDateTime << 32) | f->dwLowDateTime; }
static void fmt_when(char *out, const FILETIME *f)                /* local "2026-11-10 14:00" */
{
    FILETIME l; SYSTEMTIME s; char t[8];
    out[0] = 0;
    if (!FileTimeToLocalFileTime(f, &l) || !FileTimeToSystemTime(&l, &s)) return;
    c_utoa(t, s.wYear); c_cat(out, t, 24); c_cat(out, s.wMonth < 10 ? "-0" : "-", 24); c_utoa(t, s.wMonth); c_cat(out, t, 24);
    c_cat(out, s.wDay < 10 ? "-0" : "-", 24); c_utoa(t, s.wDay); c_cat(out, t, 24);
    c_cat(out, s.wHour < 10 ? " 0" : " ", 24); c_utoa(t, s.wHour); c_cat(out, t, 24);
    c_cat(out, s.wMinute < 10 ? ":0" : ":", 24); c_utoa(t, s.wMinute); c_cat(out, t, 24);
}
static int read_head(const char *path, char *buf, int cap)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD got = 0;
    if (f == INVALID_HANDLE_VALUE) return -1;
    if (!ReadFile(f, buf, (DWORD)cap, &got, NULL)) got = 0;
    CloseHandle(f);
    return (int)got;
}
/* the last <cap> bytes of a file */
static int read_tail(const char *path, char *buf, int cap)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, got = 0;
    if (f == INVALID_HANDLE_VALUE) return -1;
    sz = GetFileSize(f, NULL);
    if (sz != INVALID_FILE_SIZE && sz > (DWORD)cap) SetFilePointer(f, (LONG)(sz - (DWORD)cap), NULL, FILE_BEGIN);
    if (!ReadFile(f, buf, (DWORD)cap, &got, NULL)) got = 0;
    CloseHandle(f);
    return (int)got;
}

/* ---- 1. changed game files ---- */
typedef struct { u64 mt; unsigned ageS; int redmc, changed, thr; char when[24]; char path[200]; } FdFile;
static FdFile g_fdf[400]; static int g_fdfN;

static unsigned install_day(const char *gameDir)     /* the day most of the game's .iff files have */
{
    static struct { unsigned day, n; } h[96]; int hn = 0, i; unsigned best = 0, bestN = 0;
    char pat[MAX_PATH]; WIN32_FIND_DATAA fd; HANDLE f;
    s_cpy(pat, gameDir, MAX_PATH); c_cat(pat, "*.iff", MAX_PATH);
    f = FindFirstFileA(pat, &fd);
    if (f == INVALID_HANDLE_VALUE) return 0;
    do {
        unsigned d = (unsigned)udiv64(ft64(&fd.ftLastWriteTime), 864000000000ull);
        for (i = 0; i < hn && h[i].day != d; i++) ;
        if (i == hn) { if (hn == 96) continue; h[hn].day = d; h[hn].n = 0; hn++; }
        h[i].n++;
    } while (FindNextFileA(f, &fd));
    FindClose(f);
    for (i = 0; i < hn; i++) if (h[i].n > bestN) { bestN = h[i].n; best = h[i].day; }
    return bestN >= 20 ? best : 0;                    /* too few files to tell */
}

static void check_files(const char *report, const char *gameDir, unsigned crashTid)
{
    const char *p = report; int in = 0; unsigned iday = install_day(gameDir);
    static char head[1024];
    g_fdfN = 0;
    while (*p) {
        const char *e = p; int n;
        while (*e && *e != '\n' && *e != '\r') e++;
        n = (int)(e - p);
        if (n == 7 && c_istarts(p, "[files]")) in = 1;
        else if (n == 8 && c_istarts(p, "[/files]")) break;
        else if (in && n > 8 && g_fdfN < 400) {
            char t[16], rel[200], full[MAX_PATH]; unsigned ms, tid; WIN32_FIND_DATAA fd; HANDLE h; int got;
            FdFile *f = &g_fdf[g_fdfN];
            cx_field(p, e, 0, 0, t, sizeof(t)); ms = c_dec(t);
            cx_field(p, e, 1, 0, t, sizeof(t)); tid = c_dec(t);
            cx_field(p, e, 4, 1, rel, sizeof(rel));
            if (rel[0] && rel[1] == ':') s_cpy(full, rel, MAX_PATH); else { s_cpy(full, gameDir, MAX_PATH); c_cat(full, rel, MAX_PATH); }
            h = FindFirstFileA(full, &fd);
            if (h != INVALID_HANDLE_VALUE) {
                FindClose(h);
                f->mt = ft64(&fd.ftLastWriteTime);
                f->changed = iday && (unsigned)udiv64(f->mt, 864000000000ull) > iday + 1;
                got = read_head(full, head, sizeof(head));
                f->redmc = got > 0 && c_ifind(head, got, "RED MC") >= 0;
                if (f->changed || f->redmc) {
                    f->ageS = ms / 1000; f->thr = crashTid && tid == crashTid;
                    fmt_when(f->when, &fd.ftLastWriteTime);
                    s_cpy(f->path, rel, sizeof(f->path));
                    g_fdfN++;
                }
            }
        }
        while (*e == '\r' || *e == '\n') e++;
        p = e;
    }
    {   int i, j; FdFile tmp;                         /* most recently changed first */
        for (i = 1; i < g_fdfN; i++) for (j = i; j > 0 && g_fdf[j].mt > g_fdf[j - 1].mt; j--) { tmp = g_fdf[j]; g_fdf[j] = g_fdf[j - 1]; g_fdf[j - 1] = tmp; }
    }
}

/* ---- 2. the mods' logs ---- */
typedef struct { char mod[64]; unsigned ageS; char text[200]; } FdLog;
static FdLog g_fdl[64]; static int g_fdlN;
static const char *g_problemWords[] = { "error", "fail", "exception", "warning", "refused", "conflict", "could not", "couldn't",
                                        "can't", "cannot", "not found", "invalid", "corrupt", "missing", "crash", 0 };

/* seconds since midnight of "[hh:mm:ss" at the start of a log line, -1 = no time */
static int line_secs(const char *s, int n)
{
    int i = 0, h, m, sec;
    if (n < 9 || s[0] != '[') return -1;
    for (i = 1; i < 9; i++) if (i == 3 || i == 6 ? s[i] != ':' : (s[i] < '0' || s[i] > '9')) return -1;
    h = (s[1] - '0') * 10 + s[2] - '0'; m = (s[4] - '0') * 10 + s[5] - '0'; sec = (s[7] - '0') * 10 + s[8] - '0';
    return h < 24 && m < 60 && sec < 60 ? h * 3600 + m * 60 + sec : -1;
}
static int is_problem(const char *mod, const char *s, int n)
{
    int i;
    if (c_ieq(mod, "SLAM") && (c_ifind(s, n, "] CRASH ") >= 0 || c_ifind(s, n, "] TEST REPORT") >= 0 || c_ifind(s, n, "] EXCEPTION") >= 0 ||
                               c_ifind(s, n, "crash report: on") >= 0 || c_ifind(s, n, "test key") >= 0)) return 0;   /* already in the report */
    for (i = 0; g_problemWords[i]; i++) {
        int off = 0, at;
        while ((at = c_ifind(s + off, n - off, g_problemWords[i])) >= 0) {
            int a = off + at;                            /* "0 failed", "no errors": a count, not a problem */
            int zero = (a >= 2 && s[a - 1] == ' ' && s[a - 2] == '0' && (a < 3 || s[a - 3] < '0' || s[a - 3] > '9')) ||
                       (a >= 3 && c_lc(s[a - 1]) == ' ' && c_lc(s[a - 2]) == 'o' && c_lc(s[a - 3]) == 'n' && (a < 4 || s[a - 4] == ' ' || s[a - 4] == ']'));
            if (!zero) return 1;
            off = a + 1;
        }
    }
    return 0;
}
static char *g_tail;                                   /* 64 KB */
static void scan_log(const char *path, const char *mod, u64 crashFt, int crashSecs, u64 sessionFt)
{
    WIN32_FIND_DATAA fd; HANDLE h = FindFirstFileA(path, &fd); int n, i, start, lines, show; u64 mt;
    if (h == INVALID_HANDLE_VALUE) return;
    FindClose(h);
    mt = ft64(&fd.ftLastWriteTime);
    if (mt < sessionFt || mt > crashFt + 3000000000ull) return;   /* not written in the crashed session (5 min grace after it) */
    n = read_tail(path, g_tail, 65536);
    if (n <= 0) return;
    /* problem lines in the last 3 minutes */
    for (i = 0; i < n; ) {
        int e = i, s;
        while (e < n && g_tail[e] != '\n' && g_tail[e] != '\r') e++;
        s = line_secs(g_tail + i, e - i);
        if (s >= 0) {
            int age = crashSecs - s;
            if (age < 0) age += 86400;
            if (age <= 180 && is_problem(mod, g_tail + i, e - i)) {
                FdLog *l;
                if (g_fdlN == 64) { int k, old = 0; for (k = 1; k < 64; k++) if (g_fdl[k].ageS > g_fdl[old].ageS) old = k; g_fdl[old] = g_fdl[63]; g_fdlN = 63; }   /* keep the newest */
                l = &g_fdl[g_fdlN++];
                s_cpy(l->mod, mod, sizeof(l->mod)); l->ageS = (unsigned)age;
                {   int k = 0, j = i; while (j < e && k < 199) { char c = g_tail[j++]; l->text[k++] = c == '|' ? '/' : c; } l->text[k] = 0; }
            }
        }
        while (e < n && (g_tail[e] == '\n' || g_tail[e] == '\r')) e++;
        i = e;
    }
    /* the readable part: last lines of logs written in the last 10 minutes */
    if (crashFt > mt + 6000000000ull) return;
    for (start = n, lines = 0; start > 0 && lines < 16; start--) if (g_tail[start - 1] == '\n') { if (start < n) lines++; if (lines == 16) break; }
    {   char a[40]; a[0] = 0; c_age(a, crashFt > mt ? (unsigned)udiv64(crashFt - mt, 10000000ull) : 0);
        fb_add("\r\n  "); fb_add(mod); fb_add("  ("); fb_add(path); fb_add(", last written "); fb_add(a); fb_add(" before the crash)\r\n"); }
    for (i = start, show = 0; i < n && show < 16; ) {
        int e = i;
        while (e < n && g_tail[e] != '\n' && g_tail[e] != '\r') e++;
        if (e > i) { fb_add("    "); fb_addn(g_tail + i, e - i > 300 ? 300 : e - i); fb_add("\r\n"); show++; }
        while (e < n && (g_tail[e] == '\n' || g_tail[e] == '\r')) e++;
        i = e;
    }
}
static void scan_logs(const char *slamDir, u64 crashFt, int crashSecs, u64 sessionFt)
{
    char pat[MAX_PATH], p[MAX_PATH], sub[MAX_PATH], mod[64]; WIN32_FIND_DATAA fd, fd2; HANDLE h, h2;
    g_fdlN = 0;
    fb_add("\r\nMod logs written this session (last lines; problem lines of the last 3 minutes are in [findings]):\r\n");
    s_cpy(p, slamDir, MAX_PATH); c_cat(p, "SLAM.log", MAX_PATH);
    scan_log(p, "SLAM", crashFt, crashSecs, sessionFt);
    s_cpy(pat, slamDir, MAX_PATH); c_cat(pat, "*", MAX_PATH);
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        int isDir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0, L = c_len(fd.cFileName);
        if (fd.cFileName[0] == '.') continue;
        if (!isDir) {                                   /* loose .log in SLAM\ (not SLAM.log / SLAM.previous.log) */
            if (L > 4 && c_ieq(fd.cFileName + L - 4, ".log") && !c_istarts(fd.cFileName, "SLAM.")) {
                s_cpy(p, slamDir, MAX_PATH); c_cat(p, fd.cFileName, MAX_PATH);
                s_cpy(mod, fd.cFileName, sizeof(mod)); mod[L - 4 < 63 ? L - 4 : 63] = 0;
                scan_log(p, mod, crashFt, crashSecs, sessionFt);
            }
            continue;
        }
        if (c_ieq(fd.cFileName, "crash")) continue;
        s_cpy(sub, slamDir, MAX_PATH); c_cat(sub, fd.cFileName, MAX_PATH); c_cat(sub, "\\*.log", MAX_PATH);
        h2 = FindFirstFileA(sub, &fd2);
        if (h2 == INVALID_HANDLE_VALUE) continue;
        do {
            if (fd2.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
            s_cpy(p, slamDir, MAX_PATH); c_cat(p, fd.cFileName, MAX_PATH); c_cat(p, "\\", MAX_PATH); c_cat(p, fd2.cFileName, MAX_PATH);
            s_cpy(mod, fd.cFileName, sizeof(mod));
            scan_log(p, mod, crashFt, crashSecs, sessionFt);
        } while (FindNextFileA(h2, &fd2));
        FindClose(h2);
    } while (FindNextFileA(h, &fd));
    FindClose(h);
}

/* ---- all of it: returns 1 if the report was changed ---- */
static int add_findings(const char *path, const char *report)
{
    CauseFacts *fx; char gameDir[MAX_PATH], slamDir[MAX_PATH], mods[600]; int i, n, cut;
    HANDLE f; FILETIME ftw; SYSTEMTIME st; FILETIME lft; u64 crashFt, sessionFt; int crashSecs; DWORD w;
    unsigned up, tid;
    if (c_ifind(report, c_len(report), "\n[findings]") >= 0) return 0;          /* already done */
    if (c_ifind(report, c_len(report), "[facts]") < 0) return 0;                /* not a SLAM report */
    fx = (CauseFacts *)VirtualAlloc(NULL, sizeof(CauseFacts), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_fbCap = 400000;
    g_fb = (char *)VirtualAlloc(NULL, (SIZE_T)g_fbCap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    g_tail = (char *)VirtualAlloc(NULL, 65536 + 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!fx || !g_fb || !g_tail) return 0;
    g_fbN = 0; g_fb[0] = 0;
    cause_parse(report, fx);
    up = fact_u(fx, "uptime_s"); tid = fact_u(fx, "crash_tid");

    /* <game>\SLAM\crash\x.log -> <game>\SLAM\ and <game>\ */
    s_cpy(slamDir, path, MAX_PATH);
    for (i = 0, cut = 0, n = 0; slamDir[i]; i++) if (slamDir[i] == '\\') cut = i;
    slamDir[cut] = 0;                                                         /* ...\SLAM\crash */
    for (i = 0, cut = 0; slamDir[i]; i++) if (slamDir[i] == '\\') cut = i;
    slamDir[cut + 1] = 0;                                                     /* ...\SLAM\ */
    s_cpy(gameDir, slamDir, MAX_PATH);
    gameDir[c_len(gameDir) - 1] = 0;
    for (i = 0, cut = 0; gameDir[i]; i++) if (gameDir[i] == '\\') cut = i;
    gameDir[cut + 1] = 0;                                                     /* ...\ (game folder) */

    /* when the crash happened: the report's own time */
    f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    GetFileTime(f, NULL, NULL, &ftw); CloseHandle(f);
    crashFt = ft64(&ftw);
    sessionFt = crashFt - ((u64)up + 120) * 10000000ull;
    FileTimeToLocalFileTime(&ftw, &lft); FileTimeToSystemTime(&lft, &st);
    crashSecs = st.wHour * 3600 + st.wMinute * 60 + st.wSecond;

    fb_add("\r\n==================================================================================\r\n");
    fb_add("Added by SLAMCrash.exe after the crash\r\n");
    fb_add("==================================================================================\r\n");
    check_files(report, gameDir, tid);
    if (!fact_u(fx, "files_trail")) fb_add("Changed game files: unknown (this report has no list of the files the game opened)\r\n");
    else if (!g_fdfN) fb_add("Changed game files: none of the files the game opened this session look changed.\r\n");
    else {
        fb_add("Game files loaded this session that are not the originals (most recently changed first):\r\n");
        for (i = 0; i < g_fdfN && i < 60; i++) {
            char a[40]; a[0] = 0; c_age(a, g_fdf[i].ageS);
            fb_add("    "); fb_add(g_fdf[i].path); fb_add(" -");
            if (g_fdf[i].redmc) fb_add(" edited with RED MC,");
            if (g_fdf[i].changed) { fb_add(" changed "); fb_add(g_fdf[i].when); fb_add(","); }
            fb_add(" last loaded "); fb_add(a); fb_add(" before the crash");
            if (g_fdf[i].thr) fb_add(" (by the crashing thread)");
            fb_add("\r\n");
        }
    }
    scan_logs(slamDir, crashFt, crashSecs, sessionFt);

    /* the machine part */
    fb_add("\r\n[findings]\r\n");
    for (i = 0; i < g_fdfN && i < 48; i++) {
        fb_add("file|"); fb_u(g_fdf[i].ageS); fb_add("|");
        if (g_fdf[i].redmc) fb_add("R");
        if (g_fdf[i].changed) fb_add("C");
        if (g_fdf[i].thr) fb_add("T");
        fb_add("|"); fb_add(g_fdf[i].changed ? g_fdf[i].when : ""); fb_add("|"); fb_add(g_fdf[i].path); fb_add("\r\n");
    }
    {   int a, b; FdLog t;                              /* newest first (closest to the crash) */
        /* the mods' own lines before SLAM's, then the newest first */
        #define FD_KEY(l) ((c_ieq((l).mod, "SLAM") ? 100000u : 0u) + (l).ageS)
        for (a = 1; a < g_fdlN; a++) for (b = a; b > 0 && FD_KEY(g_fdl[b]) < FD_KEY(g_fdl[b - 1]); b--) { t = g_fdl[b]; g_fdl[b] = g_fdl[b - 1]; g_fdl[b - 1] = t; }
        #undef FD_KEY
    }
    for (i = 0; i < g_fdlN && i < 16; i++) {
        fb_add("log|"); fb_add(g_fdl[i].mod); fb_add("|"); fb_u(g_fdl[i].ageS); fb_add("|"); fb_add(g_fdl[i].text); fb_add("\r\n");
    }
    s_cpy(mods, fact(fx, "mods"), sizeof(mods));
    {   const char *q = mods;
        while (*q) {
            char one[80]; int k = 0, L;
            while (*q == ',' || *q == ' ') q++;
            while (*q && *q != ',' && k < 79) one[k++] = *q++;
            one[k] = 0; L = k;
            if (L && (c_ifind(one, L, "probe") >= 0 || c_ifind(one, L, "research") >= 0)) { fb_add("probe|"); fb_add(one); fb_add("\r\n"); }
        }
    }
    fb_add("[/findings]\r\n");

    f = CreateFileA(path, FILE_APPEND_DATA | FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    WriteFile(f, g_fb, (DWORD)g_fbN, &w, NULL);
    SetFileTime(f, NULL, NULL, &ftw);                 /* keep the crash time as the report's time */
    CloseHandle(f);
    return 1;
}
