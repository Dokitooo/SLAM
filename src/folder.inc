/* folder.inc - 1.0.1: NBA 2K14 in Program Files (or another folder Windows protects)
 *
 * nba2k14.exe has no manifest, so Windows "virtualizes" it: when the game (and with it SLAM and every
 * mod) writes into a protected folder such as C:\Program Files (x86)\..., Windows quietly puts the file
 * into %LOCALAPPDATA%\VirtualStore\Program Files (x86)\... instead. SLAM.log, the mods' logs, ini files
 * and roster files then never show up in the game folder, and a copy in VirtualStore hides the real file
 * (edits to the real one are ignored).
 *
 * At the game's entry point, before any mod loads: if SLAM\ is not writable for this Windows user, or
 * VirtualStore holds files of this game folder, SLAM asks once (Yes / No / Cancel = don't ask again).
 * Yes: SLAMCrash.exe /grant runs with administrator rights (the Windows prompt) - it gives the Users
 * group "Modify" on the NBA 2K14 folder (SLAM, every SLAM mod and plain .asi mods) and moves the files
 * Windows had put into VirtualStore back (the newer copy wins). SLAM.ini: [Folder] Ask=0 = never ask.
 */
#include <shellapi.h>

typedef BOOL (WINAPI *fo_GetFileSecurityA_t)(LPCSTR, SECURITY_INFORMATION, PSECURITY_DESCRIPTOR, DWORD, LPDWORD);
typedef BOOL (WINAPI *fo_OpenProcessToken_t)(HANDLE, DWORD, PHANDLE);
typedef BOOL (WINAPI *fo_DuplicateToken_t)(HANDLE, SECURITY_IMPERSONATION_LEVEL, PHANDLE);
typedef BOOL (WINAPI *fo_AccessCheck_t)(PSECURITY_DESCRIPTOR, HANDLE, DWORD, PGENERIC_MAPPING, PPRIVILEGE_SET, LPDWORD, LPDWORD, LPBOOL);
typedef BOOL (WINAPI *fo_ShellExecuteExA_t)(SHELLEXECUTEINFOA *);

static char g_foVs[MAX_PATH];           /* this game folder inside VirtualStore, trailing backslash */

/* can this Windows user (not elevated) create files and folders in dir? 1 yes, 0 no, -1 can't tell */
static int fo_can_write(const char *dir)
{
    HMODULE a = LoadLibraryA("advapi32.dll");
    fo_GetFileSecurityA_t gfs = a ? (fo_GetFileSecurityA_t)GetProcAddress(a, "GetFileSecurityA") : 0;
    fo_OpenProcessToken_t opt = a ? (fo_OpenProcessToken_t)GetProcAddress(a, "OpenProcessToken") : 0;
    fo_DuplicateToken_t dup = a ? (fo_DuplicateToken_t)GetProcAddress(a, "DuplicateToken") : 0;
    fo_AccessCheck_t ac = a ? (fo_AccessCheck_t)GetProcAddress(a, "AccessCheck") : 0;
    DWORD need = 0, granted = 0, psl; BOOL st = FALSE; PSECURITY_DESCRIPTOR sd; HANDLE tok = 0, imp = 0;
    GENERIC_MAPPING gm = { FILE_GENERIC_READ, FILE_GENERIC_WRITE, FILE_GENERIC_EXECUTE, FILE_ALL_ACCESS };
    struct { PRIVILEGE_SET ps; LUID_AND_ATTRIBUTES more[8]; } ps; int r = -1;
    const SECURITY_INFORMATION si = OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION;
    if (!gfs || !opt || !dup || !ac) return -1;
    gfs(dir, si, 0, 0, &need);
    if (!need || !(sd = (PSECURITY_DESCRIPTOR)HeapAlloc(GetProcessHeap(), 0, need))) return -1;
    if (gfs(dir, si, sd, need, &need) && opt(GetCurrentProcess(), TOKEN_QUERY | TOKEN_DUPLICATE, &tok) && dup(tok, SecurityImpersonation, &imp)) {
        psl = sizeof(ps);
        if (ac(sd, imp, FILE_ADD_FILE | FILE_ADD_SUBDIRECTORY, &gm, &ps.ps, &psl, &granted, &st)) r = st ? 1 : 0;
    }
    if (imp) CloseHandle(imp);
    if (tok) CloseHandle(tok);
    HeapFree(GetProcessHeap(), 0, sd);
    return r;
}

