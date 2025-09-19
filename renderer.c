#include "renderer.h"
#include "timetable_data.h"
#include "sys_utils.h"
#include <windows.h>
#include <shellapi.h>

extern WCHAR* timetable[DAYS][CLASSES];

// 绘制课程表
void DrawTimetable(HDC hdc, RECT rc, int viewMode) {
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255,255,255));

    LOGFONT lf = {0};
    // 根据当前 DC DPI 缩放字体高度
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    lf.lfHeight = -MulDiv(18, dpi, 96);
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

// 渲染分层窗口
void RenderLayered(HWND hwnd, int viewMode) {
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return;
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    // 获取 DPI，并按 DPI 缩放圆角半径
    UINT dpi = GetWindowDpi(hwnd);
    int corner = max(4, MulDiv(CORNER_RADIUS, dpi, 96)); // 最小值保护

    // 创建 32bpp DIB
    BITMAPINFO bmi = {0};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void *pvBits = NULL;
    HBITMAP hBitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &pvBits, NULL, 0);
    if (!hBitmap || !pvBits) {
        if (hBitmap) DeleteObject(hBitmap);
        return;
    }

    // 背景颜色和 alpha（基准 alpha）
    BYTE bgR = 60, bgG = 60, bgB = 60;
    BYTE baseAlpha = (BYTE)WINDOW_ALPHA;

    // 先把缓冲区设置为不透明的背景预乘色（用 baseAlpha），后续会根据圆角蒙版重新赋值
    UINT pixels = width * height;
    BYTE *ptr = (BYTE*)pvBits;
    BYTE bgRp_base = (BYTE)((bgR * baseAlpha) / 255);
    BYTE bgGp_base = (BYTE)((bgG * baseAlpha) / 255);
    BYTE bgBp_base = (BYTE)((bgB * baseAlpha) / 255);
    for (UINT i = 0; i < pixels; ++i) {
        ptr[0] = bgBp_base; // B
        ptr[1] = bgGp_base; // G
        ptr[2] = bgRp_base; // R
        ptr[3] = baseAlpha; // A
        ptr += 4;
    }

    // 在 DIB 的 DC 上绘制文字（GDI 不会修改 alpha 字节）
    HDC hdcScreen = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdcScreen);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBitmap);

    DrawTimetable(memDC, rc, viewMode);

    // 遍历像素：为圆角计算平滑遮罩；背景像素按遮罩设为预乘色，文本像素保持不透明
    // 使用像素中心 (x+0.5, y+0.5) 计算距离平方并在平方域内线性插值（避免 sqrt）
    ptr = (BYTE*)pvBits;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            BYTE b = ptr[0], g = ptr[1], r = ptr[2];
            float cx = x + 0.5f;
            float cy = y + 0.5f;
            float mask = 1.0f;

            if ((cx < corner && cy < corner) || (cx < corner && cy > height - corner)
             || (cx > width - corner && cy < corner) || (cx > width - corner && cy > height - corner)) {
                float centerX = (cx < corner) ? (corner - 0.5f) : (width - corner - 0.5f);
                float centerY = (cy < corner) ? (corner - 0.5f) : (height - corner - 0.5f);
                float dx = cx - centerX;
                float dy = cy - centerY;
                float dist2 = dx*dx + dy*dy;
                float rFloat = (float)corner;
                float rMinus = rFloat - 0.5f;
                float rPlus = rFloat + 0.5f;
                float rMinus2 = rMinus * rMinus;
                float rPlus2 = rPlus * rPlus;

                if (dist2 <= rMinus2) {
                    mask = 1.0f;
                } else if (dist2 >= rPlus2) {
                    mask = 0.0f;
                } else {
                    // 在平方域内线性插值（避免调用 sqrt）
                    mask = (rPlus2 - dist2) / (rPlus2 - rMinus2);
                    if (mask < 0.0f) mask = 0.0f;
                    if (mask > 1.0f) mask = 1.0f;
                }
            } else {
                mask = 1.0f;
            }

            BYTE maskAlpha = (BYTE)(baseAlpha * mask + 0.5f);

            // 判断是否为背景像素（与基准预乘色比较）
            if (b == bgBp_base && g == bgGp_base && r == bgRp_base) {
                // 背景像素：按 maskAlpha 重新计算预乘颜色与 alpha
                ptr[3] = maskAlpha;
                ptr[2] = (BYTE)((bgR * (int)maskAlpha) / 255); // R
                ptr[1] = (BYTE)((bgG * (int)maskAlpha) / 255); // G
                ptr[0] = (BYTE)((bgB * (int)maskAlpha) / 255); // B
            } else {
                // 非背景（认为是文本或其他绘制），保持不透明
                ptr[3] = 255;
            }

            ptr += 4;
        }
    }

    // 使用 UpdateLayeredWindow 提交
    POINT ptSrc = {0,0};
    SIZE sizeWnd = {width, height};
    POINT ptDst;
    RECT wndRect;
    GetWindowRect(hwnd, &wndRect);
    ptDst.x = wndRect.left;
    ptDst.y = wndRect.top;

    BLENDFUNCTION bf = {0};
    bf.BlendOp = AC_SRC_OVER;
    bf.BlendFlags = 0;
    bf.SourceConstantAlpha = 255;
    bf.AlphaFormat = AC_SRC_ALPHA;

    UpdateLayeredWindow(hwnd, NULL, &ptDst, &sizeWnd, memDC, &ptSrc, 0, &bf, ULW_ALPHA);

    // 清理
    SelectObject(memDC, oldBmp);
    DeleteDC(memDC);
    ReleaseDC(NULL, hdcScreen);
    DeleteObject(hBitmap);
}