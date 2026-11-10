/* SLAMCrash.exe - the window that SLAM opens after NBA 2K14 crashed
 *
 *   SLAMCrash.exe "<SLAM\crash\crash_....log>"   show that report
 *   SLAMCrash.exe                                show the newest report in <this folder>\crash\
 *
 * First (1.1) it adds what it finds on disk to the report, once (findings.inc): changed game files the
 * game loaded, problem lines and last lines of the mods' logs, probe builds still installed.
 * Top: the likely cause in plain words (from the report's [facts] block, see cause.inc) and who
 * to send it to. Below: the whole report. Buttons: Copy report, Open crash folder, Close.
 * It is a separate program because a crashed game can't be trusted to draw a window.
 * No C runtime; imports kernel32, user32, gdi32, shell32 only.
 */
#include <windows.h>
#include <shellapi.h>
#include "cause.inc"

#define ID_COPY   101
#define ID_FOLDER 102
#define ID_CLOSE  103
#define ID_TIMER  1

static char g_path[MAX_PATH];
static char *g_report;                   /* whole file, \r\n line ends */
static CauseFacts g_fx;
static CauseExtra g_cx;
static CauseResult g_cause;
static char g_causeText[7600];           /* headline + body, for the copy button and the edit */
static HWND g_wnd, g_head, g_causeEdit, g_who, g_label, g_log, g_btn[3];
static HFONT g_fUi, g_fBold, g_fMono;
static int g_dpi = 96;
static int g_test;

#if defined(_MSC_VER) && !defined(__clang__)
#pragma function(memset, memcpy)
#endif
void *__cdecl memset(void *d, int c, size_t n) { unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
void *__cdecl memcpy(void *d, const void *s, size_t n) { unsigned char *p = (unsigned char *)d; const unsigned char *q = (const unsigned char *)s; while (n--) *p++ = *q++; return d; }

static int px(int v) { return MulDiv(v, g_dpi, 96); }
static void s_cpy(char *d, const char *s, int cap) { int i = 0; for (; s[i] && i < cap - 1; i++) d[i] = s[i]; d[i] = 0; }
#include "findings.inc"

/* ---- the report file -------------------------------------------------------------------------- */
static char *read_text(const char *path)
{
    HANDLE f = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL);
    DWORD sz, got = 0, i, j = 0; char *raw, *out;
    if (f == INVALID_HANDLE_VALUE) return 0;
    sz = GetFileSize(f, NULL);
    if (sz == INVALID_FILE_SIZE || sz > (8u << 20)) { CloseHandle(f); return 0; }
    raw = (char *)VirtualAlloc(NULL, sz + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    out = (char *)VirtualAlloc(NULL, sz * 2 + 1, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!raw || !out || !ReadFile(f, raw, sz, &got, NULL)) { CloseHandle(f); return 0; }
    CloseHandle(f);
    /* the edit control wants \r\n; also drop NULs */
    for (i = 0; i < got; i++) {
        char c = raw[i];
        if (!c) continue;
        if (c == '\n' && (i == 0 || raw[i - 1] != '\r')) out[j++] = '\r';
        out[j++] = c;
    }
    out[j] = 0;
    VirtualFree(raw, 0, MEM_RELEASE);
    return out;
}

/* newest crash_*.log / test_*.log in <exe folder>\crash\ (names sort by date) */
static int find_newest(char *out)
{
    char dir[MAX_PATH], pat[MAX_PATH], best[MAX_PATH]; WIN32_FIND_DATAA fd; HANDLE h; int n;
    FILETIME bestT = { 0, 0 };
    GetModuleFileNameA(NULL, dir, MAX_PATH);
    for (n = c_len(dir); n > 0 && dir[n - 1] != '\\'; n--) ;
    dir[n] = 0;
    c_cat(dir, "crash\\", MAX_PATH);
    s_cpy(pat, dir, MAX_PATH); c_cat(pat, "*.log", MAX_PATH);
    best[0] = 0;
    h = FindFirstFileA(pat, &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do if (CompareFileTime(&fd.ftLastWriteTime, &bestT) > 0) { bestT = fd.ftLastWriteTime; s_cpy(best, fd.cFileName, MAX_PATH); }
    while (FindNextFileA(h, &fd));
    FindClose(h);
    if (!best[0]) return 0;
    s_cpy(out, dir, MAX_PATH); c_cat(out, best, MAX_PATH);
    return 1;
}

/* first argument of the command line (quotes allowed) */
static void first_arg(char *out, int cap)
{
    const char *c = GetCommandLineA(); int n = 0;
    if (*c == '"') { c++; while (*c && *c != '"') c++; if (*c) c++; }
    else while (*c && *c != ' ' && *c != '\t') c++;
    while (*c == ' ' || *c == '\t') c++;
    if (*c == '"') { c++; while (*c && *c != '"' && n < cap - 1) out[n++] = *c++; }
    else while (*c && n < cap - 1) out[n++] = *c++;
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) n--;
    out[n] = 0;
}

/* ---- buttons ---------------------------------------------------------------------------------- */
static void copy_report(void)
{
    int a = c_len(g_causeText), b = c_len(g_report), i;
    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)(a + b + 8));
    char *p;
    if (!mem || !OpenClipboard(g_wnd)) { if (mem) GlobalFree(mem); return; }
    p = (char *)GlobalLock(mem);
    for (i = 0; i < a; i++) *p++ = g_causeText[i];
    *p++ = '\r'; *p++ = '\n'; *p++ = '\r'; *p++ = '\n';
    for (i = 0; i < b; i++) *p++ = g_report[i];
    *p = 0;
    GlobalUnlock(mem);
    EmptyClipboard();
    if (!SetClipboardData(CF_TEXT, mem)) GlobalFree(mem);
    CloseClipboard();
    SetWindowTextA(g_btn[0], "Copied!");
}

