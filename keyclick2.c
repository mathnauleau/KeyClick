/* KeyClick v2 - turn any key into a mouse button.
 * Settings window (key capture, click type, hold to drag, pause shortcut, start with Windows),
 * tray icon + menu, animated pixel cat.  Win32 + GDI/GDI+, no other dependencies.
 *
 * Build (Zig as cross compiler):
 *   zig cc -target x86_64-windows-gnu -O2 -s -Wl,--subsystem,windows keyclick2.c -o keyclick2.exe \
 *          -luser32 -lgdi32 -lshell32 -ladvapi32 -lgdiplus
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include "cat_frames.h"

#ifndef MOD_NOREPEAT
#define MOD_NOREPEAT 0x4000
#endif

/* ---------- GDI+ flat API (declared by hand so no C++ headers are needed) ---------- */
typedef struct { UINT32 Version; void *DebugCb; BOOL NoThread; BOOL NoCodecs; } GpStartup;
int WINAPI GdiplusStartup(ULONG_PTR *token, const GpStartup *in, void *out);
void WINAPI GdiplusShutdown(ULONG_PTR token);
int WINAPI GdipCreateFromHDC(HDC dc, void **g);
int WINAPI GdipDeleteGraphics(void *g);
int WINAPI GdipSetSmoothingMode(void *g, int mode);
int WINAPI GdipCreateSolidFill(UINT32 argb, void **brush);
int WINAPI GdipDeleteBrush(void *brush);
int WINAPI GdipCreatePen1(UINT32 argb, float width, int unit, void **pen);
int WINAPI GdipDeletePen(void *pen);
int WINAPI GdipCreatePath(int fillMode, void **path);
int WINAPI GdipDeletePath(void *path);
int WINAPI GdipAddPathArc(void *path, float x, float y, float w, float h, float start, float sweep);
int WINAPI GdipClosePathFigure(void *path);
int WINAPI GdipFillPath(void *g, void *brush, void *path);
int WINAPI GdipDrawPath(void *g, void *pen, void *path);
int WINAPI GdipFillEllipse(void *g, void *brush, float x, float y, float w, float h);

/* ---------- look (same values as the sketch) ---------- */
#define C_BG     RGB(0xF3, 0xF3, 0xF3)
#define C_WHITE  RGB(0xFF, 0xFF, 0xFF)
#define C_TEXT   RGB(0x1B, 0x1B, 0x1B)
#define C_SUB    RGB(0x5F, 0x5F, 0x5F)
#define C_ACC    RGB(0x00, 0x5F, 0xB8)
#define C_CARD   RGB(0xE5, 0xE5, 0xE5)
#define C_SEP    RGB(0xED, 0xED, 0xED)
#define C_TINT   RGB(0xE6, 0xF0, 0xFA)
#define C_NONE   0xFFFFFFFFu

#define CLIENT_W 520
#define CLIENT_H 604

enum { ID_SW_MAIN = 101, ID_BTN_KEY, ID_SEG0, ID_SEG1, ID_SEG2, ID_SW_HOLD, ID_BTN_SC, ID_SW_START };
enum { CAP_NONE = 0, CAP_KEY, CAP_SC };
enum { CMD_TOGGLE = 1, CMD_SHOW, CMD_QUIT };
#define WM_TRAY    (WM_APP + 1)
#define WM_CAPDONE (WM_APP + 2)
#define HK_ID      1
#define TIMER_CAT  1

/* ---------- state ---------- */
static HINSTANCE g_inst;
static HWND g_hwnd;
static HHOOK g_hook;
static int g_dpi = 96;
static HFONT f18b, f16b, f14, f13, f12;
static NOTIFYICONDATAW g_nid;
static UINT g_wmShow;
static WNDPROC g_orig[16];
static BOOL g_hover[16];

static BOOL g_enabled = TRUE, g_hold = TRUE, g_autostart = FALSE;
static int g_action = 0;                       /* 0 left, 1 right, 2 middle */
static DWORD g_scan = 0x29;                    /* key left of "1" */
static BOOL g_ext = FALSE;
static UINT g_hkmods = MOD_CONTROL | MOD_ALT, g_hkvk = 'K';
static UINT g_newmods, g_newvk;

static volatile int g_cap = CAP_NONE;
static BOOL g_down = FALSE;                    /* mapped key is currently down */
static DWORD g_heldUp = 0;                     /* mouse-up flag to send when a held button is released */
static DWORD g_pendingUp = 0;                  /* key-up to swallow after a capture (scan | ext<<16, +1) */
static int g_frame = 0;

#define S(v) MulDiv((v), g_dpi, 96)

/* ---------- helpers ---------- */
static COLORREF mix(COLORREF a, COLORREF b, int pctB)
{
    int r = (GetRValue(a) * (100 - pctB) + GetRValue(b) * pctB) / 100;
    int g = (GetGValue(a) * (100 - pctB) + GetGValue(b) * pctB) / 100;
    int bl = (GetBValue(a) * (100 - pctB) + GetBValue(b) * pctB) / 100;
    return RGB(r, g, bl);
}

static UINT32 argbc(COLORREF c)
{
    return 0xFF000000u | ((UINT32)GetRValue(c) << 16) | ((UINT32)GetGValue(c) << 8) | GetBValue(c);
}

