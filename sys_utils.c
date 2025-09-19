#include "sys_utils.h"
#include <windows.h>

// 获取窗口 DPI（兼容没有 GetDpiForWindow 的系统）
UINT GetWindowDpi(HWND hwnd) {
    typedef UINT (WINAPI *GetDpiForWindow_t)(HWND);
    HMODULE hUser32 = GetModuleHandleW(L"user32.dll");
    GetDpiForWindow_t pGetDpiForWindow = NULL;
    if (hUser32) pGetDpiForWindow = (GetDpiForWindow_t)GetProcAddress(hUser32, "GetDpiForWindow");
    if (pGetDpiForWindow) {
        return pGetDpiForWindow(hwnd);
    } else {
        HDC dc = GetDC(NULL);
        int dpi = GetDeviceCaps(dc, LOGPIXELSX);
        ReleaseDC(NULL, dc);
        return (UINT)dpi;
    }
}