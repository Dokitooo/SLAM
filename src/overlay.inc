/* overlay.inc - the mod list shown at the top left when the game starts (text only, then gone)
 *
 *   SLAM 1.0.0
 *   BodyHook 1.19.9      SLAM
 *   CrowdTune            plain .asi
 *   OldMod.asi           FAILED to load
 *   1 conflict - see SLAM\SLAM.log
 *
 * How: at game-ready SLAM hooks d3d9.dll's Direct3DCreate9 -> IDirect3D9::CreateDevice ->
 * IDirect3DDevice9::Present (on the functions themselves, with MinHook, owner "SLAM", so mods
 * that hook the device's vtable keep working). The text is drawn once with GDI into a texture
 * and put on screen in Present for [Overlay] Seconds (SLAM.ini, default 3). Then SLAM removes all
 * three hooks: after that SLAM has nothing in Direct3D at all.
 * Nothing is drawn if [Overlay] Show=0.
 */
#define COBJMACROS
#include <d3d9.h>

/* the vtable slots used below, checked against the d3d9.h layout at compile time */
#include <stddef.h>
typedef char ov_check_present[(sizeof(void *) * 17 == offsetof(IDirect3DDevice9Vtbl, Present)) ? 1 : -1];
typedef char ov_check_createdev[(sizeof(void *) * 16 == offsetof(IDirect3D9Vtbl, CreateDevice)) ? 1 : -1];