static void *gp_begin(HDC dc)
{
    void *g = NULL;
    GdipCreateFromHDC(dc, &g);
    GdipSetSmoothingMode(g, 4);               /* anti-alias */
    return g;
}

static void *rpath(float x, float y, float w, float h, float r)
{
    void *p = NULL;
    float d = r * 2;
    if (d > w) d = w;
    if (d > h) d = h;
    if (d < 1) d = 1;
    GdipCreatePath(0, &p);
    GdipAddPathArc(p, x, y, d, d, 180, 90);
    GdipAddPathArc(p, x + w - d, y, d, d, 270, 90);
    GdipAddPathArc(p, x + w - d, y + h - d, d, d, 0, 90);
    GdipAddPathArc(p, x, y + h - d, d, d, 90, 90);
    GdipClosePathFigure(p);
    return p;
}

/* rounded rectangle: fill and/or inner stroke, anti-aliased */
static void rr(void *g, float x, float y, float w, float h, float r, COLORREF fill, COLORREF stroke, float sw)
{
    if (fill != C_NONE) {
        void *p = rpath(x, y, w, h, r), *b = NULL;
        GdipCreateSolidFill(argbc(fill), &b);
        GdipFillPath(g, b, p);
        GdipDeleteBrush(b);
        GdipDeletePath(p);
    }
    if (stroke != C_NONE) {
        float i = sw / 2;
        void *p = rpath(x + i, y + i, w - sw, h - sw, r > i ? r - i : 0.5f), *pen = NULL;
        GdipCreatePen1(argbc(stroke), sw, 2, &pen);
        GdipDrawPath(g, pen, p);
        GdipDeletePen(pen);
        GdipDeletePath(p);
    }
}

static void dot(void *g, float x, float y, float d, COLORREF c)
{
    void *b = NULL;
    GdipCreateSolidFill(argbc(c), &b);
    GdipFillEllipse(g, b, x, y, d, d);
    GdipDeleteBrush(b);
}

static void box(HDC dc, int x, int y, int w, int h, COLORREF c)
{
    RECT r = { x, y, x + w, y + h };
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}

/* text in a logical-pixel rectangle */
static void T(HDC dc, HFONT f, COLORREF c, int x, int y, int w, int h, const WCHAR *s, UINT flags)
{
    RECT r = { S(x), S(y), S(x + w), S(y + h) };
    SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    DrawTextW(dc, s, -1, &r, flags | DT_NOPREFIX);
}

static const WCHAR *action_name(void)
{
    return g_action == 1 ? L"Right click" : g_action == 2 ? L"Middle click" : L"Left click";
}

static void key_name(DWORD scan, BOOL ext, WCHAR *out, int n)
{
    LONG lp = (LONG)(scan << 16) | (ext ? (1L << 24) : 0);
    if (!GetKeyNameTextW(lp, out, n) || !out[0]) lstrcpynW(out, L"Key", n);
}

static void vk_name(UINT vk, WCHAR *out, int n)
{
    LONG lp = (LONG)(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC) << 16);
    switch (vk) {
    case VK_PRIOR: case VK_NEXT: case VK_END: case VK_HOME: case VK_LEFT: case VK_UP:
    case VK_RIGHT: case VK_DOWN: case VK_INSERT: case VK_DELETE: case VK_DIVIDE:
        lp |= (1L << 24);
    }
    if (!GetKeyNameTextW(lp, out, n) || !out[0]) lstrcpynW(out, L"?", n);
}

static void hk_text(WCHAR *out, int n)
{
    WCHAR k[40];
    out[0] = 0;
    if (g_hkmods & MOD_CONTROL) lstrcatW(out, L"Ctrl + ");
    if (g_hkmods & MOD_ALT)     lstrcatW(out, L"Alt + ");
    if (g_hkmods & MOD_SHIFT)   lstrcatW(out, L"Shift + ");
    if (g_hkmods & MOD_WIN)     lstrcatW(out, L"Win + ");
    vk_name(g_hkvk, k, 40);
    if (lstrlenW(out) + lstrlenW(k) < n) lstrcatW(out, k);
}

/* ---------- settings file + autostart ---------- */
static const WCHAR *RUNKEY = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

static void ini_path(WCHAR *p, BOOL makedir)
{
    WCHAR dir[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"APPDATA", dir, MAX_PATH - 20);
    if (!n || n >= MAX_PATH - 20) lstrcpyW(dir, L".");
    lstrcatW(dir, L"\\KeyClick");
    if (makedir) CreateDirectoryW(dir, NULL);
    lstrcpyW(p, dir);
    lstrcatW(p, L"\\settings.ini");
}

static void put_int(const WCHAR *key, int v, const WCHAR *path)
{
    WCHAR b[24];
    wsprintfW(b, L"%d", v);
    WritePrivateProfileStringW(L"KeyClick", key, b, path);
}

static void save_settings(void)
{
    WCHAR p[MAX_PATH];
    ini_path(p, TRUE);
    put_int(L"enabled", g_enabled, p);
    put_int(L"scan", (int)g_scan, p);
    put_int(L"ext", g_ext, p);
    put_int(L"action", g_action, p);
    put_int(L"hold", g_hold, p);
    put_int(L"hkmods", (int)g_hkmods, p);
    put_int(L"hkvk", (int)g_hkvk, p);
}

