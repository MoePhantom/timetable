#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <shellapi.h>
#include <tchar.h>
#include <math.h>
#include <stdlib.h>
#include <mmsystem.h>
#include "timetable_data.h"
#include "sys_utils.h"
#include "renderer.h"

#define ID_TRAY_APP_ICON 1001
#define ID_TRAY_EXIT     1002
#define ID_TRAY_SWITCH   1003
#define ID_TRAY_BOTTOM   1004
#define ID_TRAY_BLUR     1005
#define WM_SYSICON       (WM_USER + 1)
#define SNAP_DIST        20
#define SNAP_MARGIN      10

HWND hWnd;
NOTIFYICONDATA nid;
int viewMode = 1; // 0=日视图，1=周视图（默认为周视图）

// 缓动动画相关变量
BOOL isAnimating = FALSE;
RECT startRect, targetRect;
static const double ANIMATION_DURATION_MS = 300.0; // 动画持续时间
static const double FRAME_INTERVAL_MS = 1000.0 / 60.0;

static BOOL precisionTimerActive = FALSE;
static double animationStartMs = 0.0;
static double lastAnimationFrameMs = 0.0;
static double lastScrollFrameMs = 0.0;
static ULONGLONG lastMinuteSlot = 0;

static LARGE_INTEGER perfFreq = {0};
static LARGE_INTEGER perfBase = {0};

// 原始窗口大小（周视图大小）
int originalWidth, originalHeight;

typedef enum {
    SNAP_EDGE_NONE = 0,
    SNAP_EDGE_LEFT,
    SNAP_EDGE_RIGHT
} SnapEdge;

static SnapEdge currentSnapEdge = SNAP_EDGE_RIGHT;

static BOOL scrollTimerActive = FALSE;

static BOOL keepOnBottom = TRUE;

static void EnsureBottomOrder(HWND hwnd) {
    if (!keepOnBottom) {
        return;
    }
    SetWindowPos(hwnd, HWND_BOTTOM, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOREDRAW | SWP_NOSENDCHANGING);
}


static double GetClockMs(void) {
    if (perfFreq.QuadPart == 0) {
        if (!QueryPerformanceFrequency(&perfFreq)) {
            return (double)GetTickCount64();
        }
        QueryPerformanceCounter(&perfBase);
    }

    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    return (double)(now.QuadPart - perfBase.QuadPart) * 1000.0 / (double)perfFreq.QuadPart;
}

static SnapEdge DetectSnapEdge(const RECT *rc, int screenW, int snapMargin, int snapDist) {
    if (!rc) return SNAP_EDGE_NONE;
    int left = (int)rc->left;
    int right = (int)rc->right;
    if (abs(left - snapMargin) <= snapDist) {
        return SNAP_EDGE_LEFT;
    }
    if (abs((screenW - snapMargin) - right) <= snapDist) {
        return SNAP_EDGE_RIGHT;
    }
    return SNAP_EDGE_NONE;
}


