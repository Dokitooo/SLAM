/* zip.inc - 1.1.2: one file to send. After a crash SLAMCrash.exe puts the report, the dump and SLAM.log
 * into crash_<date>_<time>.zip next to the report (stored, no compression - any zip tool and Windows open
 * it). The .log and .dmp stay where they are; the zip is what players send. */

static unsigned long g_crcTab[256];
static void crc_init(void)
{
    unsigned long c; int n, k;
    for (n = 0; n < 256; n++) { c = (unsigned long)n; for (k = 0; k < 8; k++) c = (c & 1) ? 0xEDB88320UL ^ (c >> 1) : c >> 1; g_crcTab[n] = c; }
}
static unsigned long crc32_of(const unsigned char *p, DWORD n)
{
    unsigned long c = 0xFFFFFFFFUL;
    while (n--) c = g_crcTab[(c ^ *p++) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFUL;
}

typedef struct { char name[64]; unsigned long crc; DWORD size, off; WORD time, date; } ZipEnt;

static void z_put16(unsigned char *b, int o, unsigned v) { b[o] = (unsigned char)v; b[o + 1] = (unsigned char)(v >> 8); }
static void z_put32(unsigned char *b, int o, unsigned long v) { z_put16(b, o, (unsigned)(v & 0xFFFF)); z_put16(b, o + 2, (unsigned)(v >> 16)); }
static int z_write(HANDLE f, const void *p, DWORD n) { DWORD w = 0; return WriteFile(f, p, n, &w, NULL) && w == n; }

/* whole file into memory (up to 256 MB) */
static unsigned char *z_read(const char *path, DWORD *size, FILETIME *t)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, got = 0; unsigned char *b;
    if (f == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(f, NULL);
    if (sz == INVALID_FILE_SIZE || sz > (256u << 20)) { CloseHandle(f); return 0; }
    if (t) GetFileTime(f, NULL, NULL, t);
    b = (unsigned char *)VirtualAlloc(NULL, sz ? sz : 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!b || (sz && (!ReadFile(f, b, sz, &got, NULL) || got != sz))) { if (b) VirtualFree(b, 0, MEM_RELEASE); CloseHandle(f); return 0; }
    CloseHandle(f);
    *size = sz;
    return b;
}

static const char *z_base(const char *p) { const char *b = p; for (; *p; p++) if (*p == '\\' || *p == '/') b = p + 1; return b; }

/* zip `paths` (n of them, missing ones skipped) into `zipPath` */
static int zip_files(const char *zipPath, const char *const *paths, const char *const *names, int n)
{
    char tmp[MAX_PATH];
    ZipEnt e[8]; int k = 0, i; DWORD off = 0, cdOff, cdSize = 0;
    unsigned char h[64];
    HANDLE f;
    if (n > 8) n = 8;
    crc_init();
    s_cpy(tmp, zipPath, MAX_PATH); c_cat(tmp, ".part", MAX_PATH);
    f = CreateFileA(tmp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return 0;
    for (i = 0; i < n; i++) {
        DWORD sz = 0; FILETIME ft, lt; WORD dd = 0x21, tt = 0; unsigned char *b;
        int L;
        if (!paths[i] || !paths[i][0]) continue;
        b = z_read(paths[i], &sz, &ft);
        if (!b) continue;
        if (FileTimeToLocalFileTime(&ft, &lt)) FileTimeToDosDateTime(&lt, &dd, &tt);
        s_cpy(e[k].name, names && names[i] ? names[i] : z_base(paths[i]), sizeof(e[k].name));
        e[k].crc = crc32_of(b, sz); e[k].size = sz; e[k].off = off; e[k].time = tt; e[k].date = dd;
        L = c_len(e[k].name);
        memset(h, 0, 30);
        z_put32(h, 0, 0x04034B50UL); z_put16(h, 4, 20); z_put16(h, 6, 0); z_put16(h, 8, 0);
        z_put16(h, 10, tt); z_put16(h, 12, dd); z_put32(h, 14, e[k].crc); z_put32(h, 18, sz); z_put32(h, 22, sz);
        z_put16(h, 26, (unsigned)L); z_put16(h, 28, 0);
        if (!z_write(f, h, 30) || !z_write(f, e[k].name, (DWORD)L) || (sz && !z_write(f, b, sz))) { VirtualFree(b, 0, MEM_RELEASE); CloseHandle(f); DeleteFileA(tmp); return 0; }
        VirtualFree(b, 0, MEM_RELEASE);
        off += 30 + (DWORD)L + sz;
        k++;
    }
    if (!k) { CloseHandle(f); DeleteFileA(tmp); return 0; }
    cdOff = off;
    for (i = 0; i < k; i++) {
        int L = c_len(e[i].name);
        memset(h, 0, 46);
        z_put32(h, 0, 0x02014B50UL); z_put16(h, 4, 20); z_put16(h, 6, 20); z_put16(h, 8, 0); z_put16(h, 10, 0);
        z_put16(h, 12, e[i].time); z_put16(h, 14, e[i].date); z_put32(h, 16, e[i].crc); z_put32(h, 20, e[i].size); z_put32(h, 24, e[i].size);
        z_put16(h, 28, (unsigned)L); z_put32(h, 42, e[i].off);
        if (!z_write(f, h, 46) || !z_write(f, e[i].name, (DWORD)L)) { CloseHandle(f); DeleteFileA(tmp); return 0; }
        cdSize += 46 + (DWORD)L;
    }
    memset(h, 0, 22);
    z_put32(h, 0, 0x06054B50UL); z_put16(h, 8, (unsigned)k); z_put16(h, 10, (unsigned)k); z_put32(h, 12, cdSize); z_put32(h, 16, cdOff);
    if (!z_write(f, h, 22)) { CloseHandle(f); DeleteFileA(tmp); return 0; }
    CloseHandle(f);
    DeleteFileA(zipPath);
    if (!MoveFileA(tmp, zipPath)) { DeleteFileA(tmp); return 0; }
    return k;
}

/* crash_X.log -> crash_X.zip with crash_X.log, crash_X.dmp, SLAM.log (from the SLAM folder) */
static int make_bundle(const char *logPath, char *zipOut)
{
    char dmp[MAX_PATH], slamLog[MAX_PATH]; const char *paths[3], *names[3]; int n, cut;
    s_cpy(zipOut, logPath, MAX_PATH);
    n = c_len(zipOut);
    if (n < 5 || !c_ieq(zipOut + n - 4, ".log")) { zipOut[0] = 0; return 0; }
    zipOut[n - 4] = 0;
    s_cpy(dmp, zipOut, MAX_PATH); c_cat(dmp, ".dmp", MAX_PATH);
    c_cat(zipOut, ".zip", MAX_PATH);
    /* SLAM\crash\x.log -> SLAM\SLAM.log */
    s_cpy(slamLog, logPath, MAX_PATH);
    for (cut = c_len(slamLog); cut > 0 && slamLog[cut - 1] != '\\'; cut--) ;
    slamLog[cut] = 0;                                    /* ...\SLAM\crash\ */
    if (cut > 0) cut--;
    for (; cut > 0 && slamLog[cut - 1] != '\\'; cut--) ;
    slamLog[cut] = 0;                                    /* ...\SLAM\ */
    c_cat(slamLog, "SLAM.log", MAX_PATH);
    paths[0] = logPath; names[0] = 0;
    paths[1] = dmp;     names[1] = 0;
    paths[2] = slamLog; names[2] = "SLAM.log";
    if (!zip_files(zipOut, paths, names, 3)) { zipOut[0] = 0; return 0; }
    return 1;
}
