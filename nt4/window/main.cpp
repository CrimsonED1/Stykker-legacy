// nt4-window: Stykker-Design-Fenster fuer Windows NT 4.0.
// Nur GDI und USER32, kein Common Controls v6 und kein GDI+, damit es auf NT 4 laeuft.
// Farben aus Stykker tokens.css (Dark-Theme).

#include <windows.h>

#define CLR_BG      RGB(6, 9, 22)
#define CLR_CARD    RGB(17, 26, 51)
#define CLR_BORDER  RGB(33, 76, 102)
#define CLR_LINE    RGB(36, 50, 90)
#define CLR_INK     RGB(232, 238, 255)
#define CLR_MUTED   RGB(130, 146, 184)
#define CLR_ACC     RGB(79, 227, 255)
#define CLR_GOOD    RGB(108, 255, 176)
#define CLR_WARN    RGB(255, 200, 87)
#define CLR_BAD     RGB(255, 92, 122)

#define ID_BTN_REFRESH 101
#define ID_BTN_QUIT    102

#define WIN_W 640
#define WIN_H 420

static HFONT g_fontCaps;
static HFONT g_fontTitle;
static HFONT g_fontBody;
static HFONT g_fontSmall;
static char  g_stamp[64] = "Noch nicht aktualisiert";

// Mischt pct Prozent von a mit dem Rest von b.
static COLORREF MixColor(COLORREF a, COLORREF b, int pct)
{
    int r = (GetRValue(a) * pct + GetRValue(b) * (100 - pct)) / 100;
    int g = (GetGValue(a) * pct + GetGValue(b) * (100 - pct)) / 100;
    int bl = (GetBValue(a) * pct + GetBValue(b) * (100 - pct)) / 100;
    return RGB(r, g, bl);
}

static void FillBox(HDC hdc, const RECT* rc, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    FillRect(hdc, rc, br);
    DeleteObject(br);
}

