#ifndef RENDERER_H
#define RENDERER_H

#include <windows.h>

#define WINDOW_ALPHA     180
#define CORNER_RADIUS    16

// 绘制课程表
void DrawTimetable(HDC hdc, RECT rc, int viewMode);

// 渲染分层窗口
void RenderLayered(HWND hwnd, int viewMode);

#endif // RENDERER_H