static void open_folder(void)
{
    char args[MAX_PATH + 16];
    s_cpy(args, "/select,\"", sizeof(args)); c_cat(args, g_path, sizeof(args)); c_cat(args, "\"", sizeof(args));
    ShellExecuteA(g_wnd, "open", "explorer.exe", args, NULL, SW_SHOWNORMAL);
}

/* ---- layout ----------------------------------------------------------------------------------- */
static void layout(int w, int h)
{
    int m = px(12), y = m, bw = px(130), bh = px(28), headH = px(24), causeH = px(150), whoH = g_cause.blame[0] ? px(22) : 0, lblH = px(20);
    int logY, logH;
    MoveWindow(g_head, m, y, w - 2 * m, headH, TRUE);          y += headH + px(4);
    MoveWindow(g_causeEdit, m, y, w - 2 * m, causeH, TRUE);    y += causeH + px(6);
    MoveWindow(g_who, m, y, w - 2 * m, whoH, TRUE);            y += whoH;
    MoveWindow(g_label, m, y, w - 2 * m, lblH, TRUE);          y += lblH;
    logY = y; logH = h - logY - bh - 2 * m;
    if (logH < px(40)) logH = px(40);
    MoveWindow(g_log, m, logY, w - 2 * m, logH, TRUE);
    MoveWindow(g_btn[2], w - m - bw, h - m - bh, bw, bh, TRUE);
    MoveWindow(g_btn[1], w - 2 * m - 2 * bw, h - m - bh, bw, bh, TRUE);
    MoveWindow(g_btn[0], m, h - m - bh, bw, bh, TRUE);
}

static HWND child(const char *cls, const char *text, DWORD style, int id, HFONT f)
{
    HWND c = CreateWindowExA(0, cls, text, WS_CHILD | WS_VISIBLE | style, 0, 0, 10, 10, g_wnd, (HMENU)(INT_PTR)id, GetModuleHandleA(NULL), NULL);
    SendMessageA(c, WM_SETFONT, (WPARAM)f, TRUE);
    return c;
}