static BOOL autostart_get(void)
{
    DWORD sz = 0;
    return RegGetValueW(HKEY_CURRENT_USER, RUNKEY, L"KeyClick", RRF_RT_REG_SZ, NULL, NULL, &sz) == ERROR_SUCCESS;
}

static void autostart_set(BOOL on)
{
    if (on) {
        WCHAR exe[MAX_PATH], cmd[MAX_PATH + 16];
        HKEY k;
        GetModuleFileNameW(NULL, exe, MAX_PATH);
        wsprintfW(cmd, L"\"%s\" --tray", exe);
        if (RegCreateKeyExW(HKEY_CURRENT_USER, RUNKEY, 0, NULL, 0, KEY_SET_VALUE, NULL, &k, NULL) == ERROR_SUCCESS) {
            RegSetValueExW(k, L"KeyClick", 0, REG_SZ, (const BYTE *)cmd, (lstrlenW(cmd) + 1) * sizeof(WCHAR));
            RegCloseKey(k);
        }
    } else {
        RegDeleteKeyValueW(HKEY_CURRENT_USER, RUNKEY, L"KeyClick");
    }
    g_autostart = autostart_get();
}

static void load_settings(void)
{
    WCHAR p[MAX_PATH];
    ini_path(p, FALSE);
    g_enabled = GetPrivateProfileIntW(L"KeyClick", L"enabled", 1, p) != 0;
    g_scan = GetPrivateProfileIntW(L"KeyClick", L"scan", 0x29, p);
    g_ext = GetPrivateProfileIntW(L"KeyClick", L"ext", 0, p) != 0;
    g_action = (int)GetPrivateProfileIntW(L"KeyClick", L"action", 0, p);
    if (g_action < 0 || g_action > 2) g_action = 0;
    g_hold = GetPrivateProfileIntW(L"KeyClick", L"hold", 1, p) != 0;
    g_hkmods = GetPrivateProfileIntW(L"KeyClick", L"hkmods", MOD_CONTROL | MOD_ALT, p);
    g_hkvk = GetPrivateProfileIntW(L"KeyClick", L"hkvk", 'K', p);
    g_autostart = autostart_get();
}

/* ---------- mouse output ---------- */
static void mouse(DWORD flag)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    SendInput(1, &in, sizeof(in));
}

static DWORD flag_down(void) { return g_action == 1 ? MOUSEEVENTF_RIGHTDOWN : g_action == 2 ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_LEFTDOWN; }
static DWORD flag_up(void)   { return g_action == 1 ? MOUSEEVENTF_RIGHTUP   : g_action == 2 ? MOUSEEVENTF_MIDDLEUP   : MOUSEEVENTF_LEFTUP; }

static void release_if_held(void)
{
    if (g_heldUp) {
        mouse(g_heldUp);
        g_heldUp = 0;
    }
}

/* ---------- keyboard hook: key capture + the key-to-click mapping ---------- */
static BOOL is_modifier_vk(DWORD vk)
{
    return vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_CONTROL || vk == VK_LCONTROL ||
           vk == VK_RCONTROL || vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_LWIN || vk == VK_RWIN;
}

