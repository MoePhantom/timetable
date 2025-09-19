#ifndef SYS_UTILS_H
#define SYS_UTILS_H

#include <windows.h>

// 获取窗口 DPI（兼容没有 GetDpiForWindow 的系统）
UINT GetWindowDpi(HWND hwnd);

#endif // SYS_UTILS_H