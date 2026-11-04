/* util.inc - no-CRT helpers: strings, formatting, files, memory checks */

#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memset)
#pragma function(memcpy)
#endif
void *__cdecl memset(void *d, int c, size_t n) { u8 *p = (u8 *)d; while (n--) *p++ = (u8)c; return d; }
void *__cdecl memcpy(void *d, const void *s, size_t n) { u8 *p = (u8 *)d; const u8 *q = (const u8 *)s; while (n--) *p++ = *q++; return d; }
int _fltused = 0x9875;

static int  s_len(const char *s) { int n = 0; while (s[n]) n++; return n; }
static void s_cpy(char *d, const char *s, int max) { int i = 0; for (; s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }
static void s_cat(char *d, const char *s, int max) { int n = s_len(d); if (n < max) s_cpy(d + n, s, max - n); }
static char s_lc(char c) { return (char)(c >= 'A' && c <= 'Z' ? c + 32 : c); }
static int  s_ieq(const char *a, const char *b) { while (*a && s_lc(*a) == s_lc(*b)) { a++; b++; } return s_lc(*a) == s_lc(*b); }
static int  memcmp_i(const char *a, const char *b, int n) { while (n--) { if (s_lc(*a) != s_lc(*b)) return 1; a++; b++; } return 0; }
static const char *s_base(const char *p) { const char *b = p; for (; *p; p++) if (*p == '\\' || *p == '/') b = p + 1; return b; }
/* "a.asi, b.asi" contains "B.ASI"? */
static int s_inlist(const char *list, const char *item)
{
    char one[MAX_PATH]; int n;
    while (*list) {
        while (*list == ' ' || *list == ',' || *list == ';') list++;
        for (n = 0; *list && *list != ',' && *list != ';' && n < MAX_PATH - 1; list++) one[n++] = *list;
        while (n && one[n - 1] == ' ') n--;
        one[n] = 0;
        if (n && s_ieq(one, item)) return 1;
    }
    return 0;
}

/* wsprintf + "%f" (3 decimals). Output is always terminated. */
static void s_vfmt(char *out, int cap, const char *fmt, va_list ap)
{
    char tmp[1100], f2[600]; int i = 0, j = 0;
    /* turn %f into a %s of our own rendering; everything else goes to wvsprintfA */
    while (fmt[i] && j < (int)sizeof(f2) - 8) {
        if (fmt[i] == '%' && fmt[i + 1] == 'f') { f2[j++] = '%'; f2[j++] = 's'; i += 2; }
        else f2[j++] = fmt[i++];
    }
    f2[j] = 0;
    if (j == i && !fmt[i]) { wvsprintfA(tmp, fmt, ap); s_cpy(out, tmp, cap); return; }
    {
        /* rebuild the argument list: doubles become strings */
        char fbuf[8][32];
        u32 args[32]; int n = 0, k = 0;
        for (i = 0; fmt[i] && n < 32; i++) {
            if (fmt[i] != '%') continue;
            i++;
            while (fmt[i] == '-' || fmt[i] == '0' || fmt[i] == '#' || (fmt[i] >= '1' && fmt[i] <= '9') || fmt[i] == '.') i++;
            if (fmt[i] == '%') continue;
            if (fmt[i] == 'f' && k < 8) {
                double v = va_arg(ap, double); char *b = fbuf[k++]; int neg = v < 0; u32 w, fr;
                if (neg) v = -v;
                w = (u32)v; fr = (u32)((v - (double)w) * 1000.0 + 0.5); if (fr >= 1000) { w++; fr -= 1000; }
                wsprintfA(b, "%s%u.%03u", neg ? "-" : "", w, fr);
                args[n++] = (u32)b;
            } else args[n++] = va_arg(ap, u32);
        }
        wvsprintfA(tmp, f2, (va_list)args);
        s_cpy(out, tmp, cap);
    }
}
static void s_fmt(char *out, int cap, const char *fmt, ...) { va_list ap; va_start(ap, fmt); s_vfmt(out, cap, fmt, ap); va_end(ap); }

static int mem_readable(const void *p, u32 n)
{
    MEMORY_BASIC_INFORMATION mbi; u32 a = (u32)p, end = a + n;
    if (!n) return 1;
    if (end < a) return 0;
    while (a < end) {
        if (!VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return 0;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return 0;
        a = (u32)mbi.BaseAddress + (u32)mbi.RegionSize;
    }
    return 1;
}

/* which loaded module contains address a (0 if none) */
static HMODULE mod_of(u32 a)
{
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery((LPCVOID)a, &mbi, sizeof(mbi)) || mbi.Type != MEM_IMAGE) return 0;
    return (HMODULE)mbi.AllocationBase;
}

static int file_exists(const char *p) { DWORD a = GetFileAttributesA(p); return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY); }

/* whole small file into a VirtualAlloc'd buffer (+1 zero byte); caller VirtualFree's it */
static u8 *file_read(const char *path, u32 *sizeOut, u32 maxSize)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, got = 0; u8 *buf;
    if (f == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(f, NULL);
    if (sz == INVALID_FILE_SIZE || sz > maxSize) { CloseHandle(f); return 0; }
    buf = (u8 *)VirtualAlloc(NULL, sz + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (buf && (!ReadFile(f, buf, sz, &got, NULL) || got != sz)) { VirtualFree(buf, 0, MEM_RELEASE); buf = 0; }
    CloseHandle(f);
    if (buf) { buf[sz] = 0; if (sizeOut) *sizeOut = sz; }
    return buf;
}

/* PE helpers on a file image read from disk */
static u32 pe_rva2off(const u8 *img, u32 size, u32 rva)
{
    u32 pe = *(const u32 *)(img + 0x3C), n, i, sec;
    if (pe + 0xF8 > size) return 0;
    n = *(const u16 *)(img + pe + 6);
    sec = pe + 24 + *(const u16 *)(img + pe + 20);
    for (i = 0; i < n && sec + 40 * (i + 1) <= size; i++) {
        const u8 *s = img + sec + 40 * i;
        u32 va = *(const u32 *)(s + 12), vs = *(const u32 *)(s + 8), raw = *(const u32 *)(s + 20), rs = *(const u32 *)(s + 16);
        if (rva >= va && rva < va + (vs > rs ? vs : rs)) { u32 o = rva - va + raw; return o < size ? o : 0; }
    }
    return 0;
}
