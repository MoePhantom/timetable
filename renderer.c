#include "renderer.h"
#include "timetable_data.h"
#include "sys_utils.h"
#include <windows.h>
#include <shellapi.h>
#include <math.h>

extern ClassInfo timetable[DAYS][CLASSES];

static BOOL g_currentFrameHasOverflow = FALSE;
static BOOL g_lastFrameHasOverflow = FALSE;

typedef struct {
    HBITMAP bitmap;
    void *bits;
    int width;
    int height;
} LayerSurface;

static LayerSurface g_layerSurface = {0};
static HDC g_layerDC = NULL;

static BOOL EnsureLayerSurface(int width, int height) {
    if (width <= 0 || height <= 0) return FALSE;

    if (!g_layerDC) {
        g_layerDC = CreateCompatibleDC(NULL);
        if (!g_layerDC) {
            return FALSE;
        }
    }

    if (g_layerSurface.bitmap &&
        (g_layerSurface.width != width || g_layerSurface.height != height)) {
        DeleteObject(g_layerSurface.bitmap);
        g_layerSurface.bitmap = NULL;
        g_layerSurface.bits = NULL;
        g_layerSurface.width = 0;
        g_layerSurface.height = 0;
    }

    if (!g_layerSurface.bitmap) {
        BITMAPINFO bmi = {0};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = width;
        bmi.bmiHeader.biHeight = -height; // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        void *bits = NULL;
        HBITMAP bitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (!bitmap || !bits) {
            if (bitmap) {
                DeleteObject(bitmap);
            }
            return FALSE;
        }

        g_layerSurface.bitmap = bitmap;
        g_layerSurface.bits = bits;
        g_layerSurface.width = width;
        g_layerSurface.height = height;
    }

    return TRUE;
}

static void UpdateOverflowFlag(BOOL overflowed) {
    if (overflowed) {
        g_currentFrameHasOverflow = TRUE;
    }
}