static void UpdateScrollTimer(HWND hwnd) {
    BOOL needScroll = RendererHasOverflowingText();
    if (needScroll && !scrollTimerActive) {
        SetTimer(hwnd, 2, 40, NULL);
        scrollTimerActive = TRUE;
        lastScrollFrameMs = GetClockMs();
    } else if (!needScroll && scrollTimerActive) {
        KillTimer(hwnd, 2);
        scrollTimerActive = FALSE;
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

        if (!precisionTimerActive) {
            if (timeBeginPeriod(1) == TIMERR_NOERROR) {
                precisionTimerActive = TRUE;
            }
        }

        SetTimer(hwnd, 1, 16, NULL); // 约 60 FPS 的主动画定时器
        // ApplyRoundRegion(hwnd); // 已空实现，可不调用
        // 首次渲染
        RenderLayered(hwnd, viewMode);

        UpdateScrollTimer(hwnd);

        lastMinuteSlot = GetTickCount64() / 60000ULL;
        lastAnimationFrameMs = GetClockMs();
        lastScrollFrameMs = lastAnimationFrameMs;


        RECT initRect;
        if (GetWindowRect(hwnd, &initRect)) {
            UINT dpi = GetWindowDpi(hwnd);
            int snapDist = max(1, MulDiv(SNAP_DIST, dpi, 96));
            int snapMargin = max(1, MulDiv(SNAP_MARGIN, dpi, 96));
            int screenW = GetSystemMetrics(SM_CXSCREEN);
            currentSnapEdge = DetectSnapEdge(&initRect, screenW, snapMargin, snapDist);
        }

        EnsureBottomOrder(hwnd);
        break;
    }
    case WM_TIMER:
        if (wParam == 1) { // 主定时器
            double nowMs = GetClockMs();
            BOOL shouldRender = FALSE;

            if (isAnimating) {
                double elapsed = nowMs - animationStartMs;
                if (elapsed >= ANIMATION_DURATION_MS) {
                    SetWindowPos(hwnd, NULL, targetRect.left, targetRect.top,
                                 targetRect.right - targetRect.left,
                                 targetRect.bottom - targetRect.top,
                                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            EnsureBottomOrder(hwnd);
                    isAnimating = FALSE;
                    UINT dpi = GetWindowDpi(hwnd);
                    int snapDist = max(1, MulDiv(SNAP_DIST, dpi, 96));
                    int snapMargin = max(1, MulDiv(SNAP_MARGIN, dpi, 96));
                    int screenW = GetSystemMetrics(SM_CXSCREEN);
                    currentSnapEdge = DetectSnapEdge(&targetRect, screenW, snapMargin, snapDist);
                    lastAnimationFrameMs = nowMs;
                    shouldRender = TRUE;
                } else if (nowMs - lastAnimationFrameMs >= FRAME_INTERVAL_MS) {
                    double progress = elapsed / ANIMATION_DURATION_MS;
                    progress = 1.0 - pow(1.0 - progress, 3.0);

                    int startWidth = startRect.right - startRect.left;
                    int startHeight = startRect.bottom - startRect.top;
                    int targetWidth = targetRect.right - targetRect.left;
                    int targetHeight = targetRect.bottom - targetRect.top;

                    int currentX = startRect.left + (int)((targetRect.left - startRect.left) * progress);
                    int currentY = startRect.top + (int)((targetRect.top - startRect.top) * progress);
                    int currentWidth = startWidth + (int)((targetWidth - startWidth) * progress);
                    int currentHeight = startHeight + (int)((targetHeight - startHeight) * progress);

                    SetWindowPos(hwnd, NULL, currentX, currentY, currentWidth, currentHeight,
                                 SWP_NOZORDER | SWP_NOACTIVATE | SWP_NOREDRAW);
            EnsureBottomOrder(hwnd);
                    lastAnimationFrameMs = nowMs;
                    shouldRender = TRUE;
                }
            } else {
                ULONGLONG currentSlot = GetTickCount64() / 60000ULL;
                if (currentSlot != lastMinuteSlot) {
                    lastMinuteSlot = currentSlot;
                    shouldRender = TRUE;
                }
            }

            if (shouldRender) {
                RenderLayered(hwnd, viewMode);
                UpdateScrollTimer(hwnd);
                lastScrollFrameMs = nowMs;
            }
        } else if (wParam == 2) {
            if (!isAnimating) {
                double nowMs = GetClockMs();
                if (nowMs - lastScrollFrameMs >= FRAME_INTERVAL_MS) {
                    lastScrollFrameMs = nowMs;
                    RenderLayered(hwnd, viewMode);
                    UpdateScrollTimer(hwnd);
                }
            }
        }
        break;

    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(hwnd, &ps);
        // 使用 UpdateLayeredWindow 绘制内容
        RenderLayered(hwnd, viewMode);
        UpdateScrollTimer(hwnd);
        EndPaint(hwnd, &ps);
        break;
    }
    case WM_SIZE:
        //ApplyRoundRegion(hwnd); // 已空实现
        // 重新渲染尺寸变化后的图像
        RenderLayered(hwnd, viewMode);
        UpdateScrollTimer(hwnd);
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

        // 保存原始窗口大小（周视图大小）
        if (viewMode == 1) { // 周视图
            originalWidth = winW;
            originalHeight = winH;
        }

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
        EnsureBottomOrder(hwnd);
        RECT newRect;
        GetWindowRect(hwnd, &newRect);
        currentSnapEdge = DetectSnapEdge(&newRect, screenW, snapMargin, snapDist);
        break;
    }

    case WM_SETCURSOR: // 保证光标正常
        SetCursor(LoadCursor(NULL, IDC_ARROW));
        return TRUE;

    case WM_WINDOWPOSCHANGING: {
        if (keepOnBottom) {
            WINDOWPOS *pos = (WINDOWPOS*)lParam;
            if (pos && !(pos->flags & SWP_NOZORDER)) {
                pos->hwndInsertAfter = HWND_BOTTOM;
            }
        }
        break;
    }

    case WM_SYSICON: {
        if (lParam == WM_RBUTTONUP) {
            HMENU hMenu = CreatePopupMenu();
            AppendMenu(hMenu, MF_STRING, ID_TRAY_SWITCH,
                       viewMode==0 ? L"切换到周视图" : L"切换到日视图");
            AppendMenu(hMenu, MF_STRING | (keepOnBottom ? MF_CHECKED : MF_UNCHECKED),
                       ID_TRAY_BOTTOM, L"窗口总在底层");
            AppendMenu(hMenu, MF_STRING | (RendererIsBlurEnabled() ? MF_CHECKED : MF_UNCHECKED),
                       ID_TRAY_BLUR, L"背景模糊");
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
        } else if (LOWORD(wParam) == ID_TRAY_BOTTOM) {
            keepOnBottom = !keepOnBottom;
            if (keepOnBottom) {
                EnsureBottomOrder(hwnd);
            }
        } else if (LOWORD(wParam) == ID_TRAY_BLUR) {
            BOOL newState = !RendererIsBlurEnabled();
            RendererSetBlurEnabled(newState);
            RenderLayered(hwnd, viewMode);
        } else if (LOWORD(wParam) == ID_TRAY_SWITCH) {
            viewMode = 1 - viewMode; // 切换模式

            // 获取当前窗口位置和大小
            RECT currentRect;
            GetWindowRect(hwnd, &currentRect);

            // 计算目标位置和大小
            targetRect = currentRect;

            UINT dpi = GetWindowDpi(hwnd);
            int snapDist = max(1, MulDiv(SNAP_DIST, dpi, 96));
            int snapMargin = max(1, MulDiv(SNAP_MARGIN, dpi, 96));
            int screenW = GetSystemMetrics(SM_CXSCREEN);

            SnapEdge snapEdge = DetectSnapEdge(&currentRect, screenW, snapMargin, snapDist);
            if (snapEdge != SNAP_EDGE_NONE) {
                currentSnapEdge = snapEdge;
            }

            if (viewMode == 0) { // 切换到日视图 - 宽度缩小到1/3，高度不变
                int newWidth = max(1, originalWidth / 3);
                targetRect.bottom = targetRect.top + originalHeight;

                if (currentSnapEdge == SNAP_EDGE_LEFT) {
                    targetRect.left = currentRect.left;
                    targetRect.right = targetRect.left + newWidth;
                } else if (currentSnapEdge == SNAP_EDGE_RIGHT) {
                    targetRect.right = currentRect.right;
                    targetRect.left = targetRect.right - newWidth;
                } else {
                    targetRect.left = currentRect.left;
                    targetRect.right = targetRect.left + newWidth;
                    if (targetRect.right > screenW - snapMargin) {
                        targetRect.right = screenW - snapMargin;
                        targetRect.left = targetRect.right - newWidth;
                    }
                    if (targetRect.left < snapMargin) {
                        targetRect.left = snapMargin;
                        targetRect.right = targetRect.left + newWidth;
                    }
                }
            } else { // 切换到周视图 - 恢复原始大小并向外扩展
                targetRect.bottom = targetRect.top + originalHeight;
                if (currentSnapEdge == SNAP_EDGE_LEFT) {
                    targetRect.left = currentRect.left;
                    targetRect.right = targetRect.left + originalWidth;
                } else if (currentSnapEdge == SNAP_EDGE_RIGHT) {
                    targetRect.right = currentRect.right;
                    targetRect.left = targetRect.right - originalWidth;
                } else {
                    targetRect.left = currentRect.left;
                    targetRect.right = targetRect.left + originalWidth;
                    if (targetRect.right > screenW - snapMargin) {
                        targetRect.right = screenW - snapMargin;
                        targetRect.left = targetRect.right - originalWidth;
                    }
                    if (targetRect.left < snapMargin) {
                        targetRect.left = snapMargin;
                        targetRect.right = targetRect.left + originalWidth;
                    }
                }
            }

            SnapEdge newEdge = DetectSnapEdge(&targetRect, screenW, snapMargin, snapDist);
            if (newEdge != SNAP_EDGE_NONE) {
                currentSnapEdge = newEdge;
            }

            // 启动动画
            startRect = currentRect;
            animationStartMs = GetClockMs();
            lastAnimationFrameMs = animationStartMs;
            lastScrollFrameMs = animationStartMs;
            isAnimating = TRUE;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, 1);
        if (scrollTimerActive) {
            KillTimer(hwnd, 2);
            scrollTimerActive = FALSE;
        }

        if (precisionTimerActive) {
            timeEndPeriod(1);
            precisionTimerActive = FALSE;
        }

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
    
    // 保存原始窗口大小
    originalWidth = winW;
    originalHeight = winH;

    hWnd = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_LAYERED, cls, L"课程表",
                          WS_POPUP, x, y, winW, winH,
                          NULL, NULL, hInstance, NULL);

    // 不再使用 SetLayeredWindowAttributes；改为使用 UpdateLayeredWindow 在 RenderLayered 中控制像素 alpha

    ShowWindow(hWnd, nCmdShow);
    UpdateWindow(hWnd);

    // 首次确保渲染（如果 WM_CREATE 未触发）
    if (hWnd) {
        RenderLayered(hWnd, viewMode);
        UpdateScrollTimer(hWnd);
    }

    MSG msg;
    while (GetMessage(&msg,NULL,0,0)>0) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return msg.wParam;
}
