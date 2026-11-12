/* grant.inc - 1.0.1: SLAMCrash.exe /grant "<NBA 2K14 folder>" "<that folder in VirtualStore>"
 * Started by SLAM (folder.inc) with administrator rights when the game is in a folder Windows protects
 * (Program Files). 1) The Users group gets "Modify" on the NBA 2K14 folder (inherited by everything in it:
 * SLAM, SLAM mods, plain .asi mods). 2) Files Windows had put into VirtualStore instead of the game folder
 * are moved back - the newer copy wins, the older one is removed (else it would keep hiding the real file).
 * Exit code 0 = the permission is set. Log: <game>\SLAM\SLAM folder permission.log */
#include <aclapi.h>

static HANDLE g_gLog = INVALID_HANDLE_VALUE;
static int g_gMoved, g_gOlder, g_gFailed;

static int w_len2(const WCHAR *s) { int n = 0; while (s[n]) n++; return n; }
static void w_cpy2(WCHAR *d, const WCHAR *s, int max) { int i = 0; for (; s[i] && i < max - 1; i++) d[i] = s[i]; d[i] = 0; }
static void w_cat2(WCHAR *d, const WCHAR *s, int max) { int n = w_len2(d); if (n < max) w_cpy2(d + n, s, max - n); }

static void glog(const WCHAR *a, const WCHAR *b)
{
    WCHAR w[1200]; char u[2400]; DWORD n; SYSTEMTIME t; int k;
    if (g_gLog == INVALID_HANDLE_VALUE) return;
    GetLocalTime(&t);
    wsprintfW(w, L"[%04u-%02u-%02u %02u:%02u:%02u] ", t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
    w_cat2(w, a, 1200); if (b) w_cat2(w, b, 1200); w_cat2(w, L"\r\n", 1200);
    k = WideCharToMultiByte(CP_UTF8, 0, w, -1, u, sizeof(u), 0, 0);
    if (k > 1) WriteFile(g_gLog, u, (DWORD)(k - 1), &n, 0);
}

/* move vs\* back into real\* (both with trailing backslash) */
static void g_merge(const WCHAR *vs, const WCHAR *real, int depth)
{
    WCHAR pat[MAX_PATH], a[MAX_PATH], b[MAX_PATH]; WIN32_FIND_DATAW fd; HANDLE h;
    if (depth > 12) return;
    w_cpy2(pat, vs, MAX_PATH); w_cat2(pat, L"*", MAX_PATH);
    if ((h = FindFirstFileW(pat, &fd)) == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.cFileName[0] == '.' && (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2]))) continue;
        w_cpy2(a, vs, MAX_PATH); w_cat2(a, fd.cFileName, MAX_PATH);
        w_cpy2(b, real, MAX_PATH); w_cat2(b, fd.cFileName, MAX_PATH);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            CreateDirectoryW(b, 0);
            w_cat2(a, L"\\", MAX_PATH); w_cat2(b, L"\\", MAX_PATH);
            g_merge(a, b, depth + 1);
            a[w_len2(a) - 1] = 0;
            RemoveDirectoryW(a);                     /* only if it is empty now */
        } else {
            WIN32_FILE_ATTRIBUTE_DATA ra; int keepVs = 1;
            if (GetFileAttributesExW(b, GetFileExInfoStandard, &ra) && !(ra.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
                keepVs = CompareFileTime(&fd.ftLastWriteTime, &ra.ftLastWriteTime) > 0;
            if (keepVs) {
                DWORD at = GetFileAttributesW(b);
                if (at != INVALID_FILE_ATTRIBUTES && (at & FILE_ATTRIBUTE_READONLY)) SetFileAttributesW(b, at & ~FILE_ATTRIBUTE_READONLY);
                if (MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED)) { g_gMoved++; glog(L"moved back: ", b); }
                else { g_gFailed++; glog(L"could NOT move back (in use?): ", a); }
            } else {
                SetFileAttributesW(a, FILE_ATTRIBUTE_NORMAL);
                if (DeleteFileW(a)) { g_gOlder++; glog(L"older copy removed (the game folder has a newer one): ", a); }
                else { g_gFailed++; glog(L"could NOT remove the older copy: ", a); }
            }
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

static int grant_main(int ac, WCHAR **av)
{
    SID_IDENTIFIER_AUTHORITY nt = { SECURITY_NT_AUTHORITY }; PSID users = 0; EXPLICIT_ACCESSW ea; PACL oldA = 0, newA = 0;
    PSECURITY_DESCRIPTOR sd = 0; WCHAR game[MAX_PATH], p[MAX_PATH], t[64]; DWORD e; int ok;
    if (ac < 3) return 3;
    w_cpy2(game, av[2], MAX_PATH);
    w_cpy2(p, game, MAX_PATH); w_cat2(p, L"\\SLAM", MAX_PATH); CreateDirectoryW(p, 0);
    w_cat2(p, L"\\SLAM folder permission.log", MAX_PATH);
    g_gLog = CreateFileW(p, FILE_APPEND_DATA, FILE_SHARE_READ, 0, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, 0);
    glog(L"SLAM 1.0.1 - giving the Users group \"Modify\" on ", game);

    /* 1) permission */
    e = ERROR_INVALID_FUNCTION;
    if (AllocateAndInitializeSid(&nt, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_USERS, 0, 0, 0, 0, 0, 0, &users)) {
        e = GetNamedSecurityInfoW(game, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, 0, 0, &oldA, 0, &sd);
        if (e == ERROR_SUCCESS) {
            memset(&ea, 0, sizeof(ea));
            ea.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_WRITE | FILE_GENERIC_EXECUTE | DELETE;   /* "Modify" */
            ea.grfAccessMode = GRANT_ACCESS; ea.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
            ea.Trustee.TrusteeForm = TRUSTEE_IS_SID; ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP; ea.Trustee.ptstrName = (LPWSTR)users;
            e = SetEntriesInAclW(1, &ea, oldA, &newA);
            if (e == ERROR_SUCCESS) e = SetNamedSecurityInfoW(game, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, 0, 0, newA, 0);   /* reaches what is inside, too */
        }
        FreeSid(users);
    }
    if (newA) LocalFree(newA);
    if (sd) LocalFree(sd);
    ok = e == ERROR_SUCCESS;
    wsprintfW(t, L"%u", e);
    glog(ok ? L"permission set" : L"permission NOT set, Windows error ", ok ? 0 : t);

    /* 2) VirtualStore -> game folder */
    if (ac >= 4 && av[3][0]) {
        WCHAR vs[MAX_PATH], real[MAX_PATH];
        w_cpy2(vs, av[3], MAX_PATH); w_cat2(vs, L"\\", MAX_PATH);
        w_cpy2(real, game, MAX_PATH); w_cat2(real, L"\\", MAX_PATH);
        if (GetFileAttributesW(av[3]) != INVALID_FILE_ATTRIBUTES) {
            glog(L"moving back what Windows put into ", av[3]);
            g_merge(vs, real, 0);
            RemoveDirectoryW(av[3]);
            wsprintfW(t, L"%d moved back, %d older copies removed, %d failed", g_gMoved, g_gOlder, g_gFailed);
            glog(t, 0);
        }
    }
    if (g_gLog != INVALID_HANDLE_VALUE) CloseHandle(g_gLog);
    return ok ? 0 : 2;
}