static double ClampDouble(double value, double minValue, double maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static BOOL DayHasAnyClass(int dayIndex) {
    if (dayIndex < 0 || dayIndex >= DAYS) {
        return FALSE;
    }

    for (int i = 0; i < CLASSES; ++i) {
        if (timetable[dayIndex][i].name) {
            return TRUE;
        }
    }
    return FALSE;
}

static void DrawHolidayText(HDC hdc, const RECT *rc) {
    if (!hdc || !rc) return;

    const WCHAR text[] = L"放假";
    const int len = 2;
    const int spacing = 6;

    SIZE charSize = {0};
    if (!GetTextExtentPoint32W(hdc, text, 1, &charSize)) {
        return;
    }

    int cellWidth = rc->right - rc->left;
    int cellHeight = rc->bottom - rc->top;
    if (cellWidth <= 0 || cellHeight <= 0) return;

    int totalHeight = len * charSize.cy + (len - 1) * spacing;
    int startY = rc->top + (cellHeight - totalHeight) / 2;
    int x = rc->left + (cellWidth - charSize.cx) / 2;

    COLORREF originalColor = GetTextColor(hdc);
    SetTextColor(hdc, RGB(255, 215, 0));

    for (int i = 0; i < len; ++i) {
        WCHAR ch[2] = {text[i], 0};
        int y = startY + i * (charSize.cy + spacing);
        TextOutW(hdc, x, y, ch, 1);
    }

    SetTextColor(hdc, originalColor);
    UpdateOverflowFlag(FALSE);
}

static BOOL DrawTextInternal(HDC hdc, const RECT *rc, const WCHAR *text, int yOffset) {
    if (!hdc || !rc || !text) return FALSE;

    int len = lstrlenW(text);
    if (len <= 0) return FALSE;

    SIZE textSize;
    if (!GetTextExtentPoint32W(hdc, text, len, &textSize)) return FALSE;

    int cellWidth = rc->right - rc->left;
    if (cellWidth <= 0) return FALSE;

    int y = rc->top + yOffset;

    if (textSize.cx <= cellWidth) {
        int x = rc->left + (cellWidth - textSize.cx) / 2;
        TextOutW(hdc, x, y, text, len);
        return FALSE;
    }

    int saved = SaveDC(hdc);
    if (saved > 0) {
        IntersectClipRect(hdc, rc->left, y, rc->right, y + textSize.cy);
    }

    const double pixelsPerSecond = 40.0;
    const double pauseDurationMs = 1000.0;
    const double pixelsPerMs = (pixelsPerSecond > 0.0) ? (pixelsPerSecond / 1000.0) : 0.04;

    double alignLeft = (double)rc->left;
    double alignRight = (double)(rc->right - textSize.cx);
    double travelPixels = alignLeft - alignRight;
    if (travelPixels < 0.0) travelPixels = 0.0;

    double travelMs = travelPixels / pixelsPerMs;
    if (travelMs < 1.0) travelMs = 1.0;

    double cycleMs = 2.0 * (travelMs + pauseDurationMs);

    static ULONGLONG scrollEpoch = 0;
    ULONGLONG tick = GetTickCount64();
    if (scrollEpoch == 0) {
        scrollEpoch = tick;
    }

    double elapsedMs = (double)(tick - scrollEpoch);
    double phase = fmod(elapsedMs, cycleMs);
    double currentX;

    if (phase < travelMs) {
        currentX = alignLeft - phase * pixelsPerMs;
    } else if (phase < travelMs + pauseDurationMs) {
        currentX = alignRight;
    } else if (phase < travelMs + pauseDurationMs + travelMs) {
        double t = phase - (travelMs + pauseDurationMs);
        currentX = alignRight + t * pixelsPerMs;
    } else {
        currentX = alignLeft;
    }

    currentX = ClampDouble(currentX, alignRight, alignLeft);

    int drawX = (int)floor(currentX + 0.5);
    TextOutW(hdc, drawX, y, text, len);

    if (saved > 0) {
        RestoreDC(hdc, saved);
    }

    return TRUE;
}

// 文本居中绘制函数
void DrawTextCentered(HDC hdc, RECT* rc, WCHAR* text, int yOffset) {
    BOOL overflowed = DrawTextInternal(hdc, rc, text, yOffset);
    UpdateOverflowFlag(overflowed);
}

BOOL RendererHasOverflowingText(void) {
    return g_lastFrameHasOverflow;
}

// 绘制课程表
void DrawTimetable(HDC hdc, RECT rc, int viewMode) {
    g_currentFrameHasOverflow = FALSE;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(255,255,255));

    LOGFONT lf = {0};
    // 根据当前 DC DPI 缩放字体高度
    int dpi = GetDeviceCaps(hdc, LOGPIXELSX);
    lf.lfHeight = -MulDiv(12, dpi, 96);
    lstrcpyW(lf.lfFaceName, L"微软雅黑"); // 中文字体
    HFONT hFont = CreateFontIndirect(&lf);
    HFONT oldFont = (HFONT)SelectObject(hdc, hFont);

    SYSTEMTIME st;
    GetLocalTime(&st);
    int today = (st.wDayOfWeek + 6) % 7; // 周一=0

    if (viewMode == 0) {
        // ==== 日视图 ====
        int cellH = (rc.bottom - rc.top) / CLASSES;
        BOOL hasAnyClass = DayHasAnyClass(today);

        if (!hasAnyClass) {
            DrawHolidayText(hdc, &rc);
        } else {
            for (int i=0; i<CLASSES; i++) {
                RECT cellRect = {rc.left, rc.top + i*cellH, rc.right, rc.top + (i+1)*cellH};

                if (timetable[today][i].name) {
                    // 绘制课程名称（居中显示）
                    DrawTextCentered(hdc, &cellRect, timetable[today][i].name, 10);

                    // 绘制位置信息（居中显示）
                    if (timetable[today][i].location) {
                        SetTextColor(hdc, RGB(200, 200, 200)); // 稍微淡一点的颜色
                        DrawTextCentered(hdc, &cellRect, timetable[today][i].location, 35);
                        SetTextColor(hdc, RGB(255,255,255)); // 恢复白色
                    }
                }
            }
        }
    } else {
        // ==== 周视图 ====
        int cellH = (rc.bottom - rc.top) / CLASSES;
        int cellW = (rc.right - rc.left) / DAYS;

        for (int d=0; d<DAYS; d++) {
            RECT columnRect = {rc.left + d*cellW, rc.top, rc.left + (d+1)*cellW, rc.bottom};
            if (!DayHasAnyClass(d)) {
                DrawHolidayText(hdc, &columnRect);
                continue;
            }

            for (int i=0; i<CLASSES; i++) {
                RECT cellRect = {columnRect.left, rc.top + i*cellH, columnRect.right, rc.top + (i+1)*cellH};

                if (timetable[d][i].name) {
                    // 绘制课程名称（居中显示）
                    DrawTextCentered(hdc, &cellRect, timetable[d][i].name, 10);

                    // 绘制位置信息（居中显示）
                    if (timetable[d][i].location) {
                        SetTextColor(hdc, RGB(200, 200, 200)); // 稍微淡一点的颜色
                        DrawTextCentered(hdc, &cellRect, timetable[d][i].location, 35);
                        SetTextColor(hdc, RGB(255,255,255)); // 恢复白色
                    }
                }
            }
        }
    }

    SelectObject(hdc, oldFont);
    DeleteObject(hFont);

    g_lastFrameHasOverflow = g_currentFrameHasOverflow;
}

// 渲染分层窗口
void RenderLayered(HWND hwnd, int viewMode) {
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return;
    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) return;

    if (!EnsureLayerSurface(width, height)) {
        return;
    }

    // 获取 DPI，并按 DPI 缩放圆角半径
    UINT dpi = GetWindowDpi(hwnd);
    int corner = max(4, MulDiv(CORNER_RADIUS, dpi, 96)); // 最小值保护

    // 背景颜色和 alpha（基准 alpha）
    BYTE bgR = 0, bgG = 0, bgB = 0;  // 改为黑色背景
    BYTE baseAlpha = (BYTE)WINDOW_ALPHA;

    // 先把缓冲区设置为不透明的背景预乘色（用 baseAlpha），后续会根据圆角蒙版重新赋值
    UINT pixels = width * height;
    BYTE *ptr = (BYTE*)g_layerSurface.bits;
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
    RECT drawRect = {0, 0, width, height};
    HBITMAP oldBmp = (HBITMAP)SelectObject(g_layerDC, g_layerSurface.bitmap);

    DrawTimetable(g_layerDC, drawRect, viewMode);

    // 遍历像素：为圆角计算平滑遮罩；背景像素按遮罩设为预乘色，文本像素保持不透明
    // 使用像素中心 (x+0.5, y+0.5) 计算距离平方并在平方域内线性插值（避免 sqrt）
    ptr = (BYTE*)g_layerSurface.bits;
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

    UpdateLayeredWindow(hwnd, NULL, &ptDst, &sizeWnd, g_layerDC, &ptSrc, 0, &bf, ULW_ALPHA);

    // 恢复 DC 原有的位图
    SelectObject(g_layerDC, oldBmp);
}