static int g_ovShow = 1, g_ovMs = 3000;
static volatile LONG g_ovDone;                  /* list shown (or given up): hooks removed */
static void *g_ovD3DCreate, *g_ovCreateDev, *g_ovPresent;   /* hooked functions */
typedef IDirect3D9 *(WINAPI *D3DCreate9_t)(UINT);
typedef HRESULT (WINAPI *CreateDevice_t)(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
typedef HRESULT (WINAPI *Present_t)(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
static D3DCreate9_t o_D3DCreate9;
static CreateDevice_t o_CreateDevice;
static Present_t o_Present;

static void overlay_config(const char *ini)
{
    g_ovShow = GetPrivateProfileIntA("Overlay", "Show", 1, ini);
    g_ovMs = GetPrivateProfileIntA("Overlay", "Seconds", 3, ini) * 1000;
    if (g_ovMs < 500) g_ovMs = 500;
    if (g_ovMs > 30000) g_ovMs = 30000;
}

/* ---- the text ------------------------------------------------------------------------------ */
#define OV_LINES 48
static char g_ovText[OV_LINES][96], g_ovTag[OV_LINES][48];
static COLORREF g_ovColor[OV_LINES], g_ovTagColor[OV_LINES];
static int g_ovN;

#define OV_WHITE RGB(255, 255, 255)
#define OV_GREEN RGB(140, 235, 140)
#define OV_GREY  RGB(175, 175, 175)
#define OV_RED   RGB(255, 120, 110)
#define OV_AMBER RGB(255, 200, 90)
static void ov_line(const char *text, COLORREF c, const char *tag, COLORREF tc)
{
    if (g_ovN >= OV_LINES) return;
    s_cpy(g_ovText[g_ovN], text, 96); s_cpy(g_ovTag[g_ovN], tag ? tag : "", 48);
    g_ovColor[g_ovN] = c; g_ovTagColor[g_ovN] = tc; g_ovN++;
}

static void ov_build_list(void)
{
    char t[96]; int i;
    g_ovN = 0;
    s_fmt(t, sizeof(t), "SLAM %s", SLAM_VERSION);
    ov_line(t, OV_WHITE, 0, 0);
    for (i = 0; i < g_modN && g_ovN < OV_LINES - 3; i++) {
        SlamMod *m = &g_mods[i];
        if (m->isSlam) {
            s_fmt(t, sizeof(t), "%s %s", m->name, m->version);
            if (m->started && !m->failed) ov_line(t, OV_WHITE, "SLAM", OV_GREEN);
            else ov_line(t, OV_WHITE, m->status[0] ? m->status : "not running", OV_RED);
        } else ov_line(m->name, OV_WHITE, "plain .asi", OV_GREY);
    }
    for (i = 0; i < g_failedN && g_ovN < OV_LINES - 2; i++) ov_line(s_base(g_failed[i]), OV_WHITE, "FAILED to load", OV_RED);
    if (!g_modN && !g_failedN) ov_line("no mods", OV_GREY, 0, 0);
    if (g_conflictN) {
        s_fmt(t, sizeof(t), "%d conflict%s - see SLAM\\SLAM.log", g_conflictN, g_conflictN == 1 ? "" : "s");
        ov_line(t, OV_AMBER, 0, 0);
    }
}

/* ---- text -> texture (GDI, once) --------------------------------------------------------------- */
static IDirect3DTexture9 *g_ovTex;
static int g_ovW, g_ovH;
static DWORD g_ovT0;

static int ov_make_texture(IDirect3DDevice9 *dev, int screenH)
{
    int fontPx = screenH / 72, lineH, i, x, y, w = 0, tagX, pad = 3;
    HDC dc; HFONT font, oldFont; BITMAPINFO bi; void *bitsA = 0, *bitsB = 0; HBITMAP bmA, bmB, oldBm;
    D3DLOCKED_RECT lr; SIZE sz; int nameW = 0, tagW = 0;
    if (fontPx < 12) fontPx = 12;
    if (fontPx > 28) fontPx = 28;
    lineH = fontPx * 5 / 4;
    dc = CreateCompatibleDC(NULL);
    if (!dc) return 0;
    font = CreateFontA(-fontPx, 0, 0, 0, FW_SEMIBOLD, 0, 0, 0, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_SWISS, "Segoe UI");
    oldFont = (HFONT)SelectObject(dc, font);
    for (i = 0; i < g_ovN; i++) {
        if (GetTextExtentPoint32A(dc, g_ovText[i], s_len(g_ovText[i]), &sz) && sz.cx > nameW && g_ovTag[i][0]) nameW = sz.cx;
        if (!g_ovTag[i][0] && GetTextExtentPoint32A(dc, g_ovText[i], s_len(g_ovText[i]), &sz) && sz.cx > w) w = sz.cx;
        if (g_ovTag[i][0] && GetTextExtentPoint32A(dc, g_ovTag[i], s_len(g_ovTag[i]), &sz) && sz.cx > tagW) tagW = sz.cx;
    }
    tagX = pad + nameW + fontPx;                        /* tags line up in a second column */
    if (tagX + tagW + pad > w + 2 * pad) w = tagX + tagW;
    g_ovW = w + 2 * pad; g_ovH = g_ovN * lineH + 2 * pad;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(bi.bmiHeader); bi.bmiHeader.biWidth = g_ovW; bi.bmiHeader.biHeight = -g_ovH;
    bi.bmiHeader.biPlanes = 1; bi.bmiHeader.biBitCount = 32; bi.bmiHeader.biCompression = BI_RGB;
    bmA = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bitsA, NULL, 0);       /* the letters */
    bmB = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bitsB, NULL, 0);       /* the black outline */
    if (!bmA || !bmB || !bitsA || !bitsB) goto fail;
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(255, 255, 255));
    for (x = 0; x < 2; x++) {
        int ox, oy;
        oldBm = (HBITMAP)SelectObject(dc, x ? bmB : bmA);
        memset(x ? bitsB : bitsA, 0, (size_t)g_ovW * g_ovH * 4);
        for (oy = -2; oy <= 2; oy++) for (ox = -2; ox <= 2; ox++) {
            if (x == 0 && (ox || oy)) continue;                             /* letters: once, no offset */
            if (x == 1 && ox * ox + oy * oy > 5) continue;                  /* outline: a round 2 px ring */
            for (i = 0, y = pad; i < g_ovN; i++, y += lineH) {
                TextOutA(dc, pad + ox, y + oy, g_ovText[i], s_len(g_ovText[i]));
                if (g_ovTag[i][0]) TextOutA(dc, tagX + ox, y + oy, g_ovTag[i], s_len(g_ovTag[i]));
            }
        }
        GdiFlush();
        SelectObject(dc, oldBm);
    }
    if (FAILED(IDirect3DDevice9_CreateTexture(dev, g_ovW, g_ovH, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &g_ovTex, NULL))) goto fail;
    if (FAILED(IDirect3DTexture9_LockRect(g_ovTex, 0, &lr, NULL, 0))) { IDirect3DTexture9_Release(g_ovTex); g_ovTex = 0; goto fail; }
    for (y = 0; y < g_ovH; y++) {
        const u32 *a = (const u32 *)bitsA + y * g_ovW, *b = (const u32 *)bitsB + y * g_ovW;
        u32 *d = (u32 *)((u8 *)lr.pBits + y * lr.Pitch);
        int line = (y - pad) / lineH;
        if (line < 0) line = 0;
        if (line >= g_ovN) line = g_ovN - 1;
        for (x = 0; x < g_ovW; x++) {
            u32 ca = a[x] & 0xFF, cb = b[x] & 0xFF, al = ca > cb ? ca : cb, r = 0, g = 0, bl = 0;
            COLORREF c = g_ovTag[line][0] && x >= tagX - 3 ? g_ovTagColor[line] : g_ovColor[line];
            if (al) { r = GetRValue(c) * ca / al; g = GetGValue(c) * ca / al; bl = GetBValue(c) * ca / al; }
            d[x] = (al << 24) | (r << 16) | (g << 8) | bl;
        }
    }
    IDirect3DTexture9_UnlockRect(g_ovTex, 0);