/* any file below dir (a few levels deep)? */
static int fo_has_files(const char *dir, int depth)
{
    char pat[MAX_PATH], sub[MAX_PATH]; WIN32_FIND_DATAA fd; HANDLE h; int found = 0;
    s_cpy(pat, dir, MAX_PATH); s_cat(pat, "*", MAX_PATH);
    if ((h = FindFirstFileA(pat, &fd)) == INVALID_HANDLE_VALUE) return 0;
    do {
        if (fd.cFileName[0] == '.' && (!fd.cFileName[1] || (fd.cFileName[1] == '.' && !fd.cFileName[2]))) continue;
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) { found = 1; break; }
        if (depth > 0) {
            s_cpy(sub, dir, MAX_PATH); s_cat(sub, fd.cFileName, MAX_PATH); s_cat(sub, "\\", MAX_PATH);
            if (fo_has_files(sub, depth - 1)) { found = 1; break; }
        }
    } while (FindNextFileA(h, &fd));
    FindClose(h);
    return found;
}

/* runs in its own thread (COM / the shell stay out of the game's main thread) */
static DWORD WINAPI fo_thread(LPVOID unused)
{
    int w, vs, r; char msg[1600], args[3 * MAX_PATH], helper[MAX_PATH];
    (void)unused;
    if (!GetPrivateProfileIntA("Folder", "Ask", 1, g_iniPath)) { slog("folder: [Folder] Ask=0 in SLAM.ini - not checking the game folder's permissions"); return 0; }
    {   /* <LOCALAPPDATA>\VirtualStore\<game folder without the drive> */
        char la[MAX_PATH]; DWORD n = GetEnvironmentVariableA("LOCALAPPDATA", la, MAX_PATH);
        g_foVs[0] = 0;
        if (n && n < MAX_PATH && g_gameDir[1] == ':' && g_gameDir[2] == '\\') {
            s_cpy(g_foVs, la, MAX_PATH); s_cat(g_foVs, "\\VirtualStore\\", MAX_PATH); s_cat(g_foVs, g_gameDir + 3, MAX_PATH);
        }
    }
    { char d[MAX_PATH]; int n; s_cpy(d, g_slamDir, MAX_PATH); n = s_len(d); if (n > 3 && d[n - 1] == '\\') d[n - 1] = 0; w = fo_can_write(d); }
    vs = g_foVs[0] && fo_has_files(g_foVs, 6);
    if (w != 0 && !vs) return 0;                                        /* normal folder */
    slog("folder: %s%s", w == 0 ? "Windows does not let this user write into the game folder (Program Files?)" : "the game folder is writable",
         vs ? " - and Windows has put files of it into VirtualStore" : "");
    if (vs) slog("folder: VirtualStore: %s", g_foVs);
    s_cpy(msg, "NBA 2K14 is in a folder Windows protects:\n  ", sizeof(msg)); s_cat(msg, g_gameDir, sizeof(msg));   /* (not s_fmt: wsprintf stops at 1024) */
    s_cat(msg, "\n\nThere, SLAM and your mods can't save their settings, logs and roster files the normal way. "
               "Windows puts them somewhere else (VirtualStore), and changes you make to the mods' files can be ignored.\n\n"
               "Yes - fix it: Windows asks once for administrator rights. Then your Windows user may change the NBA 2K14 "
               "folder, and the files Windows put somewhere else are moved back.\n"
               "No - start the game without it (SLAM asks again next time).\n"
               "Cancel - start the game and don't ask again.\n\n"
               "(Or move NBA 2K14 out of Program Files, e.g. to C:\\Games\\NBA 2K14.)", sizeof(msg));
    r = MessageBoxA(NULL, msg, "SLAM - NBA 2K14 folder", MB_YESNOCANCEL | MB_ICONWARNING | MB_TOPMOST | MB_SETFOREGROUND);
    if (r == IDCANCEL) { WritePrivateProfileStringA("Folder", "Ask", "0", g_iniPath); slog("folder: not fixed, won't ask again ([Folder] Ask=0)"); return 0; }
    if (r != IDYES) { slog("folder: not fixed this time"); return 0; }
    s_cpy(helper, g_slamDir, MAX_PATH); s_cat(helper, "SLAMCrash.exe", MAX_PATH);
    if (!file_exists(helper)) {
        slog("folder: ERROR %s is missing - it does the fix. Copy it from the SLAM zip.", helper);
        MessageBoxA(NULL, "SLAM\\SLAMCrash.exe is missing - it does the fix.\nCopy it from the SLAM zip (Game folder\\SLAM\\SLAMCrash.exe).",
                    "SLAM - NBA 2K14 folder", MB_OK | MB_ICONERROR | MB_TOPMOST);
        return 0;
    }
    {   /* the game folder without its trailing backslash (a \" would end the quotes) */
        char gd[MAX_PATH], vd[MAX_PATH]; int n;
        s_cpy(gd, g_gameDir, MAX_PATH); n = s_len(gd); if (n > 3 && gd[n - 1] == '\\') gd[n - 1] = 0;
        s_cpy(vd, g_foVs, MAX_PATH); n = s_len(vd); if (n > 3 && vd[n - 1] == '\\') vd[n - 1] = 0;
        s_fmt(args, sizeof(args), "/grant \"%s\" \"%s\"", gd, vd);
    }
    {
        HMODULE sh = LoadLibraryA("shell32.dll");
        fo_ShellExecuteExA_t see = sh ? (fo_ShellExecuteExA_t)GetProcAddress(sh, "ShellExecuteExA") : 0;
        SHELLEXECUTEINFOA x; DWORD code = 1;
        if (!see) { slog("folder: ERROR shell32 ShellExecuteExA missing"); return 0; }
        /* SLAM.log is open (it may sit in VirtualStore): close it so it can be moved back, reopen after */
        EnterCriticalSection(&g_logLock);
        if (g_log != INVALID_HANDLE_VALUE) { CloseHandle(g_log); g_log = INVALID_HANDLE_VALUE; }
        LeaveCriticalSection(&g_logLock);
        memset(&x, 0, sizeof(x)); x.cbSize = sizeof(x); x.fMask = SEE_MASK_NOCLOSEPROCESS;
        x.lpVerb = "runas"; x.lpFile = helper; x.lpParameters = args; x.nShow = SW_HIDE;
        if (!see(&x)) code = 0x80000000u | GetLastError();
        else if (x.hProcess) { WaitForSingleObject(x.hProcess, 300000); GetExitCodeProcess(x.hProcess, &code); CloseHandle(x.hProcess); }
        EnterCriticalSection(&g_logLock);
        g_log = CreateFileA(g_logPath, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (g_log != INVALID_HANDLE_VALUE) SetFilePointer(g_log, 0, NULL, FILE_END);
        LeaveCriticalSection(&g_logLock);
        if (code == 0) slog("folder: fixed - your Windows user may change the NBA 2K14 folder now (details: SLAM\\SLAM folder permission.log)");
        else if (code == (0x80000000u | ERROR_CANCELLED)) slog("folder: not fixed - the Windows prompt was declined (SLAM asks again next time)");
        else slog("folder: NOT fixed (code %08X) - see SLAM\\SLAM folder permission.log", code);
    }
    return 0;
}

/* called at the start of load_all(), before any mod */
static void folder_check(void)
{
    HANDLE t = CreateThread(NULL, 0, fo_thread, NULL, 0, NULL);
    if (t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
}
