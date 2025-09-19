#define UNICODE
#define _UNICODE
#include <windows.h>
#include <shellapi.h>
#include <tchar.h>

#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT     1002
#define ID_TRAY_SWITCH   1003
#define WM_SYSICON       (WM_USER + 1)
#define SNAP_DIST        20
#define SNAP_MARGIN      10
#define WINDOW_ALPHA     180
#define CORNER_RADIUS    16

#define DAYS 7
#define CLASSES 8

HWND hWnd;
NOTIFYICONDATA nid;
int viewMode = 0; // 0=日视图，1=周视图

// 课程表数据（UTF-16）
WCHAR* timetable[DAYS][CLASSES] = {
    {L"高数", L"英语", L"C语言", L"体育", NULL, NULL, NULL, NULL}, // 周一
    {L"离散", L"英语", L"线代", NULL, NULL, NULL, NULL, NULL},     // 周二
    {L"概率", L"物理", L"C实验", NULL, NULL, NULL, NULL, NULL},   // 周三
    {L"毛概", L"英语", NULL, NULL, NULL, NULL, NULL, NULL},       // 周四
    {L"操作系统", L"编译原理", NULL, NULL, NULL, NULL, NULL, NULL}, // 周五
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},              // 周六
    {NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL},              // 周日
};

// 绘制课程表
void DrawTimetable(HDC hdc, RECT rc) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255,255,255));

    LOGFONT lf = {0};
    lf.lfHeight = -18;
    lstrcpyW(lf.lfFaceName, L"微软雅黑"); // 中文字体
    HFONT hFont = CreateFontIndirect(&lf);
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

    SYSTEMTIME st;
    GetLocalTime(&st);
    int today = (st.wDayOfWeek + 6) % 7; // 周一=0

    if (viewMode == 0) {
        // ==== 日视图 ====
        int cellH = (rc.bottom - rc.top) / CLASSES;
        for (int i=0; i<CLASSES; i++) {
            MoveToEx(hdc, rc.left, rc.top + i*cellH, NULL);
            LineTo(hdc, rc.right, rc.top + i*cellH);

            if (timetable[today][i]) {
                TextOutW(hdc, rc.left+10, rc.top+i*cellH+5,
                         timetable[today][i], lstrlenW(timetable[today][i]));
            }
        }
    } else {
        // ==== 周视图 ====
        int cellH = (rc.bottom - rc.top) / CLASSES;
        int cellW = (rc.right - rc.left) / DAYS;

        // 横线
        for (int i=0; i<=CLASSES; i++) {
            MoveToEx(hdc, rc.left, rc.top+i*cellH, NULL);
            LineTo(hdc, rc.right, rc.top+i*cellH);
        }
        // 竖线
        for (int d=0; d<=DAYS; d++) {
            MoveToEx(hdc, rc.left+d*cellW, rc.top, NULL);
            LineTo(hdc, rc.left+d*cellW, rc.bottom);
        }

        for (int d=0; d<DAYS; d++) {
            for (int i=0; i<CLASSES; i++) {
                if (timetable[d][i]) {
                    TextOutW(hdc,
                        rc.left + d*cellW + 5,
                        rc.top + i*cellH + 5,
                        timetable[d][i],
                        lstrlenW(timetable[d][i]));
                }
            }
        }
    }

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);
}

static void ApplyRoundRegion(HWND hwnd) {
    RECT rc;
    if (GetClientRect(hwnd, &rc)) {
        int width = rc.right - rc.left;
        int height = rc.bottom - rc.top;
        HRGN hrgn = CreateRoundRectRgn(0, 0, width, height,
                                       CORNER_RADIUS, CORNER_RADIUS);
        if (hrgn) {
            if (!SetWindowRgn(hwnd, hrgn, TRUE)) {
                DeleteObject(hrgn);
            }
        }
    }
}

// 窗口过程
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        // 托盘图标
        nid.cbSize = sizeof(NOTIFYICONDATA);
        nid.hWnd = hwnd;
        nid.uID = ID_TRAY_APP_ICON;
        nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        nid.uCallbackMessage = WM_SYSICON;
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
        lstrcpyW(nid.szTip, L"课程表小组件");
        Shell_NotifyIcon(NIM_ADD, &nid);

        SetTimer(hwnd, 1, 60000, NULL); // 每分钟刷新
        ApplyRoundRegion(hwnd);
        break;
    }
    case WM_TIMER:
        InvalidateRect(hwnd, NULL, TRUE);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);
        HBRUSH hBrush = CreateSolidBrush(RGB(60, 60, 60));
        if (hBrush) {
            FillRect(hdc, &rc, hBrush);
            DeleteObject(hBrush);
        }
        DrawTimetable(hdc, rc);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_SIZE:
        ApplyRoundRegion(hwnd);
        break;
    case WM_LBUTTONDOWN: // 拖动窗口
        ReleaseCapture();
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        break;

    case WM_EXITSIZEMOVE: { // 拖动结束吸附
        RECT rc;
        GetWindowRect(hwnd, &rc);
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;
        int newX = rc.left, newY = rc.top;
        if (abs(rc.left - 0) < SNAP_DIST) {
            newX = SNAP_MARGIN;
        }
        if (abs(screenW - rc.right) < SNAP_DIST) {
            int snapX = screenW - winW - SNAP_MARGIN;
            if (snapX < 0) snapX = 0;
            newX = snapX;
        }
        if (abs(rc.top - 0) < SNAP_DIST) {
            newY = SNAP_MARGIN;
        }
        if (abs(screenH - rc.bottom) < SNAP_DIST) {
            int snapY = screenH - winH - SNAP_MARGIN;
            if (snapY < 0) snapY = 0;
            newY = snapY;
        }
        SetWindowPos(hwnd, NULL, newX, newY, winW, winH,
                     SWP_NOZORDER|SWP_NOACTIVATE);
        break;
    }

    case WM_SETCURSOR: // 保证光标正常
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return TRUE;

    case WM_SYSICON: {
        if (lParam == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_SWITCH,
                       viewMode==0 ? L"切换到周视图" : L"切换到日视图");
            AppendMenu(hMenu, MF_STRING, ID_TRAY_EXIT, L"退出");
            POINT pt;
            GetCursorPos(&pt);
            SetForegroundWindow(hwnd);
            TrackPopupMenu(hMenu, TPM_BOTTOMALIGN|TPM_LEFTALIGN,
                           pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
        }
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == ID_TRAY_EXIT) {
            Shell_NotifyIcon(NIM_DELETE, &nid);
            PostQuitMessage(0);
        } else if (LOWORD(wParam) == ID_TRAY_SWITCH) {
            viewMode = 1 - viewMode; // 切换模式
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        Shell_NotifyIcon(NIM_DELETE, &nid);
        PostQuitMessage(0);
        break;

    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// 程序入口
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine, int nCmdShow) {
    const WCHAR cls[] = L"TimetableWidget";
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); // 设置默认箭头光标
    RegisterClassW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int winW = 400, winH = 300;
    int x = screenW - winW - 10, y = 10;

    hWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, cls, L"课程表",
                          WS_POPUP, x, y, winW, winH,
                          NULL, NULL, hInstance, NULL);

    if (hWnd) {
        SetLayeredWindowAttributes(hWnd, 0, WINDOW_ALPHA, LWA_ALPHA);
    }

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    MSG msg;
    while (GetMessage(&msg,NULL,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}