fail:
    SelectObject(dc, oldFont);
    if (bmA) DeleteObject(bmA);
    if (bmB) DeleteObject(bmB);
    DeleteObject(font); DeleteDC(dc);
    return g_ovTex != 0;
}

/* ---- drawing, inside Present ------------------------------------------------------------------ */
typedef struct { float x, y, z, rhw, u, v; } OvVert;

static void ov_draw(IDirect3DDevice9 *dev)
{
    IDirect3DStateBlock9 *sb = 0; IDirect3DSurface9 *bb = 0, *rt = 0; OvVert q[4]; float x0 = 12, y0 = 10, x1, y1;
    if (FAILED(IDirect3DDevice9_GetBackBuffer(dev, 0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    if (!g_ovTex) {
        D3DSURFACE_DESC sd;
        if (FAILED(IDirect3DSurface9_GetDesc(bb, &sd)) || !ov_make_texture(dev, (int)sd.Height)) {
            IDirect3DSurface9_Release(bb);
            slog("overlay: could not draw the mod list - skipped");
            InterlockedExchange(&g_ovDone, 1);
            return;
        }
        g_ovT0 = GetTickCount();
    }
    if (FAILED(IDirect3DDevice9_CreateStateBlock(dev, D3DSBT_ALL, &sb))) { IDirect3DSurface9_Release(bb); return; }
    IDirect3DDevice9_GetRenderTarget(dev, 0, &rt);
    IDirect3DDevice9_SetRenderTarget(dev, 0, bb);
    if (SUCCEEDED(IDirect3DDevice9_BeginScene(dev))) {
        x1 = x0 + (float)g_ovW; y1 = y0 + (float)g_ovH;
        q[0].x = x0 - 0.5f; q[0].y = y0 - 0.5f; q[0].u = 0; q[0].v = 0;
        q[1].x = x1 - 0.5f; q[1].y = y0 - 0.5f; q[1].u = 1; q[1].v = 0;
        q[2].x = x0 - 0.5f; q[2].y = y1 - 0.5f; q[2].u = 0; q[2].v = 1;
        q[3].x = x1 - 0.5f; q[3].y = y1 - 0.5f; q[3].u = 1; q[3].v = 1;
        { int i; for (i = 0; i < 4; i++) { q[i].z = 0; q[i].rhw = 1; } }
        IDirect3DDevice9_SetVertexShader(dev, NULL);
        IDirect3DDevice9_SetPixelShader(dev, NULL);
        IDirect3DDevice9_SetFVF(dev, D3DFVF_XYZRHW | D3DFVF_TEX1);
        IDirect3DDevice9_SetTexture(dev, 0, (IDirect3DBaseTexture9 *)g_ovTex);
        IDirect3DDevice9_SetTexture(dev, 1, NULL);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ZENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ZWRITEENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_STENCILENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_SCISSORTESTENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_CULLMODE, D3DCULL_NONE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_LIGHTING, FALSE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_FOGENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHATESTENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_SRGBWRITEENABLE, FALSE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_COLORWRITEENABLE, 0xF);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_ALPHABLENDENABLE, TRUE);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_BLENDOP, D3DBLENDOP_ADD);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_SRCBLEND, D3DBLEND_SRCALPHA);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
        IDirect3DDevice9_SetRenderState(dev, D3DRS_SEPARATEALPHABLENDENABLE, FALSE);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_TEXCOORDINDEX, 0);
        IDirect3DDevice9_SetTextureStageState(dev, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
        IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
        IDirect3DDevice9_SetTextureStageState(dev, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
        IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
        IDirect3DDevice9_SetSamplerState(dev, 0, D3DSAMP_SRGBTEXTURE, FALSE);
        IDirect3DDevice9_DrawPrimitiveUP(dev, D3DPT_TRIANGLESTRIP, 2, q, sizeof(OvVert));
        IDirect3DDevice9_EndScene(dev);
    }
    if (rt) { IDirect3DDevice9_SetRenderTarget(dev, 0, rt); IDirect3DSurface9_Release(rt); }
    IDirect3DStateBlock9_Apply(sb);                     /* every state back as the game had it */
    IDirect3DStateBlock9_Release(sb);
    IDirect3DSurface9_Release(bb);
}

