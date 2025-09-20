#ifndef RENDERER_H
#define RENDERER_H

#include <windows.h>

#define WINDOW_ALPHA     180
#define CORNER_RADIUS    16

// 绘制课程表
void DrawTimetable(HDC hdc, RECT rc, int viewMode);

// 渲染分层窗口
void RenderLayered(HWND hwnd, int viewMode);

// 文本居中绘制函数
void DrawTextCentered(HDC hdc, RECT* rc, WCHAR* text, int yOffset);

// 当前渲染是否存在需要滚动显示的文本
BOOL RendererHasOverflowingText(void);

#endif // RENDERER_H