static LRESULT CALLBACK wndproc(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE: {
        char who[400];
        g_wnd = w;
        g_head = child("STATIC", g_cause.headline, SS_LEFT | SS_NOPREFIX | SS_ENDELLIPSIS, 0, g_fBold);
        g_causeEdit = child("EDIT", g_causeText, ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_BORDER, 0, g_fUi);
        who[0] = 0;
        if (g_cause.blame[0]) {
            char nice[300]; c_nice_list(nice, g_cause.blame, sizeof(nice));
            c_cat(who, "Send this report to the author of: ", sizeof(who)); c_cat(who, nice, sizeof(who));
        }
        g_who = child("STATIC", who, SS_LEFT | SS_NOPREFIX, 0, g_fBold);
        g_label = child("STATIC", "Full report (for the mod author):", SS_LEFT | SS_NOPREFIX, 0, g_fUi);
        g_log = child("EDIT", "", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_AUTOHSCROLL | WS_VSCROLL | WS_HSCROLL | WS_BORDER, 0, g_fMono);
        SendMessageA(g_log, EM_SETLIMITTEXT, 0, 0);
        SetWindowTextA(g_log, g_report);
        g_btn[0] = child("BUTTON", "Copy report", BS_PUSHBUTTON, ID_COPY, g_fUi);
        g_btn[1] = child("BUTTON", "Open crash folder", BS_PUSHBUTTON, ID_FOLDER, g_fUi);
        g_btn[2] = child("BUTTON", "Close", BS_DEFPUSHBUTTON, ID_CLOSE, g_fUi);
        SetTimer(w, ID_TIMER, 2500, NULL);
        return 0;
    }
    case WM_SIZE: layout(LOWORD(lp), HIWORD(lp)); return 0;
    case WM_GETMINMAXINFO: ((MINMAXINFO *)lp)->ptMinTrackSize.x = px(520); ((MINMAXINFO *)lp)->ptMinTrackSize.y = px(420); return 0;
    case WM_TIMER:   /* came up on top of the closing game; now behave like a normal window */
        KillTimer(w, ID_TIMER);
        SetWindowPos(w, HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        return 0;
    case WM_CTLCOLORSTATIC:
        if ((HWND)lp == g_causeEdit) { SetBkColor((HDC)wp, GetSysColor(COLOR_WINDOW)); return (LRESULT)GetSysColorBrush(COLOR_WINDOW); }
        if ((HWND)lp == g_who || (HWND)lp == g_head) {
            SetBkMode((HDC)wp, TRANSPARENT);
            if ((HWND)lp == g_head && !g_test) SetTextColor((HDC)wp, g_cause.sure == 2 ? RGB(170, 20, 20) : RGB(150, 70, 0));
            return (LRESULT)GetSysColorBrush(COLOR_BTNFACE);
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_COPY: copy_report(); return 0;
        case ID_FOLDER: open_folder(); return 0;
        case ID_CLOSE: case IDCANCEL: case IDOK: DestroyWindow(w); return 0;
        }
        break;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(w, msg, wp, lp);
}

static void make_fonts(void)
{
    NONCLIENTMETRICSA ncm; LOGFONTA lf; HDC dc = GetDC(NULL);
    g_dpi = GetDeviceCaps(dc, LOGPIXELSY);
    ReleaseDC(NULL, dc);
    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = sizeof(ncm);
    if (!SystemParametersInfoA(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0)) {
        memset(&ncm.lfMessageFont, 0, sizeof(LOGFONTA));
        ncm.lfMessageFont.lfHeight = -px(12);
        s_cpy(ncm.lfMessageFont.lfFaceName, "Segoe UI", LF_FACESIZE);
    }
    g_fUi = CreateFontIndirectA(&ncm.lfMessageFont);
    lf = ncm.lfMessageFont; lf.lfWeight = FW_BOLD; lf.lfHeight = lf.lfHeight * 115 / 100;
    g_fBold = CreateFontIndirectA(&lf);
    memset(&lf, 0, sizeof(lf));
    lf.lfHeight = -px(12); lf.lfWeight = FW_NORMAL; lf.lfCharSet = DEFAULT_CHARSET; lf.lfPitchAndFamily = FIXED_PITCH | FF_MODERN;
    s_cpy(lf.lfFaceName, "Consolas", LF_FACESIZE);
    g_fMono = CreateFontIndirectA(&lf);
}

/* the cause text shown in the top box */
static void build_cause_text(void)
{
    const char *sure = g_test ? "" : g_cause.sure == 2 ? "Likely cause: " : g_cause.sure == 1 ? "Possible cause: " : "Unclear: ";
    g_causeText[0] = 0;
    c_cat(g_causeText, sure, sizeof(g_causeText));
    c_cat(g_causeText, g_cause.headline, sizeof(g_causeText));
    c_cat(g_causeText, "\r\n\r\n", sizeof(g_causeText));
    c_cat(g_causeText, g_cause.body, sizeof(g_causeText));
    /* headline line on top: with the "Likely cause:" prefix */
    s_cpy(g_cause.headline, g_causeText, sizeof(g_cause.headline));
    { int i; for (i = 0; g_cause.headline[i]; i++) if (g_cause.headline[i] == '\r') { g_cause.headline[i] = 0; break; } }
}

void __stdcall start(void)
{
    WNDCLASSA wc; MSG msg; RECT r; int w, h;
    BOOL (WINAPI *dpiAware)(void) = (BOOL (WINAPI *)(void))GetProcAddress(GetModuleHandleA("user32.dll"), "SetProcessDPIAware");
    if (dpiAware) dpiAware();

    first_arg(g_path, MAX_PATH);
    if (!g_path[0] && !find_newest(g_path)) {
        MessageBoxA(NULL, "No crash report found.\n\nSLAMCrash.exe shows SLAM's crash reports (SLAM\\crash\\*.log).\n"
                          "You can also drag a report onto SLAMCrash.exe.", "SLAM crash reports", MB_OK | MB_ICONINFORMATION);
        ExitProcess(0);
    }
    g_report = read_text(g_path);
    if (!g_report) {
        char m[MAX_PATH + 64];
        s_cpy(m, "Could not read the crash report:\n\n", sizeof(m)); c_cat(m, g_path, sizeof(m));
        MessageBoxA(NULL, m, "SLAM crash report", MB_OK | MB_ICONERROR);
        ExitProcess(1);
    }
    if (add_findings(g_path, g_report)) {              /* changed files + mod logs, added to the report once */
        char *again = read_text(g_path);
        if (again) { VirtualFree(g_report, 0, MEM_RELEASE); g_report = again; }
    }
    cause_parse(g_report, &g_fx);
    cause_extra(g_report, &g_cx);
    cause_guess(&g_fx, &g_cx, &g_cause);
    g_test = (int)fact_u(&g_fx, "test");
    build_cause_text();
    make_fonts();

    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndproc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.hCursor = LoadCursorA(NULL, (LPCSTR)IDC_ARROW);
    wc.hIcon = LoadIconA(NULL, (LPCSTR)(g_test ? IDI_INFORMATION : IDI_ERROR));
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "SLAMCrashWindow";
    RegisterClassA(&wc);

    w = px(780); h = px(640);
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &r, 0);
    if (h > r.bottom - r.top) h = r.bottom - r.top;
    CreateWindowExA(WS_EX_TOPMOST, wc.lpszClassName, g_test == 2 ? "SLAM - test crash (the game was closed on purpose)" : g_test ? "SLAM - test report (the game did not crash)" : "NBA 2K14 crashed - SLAM",
                    WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                    r.left + (r.right - r.left - w) / 2, r.top + (r.bottom - r.top - h) / 2, w, h, NULL, NULL, wc.hInstance, NULL);
    if (!g_wnd) ExitProcess(1);
    SetForegroundWindow(g_wnd);
    FlashWindow(g_wnd, TRUE);
    SetFocus(g_btn[2]);
    if (!g_test) MessageBeep(MB_ICONERROR);

    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        if (IsDialogMessageA(g_wnd, &msg)) continue;   /* Tab, Enter, Esc */
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    ExitProcess(0);
}