static void RoundBox(HDC hdc, const RECT* rc, COLORREF fill, COLORREF border, int radius)
{
    HBRUSH br = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    RoundRect(hdc, rc->left, rc->top, rc->right, rc->bottom, radius, radius);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

static void HLine(HDC hdc, int x1, int y, int x2, COLORREF c)
{
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ old = SelectObject(hdc, pen);
    MoveToEx(hdc, x1, y, NULL);
    LineTo(hdc, x2, y);
    SelectObject(hdc, old);
    DeleteObject(pen);
}

static void FillCircle(HDC hdc, int cx, int cy, int radius, COLORREF c)
{
    HBRUSH br = CreateSolidBrush(c);
    HPEN pen = CreatePen(PS_SOLID, 1, c);
    HGDIOBJ oldBr = SelectObject(hdc, br);
    HGDIOBJ oldPen = SelectObject(hdc, pen);
    Ellipse(hdc, cx - radius, cy - radius, cx + radius + 1, cy + radius + 1);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(br);
    DeleteObject(pen);
}

// Statuspunkt mit weichem Leuchten: aeusserer Kreis gedaempft, innen die volle Farbe.
static void StatusDot(HDC hdc, int cx, int cy, COLORREF c)
{
    FillCircle(hdc, cx, cy, 9, MixColor(c, CLR_CARD, 28));
    FillCircle(hdc, cx, cy, 5, c);
}

static void PrintAt(HDC hdc, HFONT font, COLORREF color, int x, int y, const char* s)
{
    HGDIOBJ old = SelectObject(hdc, font);
    SetTextColor(hdc, color);
    SetBkMode(hdc, TRANSPARENT);
    TextOutA(hdc, x, y, s, lstrlenA(s));
    SelectObject(hdc, old);
}

static void PaintWindow(HDC hdc, int w, int h)
{
    RECT all = { 0, 0, w, h };
    FillBox(hdc, &all, CLR_BG);

    // Kopfzeile
    PrintAt(hdc, g_fontCaps, CLR_ACC, 28, 22, "S T Y K K E R");
    PrintAt(hdc, g_fontTitle, CLR_INK, 28, 38, "NT 4 Konsole");
    PrintAt(hdc, g_fontSmall, CLR_MUTED, 28, 76, "Win32 | MSVC 14.38 CRT | clang-cl | Windows NT 4.0 SP6");

    // Karte mit vier Statuszeilen
    RECT card = { 24, 102, w - 24, 300 };
    RoundBox(hdc, &card, CLR_CARD, CLR_BORDER, 14);

    static const char* labels[4] = { "Build", "Laufzeit", "Startcode", "Mathe (log10)" };
    static const char* values[4] = { "Release x86, erfolgreich", "MSVC CRT 14.38.33130", "keine SSE-Befehle", "SSE, noch ungetestet" };
    COLORREF colors[4] = { CLR_GOOD, CLR_ACC, CLR_GOOD, CLR_WARN };

    for (int i = 0; i < 4; i++) {
        int y = 124 + i * 40;
        StatusDot(hdc, 50, y + 11, colors[i]);
        PrintAt(hdc, g_fontBody, CLR_INK, 72, y, labels[i]);
        PrintAt(hdc, g_fontBody, CLR_MUTED, 240, y, values[i]);
        if (i < 3) {
            HLine(hdc, 48, y + 30, w - 48, CLR_LINE);
        }
    }

    PrintAt(hdc, g_fontSmall, CLR_MUTED, 28, 310, g_stamp);
}

static void DrawButton(const DRAWITEMSTRUCT* dis)
{
    RECT r = dis->rcItem;
    BOOL pressed = (dis->itemState & ODS_SELECTED) != 0;
    BOOL primary = (dis->CtlID == ID_BTN_REFRESH);

    FillBox(dis->hDC, &r, CLR_BG);

    COLORREF fill;
    COLORREF border;
    COLORREF ink;
    if (primary) {
        fill = pressed ? MixColor(CLR_ACC, CLR_BG, 75) : CLR_ACC;
        border = CLR_ACC;
        ink = CLR_BG;
    } else {
        fill = pressed ? MixColor(CLR_BAD, CLR_CARD, 25) : CLR_CARD;
        border = CLR_BAD;
        ink = CLR_BAD;
    }
    RoundBox(dis->hDC, &r, fill, border, 12);

    const char* label = primary ? "Aktualisieren" : "Beenden";
    HGDIOBJ old = SelectObject(dis->hDC, g_fontBody);
    SetTextColor(dis->hDC, ink);
    SetBkMode(dis->hDC, TRANSPARENT);
    DrawTextA(dis->hDC, label, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    SelectObject(dis->hDC, old);
}

static void UpdateStamp(HWND hwnd)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    wsprintfA(g_stamp, "Zuletzt aktualisiert: %02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);
    InvalidateRect(hwnd, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_CREATE:
        CreateWindowA("BUTTON", "Aktualisieren", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                      28, 336, 200, 44, hwnd, (HMENU)ID_BTN_REFRESH, GetModuleHandleA(NULL), NULL);
        CreateWindowA("BUTTON", "Beenden", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                      240, 336, 130, 44, hwnd, (HMENU)ID_BTN_QUIT, GetModuleHandleA(NULL), NULL);
        return 0;

    case WM_DRAWITEM:
        if (wp == ID_BTN_REFRESH || wp == ID_BTN_QUIT) {
            DrawButton((const DRAWITEMSTRUCT*)lp);
            return TRUE;
        }
        break;

    case WM_COMMAND:
        if (LOWORD(wp) == ID_BTN_REFRESH) {
            UpdateStamp(hwnd);
        } else if (LOWORD(wp) == ID_BTN_QUIT) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_KEYDOWN:
        // Tastatur-Bedienung: R oder Enter aktualisiert, Esc beendet
        if (wp == 'R' || wp == VK_RETURN) {
            UpdateStamp(hwnd);
        } else if (wp == VK_ESCAPE) {
            DestroyWindow(hwnd);
        }
        return 0;

    case WM_ERASEBKGND:
        return 1;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        int w = rc.right - rc.left;
        int h = rc.bottom - rc.top;

        // Doppelpufferung gegen Flackern
        HDC mem = CreateCompatibleDC(hdc);
        HBITMAP bmp = CreateCompatibleBitmap(hdc, w, h);
        HGDIOBJ oldBmp = SelectObject(mem, bmp);
        PaintWindow(mem, w, h);
        BitBlt(hdc, 0, 0, w, h, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp);
        DeleteObject(bmp);
        DeleteDC(mem);

        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static HFONT MakeFont(int height, int weight)
{
    return CreateFontA(height, 0, 0, 0, weight, FALSE, FALSE, FALSE,
                       DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       DEFAULT_QUALITY, DEFAULT_PITCH | FF_SWISS, "Arial");
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmdLine, int nShow)
{
    (void)hPrev;
    (void)cmdLine;

    g_fontCaps  = MakeFont(-11, FW_BOLD);
    g_fontTitle = MakeFont(-28, FW_BOLD);
    g_fontBody  = MakeFont(-15, FW_NORMAL);
    g_fontSmall = MakeFont(-12, FW_NORMAL);

    WNDCLASSA wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursorA(NULL, MAKEINTRESOURCEA(32512)); // IDC_ARROW
    wc.hbrBackground = CreateSolidBrush(CLR_BG);
    wc.lpszClassName = "StykkerNT4Window";
    if (!RegisterClassA(&wc)) {
        return 1;
    }

    RECT rc = { 0, 0, WIN_W, WIN_H };
    AdjustWindowRect(&rc, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE);

    HWND hwnd = CreateWindowA(wc.lpszClassName, "Stykker - NT4 Konsole",
                              WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPCHILDREN,
                              CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left, rc.bottom - rc.top,
                              NULL, NULL, hInst, NULL);
    if (!hwnd) {
        return 1;
    }

    ShowWindow(hwnd, nShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }

    DeleteObject(g_fontCaps);
    DeleteObject(g_fontTitle);
    DeleteObject(g_fontBody);
    DeleteObject(g_fontSmall);
    return (int)msg.wParam;
}