static LRESULT CALLBACK KbProc(int code, WPARAM wp, LPARAM lp)
{
    KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lp;
    BOOL down, up, ext;
    DWORD id;

    if (code != HC_ACTION || (k->flags & LLKHF_INJECTED))
        return CallNextHookEx(g_hook, code, wp, lp);

    down = (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN);
    up = (wp == WM_KEYUP || wp == WM_SYSKEYUP);
    ext = (k->flags & LLKHF_EXTENDED) != 0;
    id = k->scanCode | (ext ? 0x10000 : 0);

    if (up && g_pendingUp == id + 1) {         /* key-up of a key we swallowed during capture */
        g_pendingUp = 0;
        return 1;
    }

    if (g_cap != CAP_NONE) {
        if (down) {
            if (k->vkCode == VK_ESCAPE) {
                g_cap = CAP_NONE;
                g_pendingUp = id + 1;
                PostMessageW(g_hwnd, WM_CAPDONE, 0, 0);
                return 1;
            }
            if (g_cap == CAP_KEY) {
                if (k->vkCode == VK_LWIN || k->vkCode == VK_RWIN)
                    return CallNextHookEx(g_hook, code, wp, lp);
                g_scan = k->scanCode;
                g_ext = ext;
                g_cap = CAP_NONE;
                g_pendingUp = id + 1;
                PostMessageW(g_hwnd, WM_CAPDONE, 1, 0);
                return 1;
            } else {
                UINT mods = 0;
                if (is_modifier_vk(k->vkCode))
                    return CallNextHookEx(g_hook, code, wp, lp);
                if (GetAsyncKeyState(VK_CONTROL) & 0x8000) mods |= MOD_CONTROL;
                if (GetAsyncKeyState(VK_MENU) & 0x8000)    mods |= MOD_ALT;
                if (GetAsyncKeyState(VK_SHIFT) & 0x8000)   mods |= MOD_SHIFT;
                if ((GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000) mods |= MOD_WIN;
                if (mods) {                    /* a shortcut needs at least one modifier */
                    g_newmods = mods;
                    g_newvk = k->vkCode;
                    g_cap = CAP_NONE;
                    g_pendingUp = id + 1;
                    PostMessageW(g_hwnd, WM_CAPDONE, 2, 0);
                }
                return 1;
            }
        }
        if (up) return CallNextHookEx(g_hook, code, wp, lp);
    }

    if (g_enabled && k->scanCode == g_scan && ext == g_ext) {
        if (down) {
            if (!g_down) {
                g_down = TRUE;
                mouse(flag_down());
                if (g_hold) g_heldUp = flag_up();
                else mouse(flag_up());
            }
        } else if (up) {
            g_down = FALSE;
            release_if_held();
        }
        return 1;                              /* swallow: the key no longer types */
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

/* ---------- icon (cursor arrow, drawn from a small pattern) ---------- */
static const char *ARROW[16] = {
    "D...............", "DD..............", "DBD.............", "DBBD............",
    "DBBBD...........", "DBBBBD..........", "DBBBBBD.........", "DBBBBBBD........",
    "DBBBBBBBD.......", "DBBBBBBBBD......", "DBBBBBDDDDD.....", "DBBDBBD.........",
    "DBD.DBBD........", "DD..DBBD........", "D....DBBD.......", ".....DDD........"
};

static HICON make_icon(int sz)
{
    BITMAPINFO bi;
    void *bits = NULL;
    HDC sdc = GetDC(NULL);
    HBITMAP color, mask;
    ICONINFO ii;
    HICON ic;
    DWORD *px;
    int x, y;

    ZeroMemory(&bi, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = sz;
    bi.bmiHeader.biHeight = -sz;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    color = CreateDIBSection(sdc, &bi, DIB_RGB_COLORS, &bits, NULL, 0);
    ReleaseDC(NULL, sdc);
    if (!color) return NULL;
    px = (DWORD *)bits;
    for (y = 0; y < sz; y++)
        for (x = 0; x < sz; x++) {
            char c = ARROW[y * 16 / sz][x * 16 / sz];
            px[y * sz + x] = c == '.' ? 0u : c == 'D' ? 0xFFFFFFFFu : 0xFF005FB8u;
        }
    mask = CreateBitmap(sz, sz, 1, 1, NULL);
    ZeroMemory(&ii, sizeof(ii));
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    ic = CreateIconIndirect(&ii);
    DeleteObject(mask);
    DeleteObject(color);
    return ic;
}

/* ---------- tray ---------- */
static void tray_update(void)
{
    WCHAR k[40];
    key_name(g_scan, g_ext, k, 40);
    if (g_enabled) wsprintfW(g_nid.szTip, L"KeyClick: %s = %s", k, action_name());
    else lstrcpyW(g_nid.szTip, L"KeyClick: paused");
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static void show_window(void)
{
    ShowWindow(g_hwnd, SW_SHOW);
    ShowWindow(g_hwnd, SW_RESTORE);
    SetForegroundWindow(g_hwnd);
}

static void tray_menu(void)
{
    HMENU m = CreatePopupMenu();
    WCHAR k[40], line[96], en[96], hk[64];
    POINT pt;
    int cmd;

    key_name(g_scan, g_ext, k, 40);
    wsprintfW(line, L"%s \x2192 %s", k, action_name());
    hk_text(hk, 64);
    wsprintfW(en, L"Enabled\t%s", hk);
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, L"KeyClick");
    AppendMenuW(m, MF_STRING | MF_GRAYED, 0, line);
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING | (g_enabled ? MF_CHECKED : 0), CMD_TOGGLE, en);
    AppendMenuW(m, MF_STRING, CMD_SHOW, L"Settings\x2026");
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, CMD_QUIT, L"Quit");
    GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    cmd = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, NULL);
    PostMessageW(g_hwnd, WM_NULL, 0, 0);
    DestroyMenu(m);
    if (cmd == CMD_TOGGLE) SendMessageW(g_hwnd, WM_COMMAND, MAKEWPARAM(ID_SW_MAIN, BN_CLICKED), 0);
    else if (cmd == CMD_SHOW) show_window();
    else if (cmd == CMD_QUIT) DestroyWindow(g_hwnd);
}

/* ---------- UI state ---------- */
static HWND C(int id) { return GetDlgItem(g_hwnd, id); }

static void update_ui(void)
{
    BOOL ck = g_cap == CAP_KEY, cs = g_cap == CAP_SC;
    int ids[] = { ID_SW_MAIN, ID_BTN_KEY, ID_SEG0, ID_SEG1, ID_SEG2, ID_SW_HOLD, ID_BTN_SC, ID_SW_START };
    int i;

    SetWindowTextW(C(ID_SW_MAIN), g_enabled ? L"KeyClick on" : L"KeyClick off");
    SetWindowTextW(C(ID_SW_HOLD), g_hold ? L"Hold to drag on" : L"Hold to drag off");
    SetWindowTextW(C(ID_SW_START), g_autostart ? L"Start with Windows on" : L"Start with Windows off");
    SetWindowTextW(C(ID_BTN_KEY), ck ? L"Cancel" : L"Change key");
    SetWindowTextW(C(ID_BTN_SC), cs ? L"Cancel" : L"Change");
    SetWindowPos(C(ID_BTN_KEY), NULL, S(ck ? 412 : 388), S(108), S(ck ? 72 : 96), S(36), SWP_NOZORDER);

    EnableWindow(C(ID_SW_MAIN), !ck);
    EnableWindow(C(ID_SEG0), !ck);
    EnableWindow(C(ID_SEG1), !ck);
    EnableWindow(C(ID_SEG2), !ck);
    EnableWindow(C(ID_SW_HOLD), !ck);
    EnableWindow(C(ID_BTN_SC), !ck);
    EnableWindow(C(ID_SW_START), !ck);
    EnableWindow(C(ID_BTN_KEY), !cs);
    if (ck) SetFocus(C(ID_BTN_KEY));

    tray_update();
    for (i = 0; i < 8; i++) InvalidateRect(C(ids[i]), NULL, FALSE);
    InvalidateRect(g_hwnd, NULL, FALSE);
}

static void set_enabled(BOOL on)
{
    g_enabled = on;
    if (!on) { g_down = FALSE; release_if_held(); }
    save_settings();
    update_ui();
}

static BOOL register_hotkey(UINT mods, UINT vk)
{
    return RegisterHotKey(g_hwnd, HK_ID, mods | MOD_NOREPEAT, vk);
}

/* ---------- painting ---------- */
static void cat_rect(RECT *r)
{
    int cell = S(3);
    r->left = (S(CLIENT_W) - CAT_W * cell) / 2;
    r->top = S(474);
    r->right = r->left + CAT_W * cell;
    r->bottom = r->top + CAT_H * cell;
}

static void draw_cat(HDC dc)
{
    RECT r;
    HBRUSH br[8];
    int cell = S(3), x, y, i;
    cat_rect(&r);
    for (i = 1; i < 8; i++) br[i] = CreateSolidBrush((COLORREF)CAT_PAL[i]);
    for (y = 0; y < CAT_H; y++)
        for (x = 0; x < CAT_W; x++) {
            int idx = CAT_PIX[g_frame][y][x];
            if (idx) {
                RECT c = { r.left + x * cell, r.top + y * cell, r.left + (x + 1) * cell, r.top + (y + 1) * cell };
                FillRect(dc, &c, br[idx]);
            }
        }
    for (i = 1; i < 8; i++) DeleteObject(br[i]);
}

static void paint_main(HDC dc)
{
    BOOL dim = (g_cap == CAP_KEY), sc = (g_cap == CAP_SC);
    COLORREF cardBg = dim ? mix(C_WHITE, C_BG, 50) : C_WHITE;
    COLORREF cardLn = dim ? mix(C_CARD, C_BG, 50) : C_CARD;
    COLORREF tx = C_TEXT, sub = C_SUB, txW = C_TEXT, subW = C_SUB;   /* *W: inside the white key card */
    WCHAR kn[48], hk[64];
    void *g;
    int sepH = S(1) < 1 ? 1 : S(1);

    if (dim) { tx = mix(C_TEXT, cardBg, 55); sub = mix(C_SUB, cardBg, 55); txW = mix(C_TEXT, C_WHITE, 55); subW = mix(C_SUB, C_WHITE, 55); }

    box(dc, 0, 0, S(CLIENT_W), S(CLIENT_H), C_BG);
    key_name(g_scan, g_ext, kn, 48);
    hk_text(hk, 64);

    /* shapes */
    g = gp_begin(dc);
    rr(g, (float)S(20), (float)S(4), (float)S(480), (float)S(72), (float)S(8), cardBg, cardLn, 1);
    rr(g, (float)S(20), (float)S(88), (float)S(480), (float)S(214), (float)S(8), C_WHITE, C_CARD, 1);
    rr(g, (float)S(20), (float)S(314), (float)S(480), (float)S(139), (float)S(8), cardBg, cardLn, 1);
    rr(g, (float)S(230), (float)S(179), (float)S(254), (float)S(38), (float)S(6), dim ? mix(C_BG, C_WHITE, 50) : C_BG,
       dim ? mix(RGB(0xD8, 0xD8, 0xD8), C_WHITE, 50) : RGB(0xD8, 0xD8, 0xD8), 1);
    if (g_cap == CAP_KEY) {
        rr(g, (float)S(268), (float)S(102), (float)S(132), (float)S(48), (float)S(8), C_TINT, C_ACC, (float)S(2));
    } else {
        rr(g, (float)S(312), (float)S(104), (float)S(64), (float)S(44), (float)S(8), RGB(0xC6, 0xC6, 0xC6), C_NONE, 0);
        rr(g, (float)S(313), (float)S(105), (float)S(62), (float)S(40), (float)S(7), C_WHITE, C_NONE, 0);
    }
    {   /* shortcut chip */
        SIZE sz;
        HDC mdc = dc;
        int w;
        SelectObject(mdc, f12);
        GetTextExtentPoint32W(mdc, sc ? L"Press shortcut\x2026" : hk, lstrlenW(sc ? L"Press shortcut\x2026" : hk), &sz);
        w = sz.cx + S(16);
        rr(g, (float)(S(400) - w), (float)S(337 - 0), (float)w, (float)S(24), (float)S(4),
           sc ? C_TINT : C_BG, sc ? C_ACC : RGB(0xD8, 0xD8, 0xD8), sc ? 2.0f : 1.0f);
    }
    GdipDeleteGraphics(g);

    /* separators */
    box(dc, S(21), S(163), S(478), sepH, C_SEP);
    box(dc, S(21), S(232), S(478), sepH, C_SEP);
    box(dc, S(21), S(383), S(478), sepH, dim ? mix(C_SEP, C_BG, 50) : C_SEP);

    /* status card */
    T(dc, f16b, tx, 36, 19, 380, 22, g_enabled ? L"KeyClick is on" : L"KeyClick is off", DT_SINGLELINE | DT_VCENTER);
    T(dc, f13, sub, 36, 43, 380, 18, g_enabled ? L"Your key is acting as a mouse button." : L"Paused. Your key types normally.",
      DT_SINGLELINE | DT_VCENTER);

    /* key row */
    if (g_cap == CAP_KEY) {
        T(dc, f14, C_TEXT, 36, 99, 200, 20, L"Key", DT_SINGLELINE | DT_VCENTER);
        T(dc, f12, C_SUB, 36, 121, 220, 32, L"Press the key you want to use.\nEsc cancels.", DT_LEFT);
        T(dc, f14, C_TEXT, 268, 102, 132, 48, L"Press a key\x2026", DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    } else {
        T(dc, f14, C_TEXT, 36, 116, 200, 20, L"Key", DT_SINGLELINE | DT_VCENTER);
        T(dc, lstrlenW(kn) <= 2 ? f18b : f13, C_TEXT, 313, 105, 62, 40, kn, DT_SINGLELINE | DT_VCENTER | DT_CENTER);
    }
    T(dc, f14, txW, 36, 188, 160, 20, L"Acts as", DT_SINGLELINE | DT_VCENTER);
    T(dc, f14, txW, 36, 248, 300, 20, L"Hold to drag", DT_SINGLELINE | DT_VCENTER);
    T(dc, f12, subW, 36, 270, 340, 16, L"Holding the key keeps the button pressed.", DT_SINGLELINE | DT_VCENTER);

    /* general card */
    T(dc, f14, tx, 36, 330, 300, 20, L"Pause shortcut", DT_SINGLELINE | DT_VCENTER);
    T(dc, f12, sub, 36, 352, 300, 16, L"Turn KeyClick on or off from anywhere.", DT_SINGLELINE | DT_VCENTER);
    {
        SIZE sz;
        int w;
        const WCHAR *s = sc ? L"Press shortcut\x2026" : hk;
        SelectObject(dc, f12);
        GetTextExtentPoint32W(dc, s, lstrlenW(s), &sz);
        w = sz.cx + S(16);
        {
            RECT r = { S(400) - w, S(337), S(400), S(361) };
            SetTextColor(dc, dim ? mix(C_TEXT, cardBg, 55) : C_TEXT);
            SetBkMode(dc, TRANSPARENT);
            DrawTextW(dc, s, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX);
        }
    }
    T(dc, f14, tx, 36, 408, 300, 20, L"Start with Windows", DT_SINGLELINE | DT_VCENTER);

    /* footer */
    draw_cat(dc);
    T(dc, f12, C_SUB, 20, 554, 480, 32, L"Changes apply right away.\nClosing this window keeps KeyClick running in the tray.", DT_CENTER);
}

static void draw_item(const DRAWITEMSTRUCT *d)
{
    int id = (int)d->CtlID;
    RECT rc = d->rcItem;
    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    HDC dc = CreateCompatibleDC(d->hDC);
    HBITMAP bm = CreateCompatibleBitmap(d->hDC, w, h);
    HGDIOBJ old = SelectObject(dc, bm);
    BOOL dis = (d->itemState & ODS_DISABLED) != 0;
    BOOL sel = (d->itemState & ODS_SELECTED) != 0;
    BOOL foc = (d->itemState & ODS_FOCUS) && !(d->itemState & ODS_NOFOCUSRECT);
    BOOL hov = g_hover[id - 100];
    BOOL isSeg = id >= ID_SEG0 && id <= ID_SEG2;
    BOOL isSw = id == ID_SW_MAIN || id == ID_SW_HOLD || id == ID_SW_START;
    COLORREF bg = isSeg ? C_BG : C_WHITE;
    WCHAR txt[48];
    COLORREF tc = C_TEXT;
    void *g;

    GetWindowTextW(d->hwndItem, txt, 48);
    box(dc, 0, 0, w, h, bg);
    g = gp_begin(dc);

    if (isSw) {
        BOOL on = id == ID_SW_MAIN ? g_enabled : id == ID_SW_HOLD ? g_hold : g_autostart;
        float kd = (float)S(12), ky = (h - kd) / 2;
        COLORREF acc = dis ? mix(C_ACC, bg, 50) : C_ACC;
        COLORREF off = dis ? mix(RGB(0x6B, 0x6B, 0x6B), bg, 50) : RGB(0x6B, 0x6B, 0x6B);
        if (on) {
            rr(g, 0, 0, (float)w, (float)h, h / 2.0f, acc, acc, 1);
            dot(g, w - (float)S(8) - kd, ky, kd, C_WHITE);
        } else {
            rr(g, 0, 0, (float)w, (float)h, h / 2.0f, C_NONE, off, 1);
            dot(g, (float)S(5), ky, kd, off);
        }
        if (foc) rr(g, 1, 1, (float)w - 2, (float)h - 2, (h - 2) / 2.0f, C_NONE, C_TEXT, 2);
        GdipDeleteGraphics(g);
    } else if (isSeg) {
        BOOL cur = (g_action == id - ID_SEG0);
        if (cur) {
            rr(g, 0, 0, (float)w, (float)h, (float)S(4), dis ? mix(C_ACC, bg, 50) : C_ACC, C_NONE, 0);
            tc = C_WHITE;
        } else {
            if (hov && !dis) rr(g, 0, 0, (float)w, (float)h, (float)S(4), RGB(0xE4, 0xE4, 0xE4), C_NONE, 0);
            tc = dis ? mix(C_TEXT, bg, 55) : C_TEXT;
        }
        if (foc) rr(g, 1, 1, (float)w - 2, (float)h - 2, (float)S(4), C_NONE, cur ? C_WHITE : C_ACC, 2);
        GdipDeleteGraphics(g);
        SelectObject(dc, f13);
        SetTextColor(dc, tc);
        SetBkMode(dc, TRANSPARENT);
        { RECT r = { 0, 0, w, h }; DrawTextW(dc, txt, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX); }
    } else {                                   /* push button */
        COLORREF fill = sel ? RGB(0xEA, 0xEA, 0xEA) : hov ? RGB(0xF6, 0xF6, 0xF6) : C_WHITE;
        rr(g, 0, 0, (float)w, (float)h, (float)S(4), dis ? mix(fill, bg, 50) : fill, RGB(0xD0, 0xD0, 0xD0), 1);
        if (foc) rr(g, 1, 1, (float)w - 2, (float)h - 2, (float)S(4), C_NONE, C_ACC, 2);
        GdipDeleteGraphics(g);
        box(dc, S(4), h - 1, w - S(8), 1, RGB(0xBD, 0xBD, 0xBD));
        SelectObject(dc, f13);
        SetTextColor(dc, dis ? mix(C_TEXT, bg, 55) : C_TEXT);
        SetBkMode(dc, TRANSPARENT);
        { RECT r = { 0, 0, w, h }; DrawTextW(dc, txt, -1, &r, DT_SINGLELINE | DT_VCENTER | DT_CENTER | DT_NOPREFIX); }
    }

    BitBlt(d->hDC, rc.left, rc.top, w, h, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old);
    DeleteObject(bm);
    DeleteDC(dc);
}

/* ---------- controls ---------- */
static LRESULT CALLBACK btn_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    int id = GetDlgCtrlID(h) - 100;
    if (m == WM_MOUSEMOVE && !g_hover[id]) {
        TRACKMOUSEEVENT t;
        t.cbSize = sizeof(t); t.dwFlags = TME_LEAVE; t.hwndTrack = h; t.dwHoverTime = 0;
        g_hover[id] = TRUE;
        TrackMouseEvent(&t);
        InvalidateRect(h, NULL, FALSE);
    } else if (m == WM_MOUSELEAVE) {
        g_hover[id] = FALSE;
        InvalidateRect(h, NULL, FALSE);
    }
    return CallWindowProcW(g_orig[id], h, m, w, l);
}

static void mk(int id, int x, int y, int w, int h)
{
    HWND c = CreateWindowExW(0, L"BUTTON", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                             S(x), S(y), S(w), S(h), g_hwnd, (HMENU)(INT_PTR)id, g_inst, NULL);
    g_orig[id - 100] = (WNDPROC)SetWindowLongPtrW(c, GWLP_WNDPROC, (LONG_PTR)btn_proc);
}

static void set_cap(int mode)
{
    g_cap = (g_cap == mode) ? CAP_NONE : mode;
    update_ui();
}

static void on_command(int id)
{
    switch (id) {
    case ID_SW_MAIN:  set_enabled(!g_enabled); break;
    case ID_SEG0: case ID_SEG1: case ID_SEG2:
        release_if_held();
        g_action = id - ID_SEG0;
        save_settings();
        update_ui();
        break;
    case ID_SW_HOLD:  g_hold = !g_hold; save_settings(); update_ui(); break;
    case ID_SW_START: autostart_set(!g_autostart); update_ui(); break;
    case ID_BTN_KEY:  set_cap(CAP_KEY); break;
    case ID_BTN_SC:   set_cap(CAP_SC); break;
    case IDOK: {
        HWND f = GetFocus();
        if (f && GetParent(f) == g_hwnd) SendMessageW(f, BM_CLICK, 0, 0);
        break;
    }
    }
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == g_wmShow && m) { show_window(); return 0; }
    switch (m) {
    case WM_CREATE: {
        int px[] = { 18, 16, 14, 13, 12 };
        HFONT *fs[] = { &f18b, &f16b, &f14, &f13, &f12 };
        int i;
        g_hwnd = h;
        for (i = 0; i < 5; i++)
            *fs[i] = CreateFontW(-S(px[i]), 0, 0, 0, i < 2 ? FW_SEMIBOLD : FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                                 OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        mk(ID_SW_MAIN, 440, 28, 44, 24);
        mk(ID_BTN_KEY, 388, 108, 96, 36);
        mk(ID_SEG0, 233, 182, 84, 32);
        mk(ID_SEG1, 319, 182, 92, 32);
        mk(ID_SEG2, 413, 182, 68, 32);
        mk(ID_SW_HOLD, 440, 255, 44, 24);
        mk(ID_BTN_SC, 412, 331, 72, 36);
        mk(ID_SW_START, 440, 406, 44, 24);
        SetWindowTextW(C(ID_SEG0), L"Left click");
        SetWindowTextW(C(ID_SEG1), L"Right click");
        SetWindowTextW(C(ID_SEG2), L"Middle");
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(h, &ps);
        RECT cr;
        HDC mdc;
        HBITMAP bm;
        HGDIOBJ old;
        GetClientRect(h, &cr);
        mdc = CreateCompatibleDC(dc);
        bm = CreateCompatibleBitmap(dc, cr.right, cr.bottom);
        old = SelectObject(mdc, bm);
        paint_main(mdc);
        BitBlt(dc, ps.rcPaint.left, ps.rcPaint.top, ps.rcPaint.right - ps.rcPaint.left,
               ps.rcPaint.bottom - ps.rcPaint.top, mdc, ps.rcPaint.left, ps.rcPaint.top, SRCCOPY);
        SelectObject(mdc, old);
        DeleteObject(bm);
        DeleteDC(mdc);
        EndPaint(h, &ps);
        return 0;
    }
    case WM_DRAWITEM:
        draw_item((const DRAWITEMSTRUCT *)l);
        return TRUE;
    case WM_COMMAND:
        if (HIWORD(w) == BN_CLICKED || LOWORD(w) == IDOK) on_command(LOWORD(w));
        return 0;
    case WM_TIMER:
        if (w == TIMER_CAT) {
            RECT r;
            g_frame = (g_frame + 1) % CAT_N;
            SetTimer(h, TIMER_CAT, CAT_MS[g_frame], NULL);
            cat_rect(&r);
            InvalidateRect(h, &r, FALSE);
        }
        return 0;
    case WM_SHOWWINDOW:
        if (w) {
            SetTimer(h, TIMER_CAT, CAT_MS[g_frame], NULL);
        } else {
            KillTimer(h, TIMER_CAT);
            if (g_cap != CAP_NONE) { g_cap = CAP_NONE; update_ui(); }
        }
        return 0;
    case WM_CAPDONE:
        if (w == 2) {                          /* new pause shortcut: keep the old one if it is taken */
            UnregisterHotKey(h, HK_ID);
            if (register_hotkey(g_newmods, g_newvk)) { g_hkmods = g_newmods; g_hkvk = g_newvk; }
            else { MessageBeep(MB_ICONEXCLAMATION); register_hotkey(g_hkmods, g_hkvk); }
        }
        if (w == 1) release_if_held();
        save_settings();
        update_ui();
        return 0;
    case WM_HOTKEY:
        if (w == HK_ID) set_enabled(!g_enabled);
        return 0;
    case WM_TRAY:
        if (l == WM_LBUTTONUP) show_window();
        else if (l == WM_RBUTTONUP) tray_menu();
        return 0;
    case WM_CLOSE:                             /* closing hides to the tray */
        ShowWindow(h, SW_HIDE);
        return 0;
    case WM_DESTROY:
        release_if_held();
        UnregisterHotKey(h, HK_ID);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    WNDCLASSEXW wc;
    GpStartup gsi;
    ULONG_PTR gtok = 0;
    HDC sdc;
    RECT r;
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    int sw, sh;
    MSG msg;
    BOOL trayOnly;
    HICON big, small;
    (void)hp; (void)cmd; (void)show;

    g_inst = hi;
    SetProcessDPIAware();
    g_wmShow = RegisterWindowMessageW(L"KeyClick_ShowSettings_v2");
    CreateMutexW(NULL, TRUE, L"KeyClick_SingleInstance_v2");
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        PostMessageW(HWND_BROADCAST, g_wmShow, 0, 0);
        return 0;
    }

    gsi.Version = 1; gsi.DebugCb = NULL; gsi.NoThread = FALSE; gsi.NoCodecs = FALSE;
    GdiplusStartup(&gtok, &gsi, NULL);
    sdc = GetDC(NULL);
    g_dpi = GetDeviceCaps(sdc, LOGPIXELSX);
    ReleaseDC(NULL, sdc);
    load_settings();
    trayOnly = wcsstr(GetCommandLineW(), L"--tray") != NULL;

    ZeroMemory(&wc, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.lpszClassName = L"KeyClick2Wnd";
    big = make_icon(32);
    small = make_icon(16);
    wc.hIcon = big;
    wc.hIconSm = small;
    RegisterClassExW(&wc);

    r.left = 0; r.top = 0; r.right = S(CLIENT_W); r.bottom = S(CLIENT_H);
    AdjustWindowRect(&r, style, FALSE);
    sw = GetSystemMetrics(SM_CXSCREEN);
    sh = GetSystemMetrics(SM_CYSCREEN);
    g_hwnd = CreateWindowExW(0, wc.lpszClassName, L"KeyClick", style,
                             (sw - (r.right - r.left)) / 2, (sh - (r.bottom - r.top)) / 2,
                             r.right - r.left, r.bottom - r.top, NULL, NULL, hi, NULL);
    if (!g_hwnd) return 1;

    if (!register_hotkey(g_hkmods, g_hkvk)) { g_hkmods = MOD_CONTROL | MOD_ALT; g_hkvk = 'K'; register_hotkey(g_hkmods, g_hkvk); }
    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, hi, 0);
    if (!g_hook) MessageBoxW(NULL, L"Could not install the keyboard hook.", L"KeyClick", MB_ICONERROR);

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = small;
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    update_ui();
    if (!trayOnly) ShowWindow(g_hwnd, SW_SHOW);

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        if (!IsDialogMessageW(g_hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }
    if (g_hook) UnhookWindowsHookEx(g_hook);
    GdiplusShutdown(gtok);
    return 0;
}
