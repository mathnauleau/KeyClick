/* keyclick.c - turns the key left of "1" (the § or ` key) into the left mouse button.
 * Tray icon: right-click for "Enabled" toggle and "Quit".
 * Build: zig cc -target x86_64-windows-gnu -O2 -s -mwindows keyclick.c -o keyclick.exe -luser32 -lshell32
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>

#define SCAN_KEY   0x29          /* physical key left of "1", same on every layout */
#define WM_TRAY    (WM_APP + 1)
#define ID_TOGGLE  1
#define ID_QUIT    2

static HHOOK g_hook;
static BOOL g_enabled = TRUE;
static BOOL g_down = FALSE;
static NOTIFYICONDATAW g_nid;

static void send_mouse(DWORD flag)
{
    INPUT in;
    ZeroMemory(&in, sizeof(in));
    in.type = INPUT_MOUSE;
    in.mi.dwFlags = flag;
    SendInput(1, &in, sizeof(in));
}

static void release_if_down(void)
{
    if (g_down) {
        g_down = FALSE;
        send_mouse(MOUSEEVENTF_LEFTUP);
    }
}

static void update_tip(void)
{
    lstrcpyW(g_nid.szTip, g_enabled ? L"KeyClick: key left of 1 = left click"
                                    : L"KeyClick: paused");
    g_nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
}

static LRESULT CALLBACK KbProc(int code, WPARAM wp, LPARAM lp)
{
    if (code == HC_ACTION && g_enabled) {
        KBDLLHOOKSTRUCT *k = (KBDLLHOOKSTRUCT *)lp;
        if (k->scanCode == SCAN_KEY &&
            !(k->flags & LLKHF_EXTENDED) &&
            !(k->flags & LLKHF_INJECTED)) {
            if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) {
                if (!g_down) {               /* ignore key auto-repeat */
                    g_down = TRUE;
                    send_mouse(MOUSEEVENTF_LEFTDOWN);
                }
            } else if (wp == WM_KEYUP || wp == WM_SYSKEYUP) {
                release_if_down();
            }
            return 1;                        /* swallow the key: no § or ` typed */
        }
    }
    return CallNextHookEx(g_hook, code, wp, lp);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM wp, LPARAM lp)
{
    switch (m) {
    case WM_TRAY:
        if (lp == WM_RBUTTONUP || lp == WM_LBUTTONUP) {
            POINT pt;
            HMENU menu = CreatePopupMenu();
            GetCursorPos(&pt);
            AppendMenuW(menu, MF_STRING | (g_enabled ? MF_CHECKED : 0), ID_TOGGLE, L"Enabled");
            AppendMenuW(menu, MF_STRING, ID_QUIT, L"Quit");
            SetForegroundWindow(h);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, h, NULL);
            PostMessageW(h, WM_NULL, 0, 0);
            DestroyMenu(menu);
        }
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == ID_TOGGLE) {
            g_enabled = !g_enabled;
            if (!g_enabled) release_if_down();
            update_tip();
        } else if (LOWORD(wp) == ID_QUIT) {
            DestroyWindow(h);
        }
        return 0;
    case WM_DESTROY:
        release_if_down();
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, wp, lp);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show)
{
    WNDCLASSW wc;
    HWND hwnd;
    MSG msg;
    (void)hp; (void)cmd; (void)show;

    CreateMutexW(NULL, TRUE, L"KeyClick_SingleInstance");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"KeyClickWnd";
    RegisterClassW(&wc);
    hwnd = CreateWindowExW(0, wc.lpszClassName, L"KeyClick", 0, 0, 0, 0, 0,
                           NULL, NULL, hi, NULL);

    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = LoadIconW(NULL, MAKEINTRESOURCEW(32512)); /* IDI_APPLICATION */
    lstrcpyW(g_nid.szTip, L"KeyClick: key left of 1 = left click");
    Shell_NotifyIconW(NIM_ADD, &g_nid);

    g_hook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, hi, 0);
    if (!g_hook) {
        MessageBoxW(NULL, L"Could not install the keyboard hook.", L"KeyClick", MB_ICONERROR);
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        return 1;
    }

    while (GetMessageW(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    UnhookWindowsHookEx(g_hook);
    return 0;
}
