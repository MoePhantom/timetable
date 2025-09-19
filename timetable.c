#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <tchar.h>
#include "timetable_data.h"
#include "sys_utils.h"
#include "renderer.h"

#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT     1002
#define ID_TRAY_SWITCH   1003
#define WM_SYSICON       (WM_USER + 1)
#define SNAP_DIST        20
#define SNAP_MARGIN      10

HWND hWnd;
NOTIFYICONDATA nid;
int viewMode = 0; // 0=日视图，1=周视图

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
        // ApplyRoundRegion(hwnd); // 已空实现，可不调用
        // 首次渲染
        RenderLayered(hwnd, viewMode);
        break;
    }
    case WM_TIMER:
        // 直接重新渲染分层窗口
        RenderLayered(hwnd, viewMode);
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        // 使用 UpdateLayeredWindow 绘制内容
        RenderLayered(hwnd, viewMode);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_SIZE:
        //ApplyRoundRegion(hwnd); // 已空实现
        // 重新渲染尺寸变化后的图像
        RenderLayered(hwnd, viewMode);
        break;
    case WM_LBUTTONDOWN: // 拖动窗口
        ReleaseCapture();
        SendMessage(hwnd, WM_NCLBUTTONDOWN, HTCAPTION, 0);
        break;

    case WM_EXITSIZEMOVE: { // 拖动结束吸附（按窗口 DPI 缩放 SNAP 参数）
        RECT rc;
        GetWindowRect(hwnd, &rc);
        int screenW = GetSystemMetrics(SM_CXSCREEN);
        int screenH = GetSystemMetrics(SM_CYSCREEN);
        int winW = rc.right - rc.left;
        int winH = rc.bottom - rc.top;
        int newX = rc.left, newY = rc.top;

        // 按窗口 DPI 缩放距离和边距
        UINT dpi = GetWindowDpi(hwnd);
        int snapDist = max(1, MulDiv(SNAP_DIST, dpi, 96));
        int snapMargin = max(1, MulDiv(SNAP_MARGIN, dpi, 96));

        if (abs(rc.left - 0) < snapDist) {
            newX = snapMargin;
        }
        if (abs(screenW - rc.right) < snapDist) {
            int snapX = screenW - winW - snapMargin;
            if (snapX < 0) snapX = 0;
            newX = snapX;
        }
        if (abs(rc.top - 0) < snapDist) {
            newY = snapMargin;
        }
        if (abs(screenH - rc.bottom) < snapDist) {
            int snapY = screenH - winH - snapMargin;
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
            // 切换模式后直接重绘分层窗口
            RenderLayered(hwnd, viewMode);
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
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance,
                   PWSTR lpCmdLine, int nCmdShow) {
    // 建议让进程 DPI aware，以便按正确 DPI 创建初始窗口尺寸
    SetProcessDPIAware();

    const WCHAR cls[] = L"TimetableWidget";
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.hbrBackground = NULL;
    wc.lpszClassName = cls;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW); // 设置默认箭头光标
    RegisterClassW(&wc);

    int screenW = GetSystemMetrics(SM_CXSCREEN);

    // 获取系统 DPI 并按 DPI 缩放初始窗口尺寸与边距
    HDC dc = GetDC(NULL);
    int sysDpi = GetDeviceCaps(dc, LOGPIXELSX);
    ReleaseDC(NULL, dc);
    int winW = MulDiv(400, sysDpi, 96);
    int winH = MulDiv(300, sysDpi, 96);
    int x = screenW - winW - MulDiv(10, sysDpi, 96);
    int y = MulDiv(10, sysDpi, 96);

    hWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, cls, L"课程表",
                          WS_POPUP, x, y, winW, winH,
                          NULL, NULL, hInstance, NULL);

    // 不再使用 SetLayeredWindowAttributes；改为使用 UpdateLayeredWindow 在 RenderLayered 中控制像素 alpha

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 首次确保渲染（如果 WM_CREATE 未触发）
    if (hWnd) RenderLayered(hWnd, viewMode);

    MSG msg;
    while (GetMessage(&msg,NULL,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}
