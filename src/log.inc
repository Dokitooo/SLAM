/* log.inc - SLAM\SLAM.log (previous run kept as SLAM.previous.log) and SLAM\<Mod>\<Mod>.log */

static CRITICAL_SECTION g_logLock;
static HANDLE g_log = INVALID_HANDLE_VALUE;
static char g_logPath[MAX_PATH];

static void log_raw(HANDLE f, const char *text)
{
    char line[1200]; SYSTEMTIME t; DWORD w;
    if (f == INVALID_HANDLE_VALUE || !f) return;
    GetLocalTime(&t);
    wsprintfA(line, "[%02u:%02u:%02u.%03u] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    s_cat(line, text, sizeof(line) - 2);
    s_cat(line, "\r\n", sizeof(line));
    EnterCriticalSection(&g_logLock);
    WriteFile(f, line, s_len(line), &w, NULL);
    LeaveCriticalSection(&g_logLock);
}

static void slog(const char *fmt, ...)
{
    char text[1100]; va_list ap;
    va_start(ap, fmt); s_vfmt(text, sizeof(text), fmt, ap); va_end(ap);
    log_raw(g_log, text);
}

static void log_flush(void) { if (g_log != INVALID_HANDLE_VALUE) FlushFileBuffers(g_log); }

/* SLAM.log -> SLAM.previous.log, then a fresh SLAM.log */
static void log_open(void)
{
    char prev[MAX_PATH];
    InitializeCriticalSection(&g_logLock);
    s_cpy(g_logPath, g_slamDir, MAX_PATH); s_cat(g_logPath, "SLAM.log", MAX_PATH);
    s_cpy(prev, g_slamDir, MAX_PATH); s_cat(prev, "SLAM.previous.log", MAX_PATH);
    MoveFileExA(g_logPath, prev, MOVEFILE_REPLACE_EXISTING);
    g_log = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
}

/* a mod's own log, opened on its first line */
static void mod_log_line(SlamMod *m, const char *text)
{
    if (!m) { log_raw(g_log, text); return; }
    if (m->logFile == 0) {
        char d[MAX_PATH];
        s_cpy(d, g_slamDir, MAX_PATH); s_cat(d, m->name, MAX_PATH);
        CreateDirectoryA(d, NULL);
        s_cpy(m->logPath, d, MAX_PATH); s_cat(m->logPath, "\\", MAX_PATH); s_cat(m->logPath, m->name, MAX_PATH); s_cat(m->logPath, ".log", MAX_PATH);
        m->logFile = CreateFileA(m->logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    }
    log_raw(m->logFile, text);
}