static HRESULT WINAPI ov_Present(IDirect3DDevice9 *, const RECT *, const RECT *, HWND, const RGNDATA *);
static HRESULT WINAPI ov_CreateDevice(IDirect3D9 *, UINT, D3DDEVTYPE, HWND, DWORD, D3DPRESENT_PARAMETERS *, IDirect3DDevice9 **);
static IDirect3D9 *WINAPI ov_D3DCreate9(UINT);

/* list shown: take every SLAM hook out of Direct3D again */
static void ov_finish(void)
{
    if (InterlockedExchange(&g_ovDone, 2) == 2) return;
    if (g_ovPresent) hook_remove(&g_core, g_ovPresent, (void *)ov_Present);
    if (g_ovCreateDev) hook_remove(&g_core, g_ovCreateDev, (void *)ov_CreateDevice);
    if (g_ovD3DCreate) hook_remove(&g_core, g_ovD3DCreate, (void *)ov_D3DCreate9);
    slog("overlay: mod list shown for %d s - SLAM's Direct3D hooks removed", g_ovMs / 1000);
}

/* ---- the three hooks -------------------------------------------------------------------------- */
static HRESULT WINAPI ov_Present(IDirect3DDevice9 *dev, const RECT *a, const RECT *b, HWND w, const RGNDATA *r)
{
    if (!g_ovDone) {
        ov_draw(dev);
        if (g_ovTex && GetTickCount() - g_ovT0 >= (DWORD)g_ovMs) {
            IDirect3DTexture9_Release(g_ovTex); g_ovTex = 0;
            InterlockedExchange(&g_ovDone, 1);
        }
    }
    {
        HRESULT hr = o_Present(dev, a, b, w, r);
        if (g_ovDone == 1) ov_finish();
        return hr;
    }
}

static HRESULT WINAPI ov_CreateDevice(IDirect3D9 *d3d, UINT ad, D3DDEVTYPE t, HWND w, DWORD f, D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out)
{
    HRESULT hr = o_CreateDevice(d3d, ad, t, w, f, pp, out);
    if (SUCCEEDED(hr) && out && *out && !g_ovPresent && !g_ovDone) {
        void *fn = ((void **)*(void **)*out)[17];           /* IDirect3DDevice9::Present */
        EnterCriticalSection(&g_hookLock);
        ov_build_list();
        LeaveCriticalSection(&g_hookLock);
        if (hook_add(&g_core, fn, (void *)ov_Present, (void **)&o_Present) == SLAM_OK) g_ovPresent = fn;
        else { slog("overlay: Present could not be hooked - no mod list"); InterlockedExchange(&g_ovDone, 1); }
    }
    return hr;
}

static IDirect3D9 *WINAPI ov_D3DCreate9(UINT sdk)
{
    IDirect3D9 *d = o_D3DCreate9(sdk);
    if (d && !g_ovCreateDev && !g_ovDone) {
        void *fn = ((void **)*(void **)d)[16];              /* IDirect3D9::CreateDevice */
        if (hook_add(&g_core, fn, (void *)ov_CreateDevice, (void **)&o_CreateDevice) == SLAM_OK) g_ovCreateDev = fn;
    }
    return d;
}

/* game-ready: before the game creates Direct3D */
static void overlay_install(void)
{
    HMODULE d3d; void *fn;
    if (!g_ovShow) { slog("overlay: off (SLAM.ini [Overlay] Show=0)"); return; }
    d3d = LoadLibraryA("d3d9.dll");                      /* the same d3d9.dll the game will get */
    fn = d3d ? (void *)GetProcAddress(d3d, "Direct3DCreate9") : 0;
    if (!fn || hook_add(&g_core, fn, (void *)ov_D3DCreate9, (void **)&o_D3DCreate9) != SLAM_OK) {
        slog("overlay: Direct3DCreate9 could not be hooked - no mod list");
        return;
    }
    g_ovD3DCreate = fn;
}
