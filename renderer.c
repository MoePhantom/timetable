#include "renderer.h"
#include "timetable_data.h"
#include "sys_utils.h"
#include <windows.h>
#include <shellapi.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define BLUR_RADIUS_PIXELS      6
#define BLUR_BACKGROUND_ALPHA   230

extern ClassInfo timetable[DAYS][CLASSES];

static BOOL g_currentFrameHasOverflow = FALSE;
static BOOL g_lastFrameHasOverflow = FALSE;
static BOOL g_blurEnabled = TRUE;
static BYTE *g_blurScratch = NULL;
static size_t g_blurScratchSize = 0;
static BYTE *g_backgroundSnapshot = NULL;
static size_t g_backgroundSnapshotSize = 0;

typedef HRESULT (WINAPI *PFNDwmFlush)(VOID);
static PFNDwmFlush g_pfnDwmFlush = NULL;
static BOOL g_dwmTriedLoad = FALSE;


static void UpdateOverflowFlag(BOOL overflowed) {
    if (overflowed) {
        g_currentFrameHasOverflow = TRUE;
    }
}

void RendererSetBlurEnabled(BOOL enabled) {
    g_blurEnabled = enabled ? TRUE : FALSE;
}

BOOL RendererIsBlurEnabled(void) {
    return g_blurEnabled;
}

static BOOL EnsureBufferCapacity(BYTE **buffer, size_t *capacity, size_t required) {
    if (!buffer || !capacity) {
        return FALSE;
    }
    if (*capacity >= required) {
        return TRUE;
    }
    BYTE *newBuffer = (BYTE*)realloc(*buffer, required);
    if (!newBuffer) {
        return FALSE;
    }
    *buffer = newBuffer;
    *capacity = required;
    return TRUE;
}

static int ClampInt(int value, int minValue, int maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

static void WaitForCompositorFrame(void) {
    if (!g_dwmTriedLoad) {
        HMODULE hDwm = LoadLibraryW(L"dwmapi.dll");
        if (hDwm) {
            g_pfnDwmFlush = (PFNDwmFlush)GetProcAddress(hDwm, "DwmFlush");
        }
        g_dwmTriedLoad = TRUE;
    }
    if (g_pfnDwmFlush) {
        g_pfnDwmFlush();
    } else {
        Sleep(1);
    }
}

static BOOL CaptureWindowBackdrop(HWND hwnd, HDC targetDC, HDC screenDC, int width, int height, const RECT *wndRect, BYTE *pixelData, size_t bufferSize) {
    if (!hwnd || !targetDC || !screenDC || !wndRect || !pixelData) {
        return FALSE;
    }

    BOOL madeTransparent = FALSE;
    BOOL forcedHide = FALSE;

    if (SetLayeredWindowAttributes(hwnd, 0, 0, LWA_ALPHA)) {
        madeTransparent = TRUE;
        WaitForCompositorFrame();
    } else if (IsWindowVisible(hwnd)) {
        ShowWindow(hwnd, SW_HIDE);
        forcedHide = TRUE;
        WaitForCompositorFrame();
    }

    BOOL copied = BitBlt(targetDC, 0, 0, width, height, screenDC, wndRect->left, wndRect->top, SRCCOPY);

    if (madeTransparent) {
        SetLayeredWindowAttributes(hwnd, 0, 255, LWA_ALPHA);
    } else if (forcedHide) {
        ShowWindow(hwnd, SW_SHOWNA);
    }

    if (!copied) {
        return FALSE;
    }

    for (size_t i = 0; i < bufferSize; i += 4) {
        pixelData[i + 3] = 255;
    }

    return TRUE;
}

static void BoxBlurHorizontal(const BYTE *src, BYTE *dst, int width, int height, int stride, int radius) {
    int kernelSize = radius * 2 + 1;
    for (int y = 0; y < height; ++y) {
        const BYTE *row = src + y * stride;
        BYTE *outRow = dst + y * stride;
        int sumB = 0, sumG = 0, sumR = 0;
        for (int ix = -radius; ix <= radius; ++ix) {
            int cx = ClampInt(ix, 0, width - 1);
            const BYTE *p = row + cx * 4;
            sumB += p[0];
            sumG += p[1];
            sumR += p[2];
        }
        for (int x = 0; x < width; ++x) {
            BYTE *dstPx = outRow + x * 4;
            dstPx[0] = (BYTE)(sumB / kernelSize);
            dstPx[1] = (BYTE)(sumG / kernelSize);
            dstPx[2] = (BYTE)(sumR / kernelSize);
            dstPx[3] = 255;

            int oldIndex = x - radius;
            int newIndex = x + radius + 1;
            int oldClamped = ClampInt(oldIndex, 0, width - 1);
            int newClamped = ClampInt(newIndex, 0, width - 1);
            const BYTE *oldPx = row + oldClamped * 4;
            const BYTE *newPx = row + newClamped * 4;
            sumB += newPx[0] - oldPx[0];
            sumG += newPx[1] - oldPx[1];
            sumR += newPx[2] - oldPx[2];
        }
    }
}

static void BoxBlurVertical(const BYTE *src, BYTE *dst, int width, int height, int stride, int radius) {
    int kernelSize = radius * 2 + 1;
    for (int x = 0; x < width; ++x) {
        int sumB = 0, sumG = 0, sumR = 0;
        for (int iy = -radius; iy <= radius; ++iy) {
            int cy = ClampInt(iy, 0, height - 1);
            const BYTE *p = src + cy * stride + x * 4;
            sumB += p[0];
            sumG += p[1];
            sumR += p[2];
        }
        for (int y = 0; y < height; ++y) {
            BYTE *dstPx = dst + y * stride + x * 4;
            dstPx[0] = (BYTE)(sumB / kernelSize);
            dstPx[1] = (BYTE)(sumG / kernelSize);
            dstPx[2] = (BYTE)(sumR / kernelSize);
            dstPx[3] = 255;

            int oldIndex = y - radius;
            int newIndex = y + radius + 1;
            int oldClamped = ClampInt(oldIndex, 0, height - 1);
            int newClamped = ClampInt(newIndex, 0, height - 1);
            const BYTE *oldPx = src + oldClamped * stride + x * 4;
            const BYTE *newPx = src + newClamped * stride + x * 4;
            sumB += newPx[0] - oldPx[0];
            sumG += newPx[1] - oldPx[1];
            sumR += newPx[2] - oldPx[2];
        }
    }
}

static BOOL ApplyBoxBlur(BYTE *pixels, int width, int height, int stride, int radius) {
    if (!pixels || width <= 0 || height <= 0) {
        return FALSE;
    }
    if (radius <= 0) {
        return TRUE;
    }
    size_t required = (size_t)stride * (size_t)height;
    if (!EnsureBufferCapacity(&g_blurScratch, &g_blurScratchSize, required)) {
        return FALSE;
    }
    BYTE *temp = g_blurScratch;
    BoxBlurHorizontal(pixels, temp, width, height, stride, radius);
    BoxBlurVertical(temp, pixels, width, height, stride, radius);
    return TRUE;
}

static double ClampDouble(double value, double minValue, double maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
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
        BOOL hasCourse = FALSE;
        for (int i=0; i<CLASSES; i++) {
            if ((timetable[today][i].name && timetable[today][i].name[0] != L'\0') ||
                (timetable[today][i].location && timetable[today][i].location[0] != L'\0')) {
                hasCourse = TRUE;
                break;
            }
        }

        for (int i=0; i<CLASSES; i++) {
            MoveToEx(hdc, rc.left, rc.top + i*cellH, NULL);
            LineTo(hdc, rc.right, rc.top + i*cellH);

            RECT cellRect = {rc.left, rc.top + i*cellH, rc.right, rc.top + (i+1)*cellH};

            if (hasCourse && timetable[today][i].name) {
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

        if (!hasCourse) {
            LOGFONT lfHoliday = lf;
            lfHoliday.lfHeight = -MulDiv(28, dpi, 96);
            HFONT hHoliday = CreateFontIndirect(&lfHoliday);
            HFONT oldHoliday = NULL;
            if (hHoliday) {
                oldHoliday = (HFONT)SelectObject(hdc, hHoliday);
            }

            const WCHAR holidayText[] = L"放假";
            int charCount = (int)lstrlenW(holidayText);
            TEXTMETRIC tmHoliday;
            GetTextMetrics(hdc, &tmHoliday);
            int charSpacing = MulDiv(6, dpi, 96);
            int totalHeight = charCount * tmHoliday.tmHeight + (charCount - 1) * charSpacing;
            int startY = rc.top + ((rc.bottom - rc.top) - totalHeight) / 2;
            int centerX = rc.left + (rc.right - rc.left) / 2;

            for (int i = 0; i < charCount; ++i) {
                WCHAR ch[2] = { holidayText[i], L'\0' };
                SIZE chSize = {0};
                GetTextExtentPoint32W(hdc, ch, 1, &chSize);
                int x = centerX - chSize.cx / 2;
                int y = startY + i * (tmHoliday.tmHeight + charSpacing);
                TextOutW(hdc, x, y, ch, 1);
            }

            if (hHoliday && oldHoliday) {
                SelectObject(hdc, oldHoliday);
            }
            if (hHoliday) {
                DeleteObject(hHoliday);
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
                RECT cellRect = {rc.left + d*cellW, rc.top + i*cellH, rc.left + (d+1)*cellW, rc.top + (i+1)*cellH};
                
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

    int stride = width * 4;
    size_t bufferSize = (size_t)stride * (size_t)height;
    BOOL blurActive = g_blurEnabled ? TRUE : FALSE;
    BYTE baseAlpha = blurActive ? (BYTE)BLUR_BACKGROUND_ALPHA : (BYTE)WINDOW_ALPHA;
    BYTE *pixelData = (BYTE*)pvBits;

    HDC hdcScreen = GetDC(NULL);
    HDC memDC = CreateCompatibleDC(hdcScreen);
    HBITMAP oldBmp = (HBITMAP)SelectObject(memDC, hBitmap);

    RECT wndRect;
    GetWindowRect(hwnd, &wndRect);

    if (blurActive) {
        if (!CaptureWindowBackdrop(hwnd, memDC, hdcScreen, width, height, &wndRect, pixelData, bufferSize) ||
            !ApplyBoxBlur(pixelData, width, height, stride, BLUR_RADIUS_PIXELS)) {
            blurActive = FALSE;
            baseAlpha = (BYTE)WINDOW_ALPHA;
        }
    }

    if (!blurActive) {
        for (size_t i = 0; i < bufferSize; i += 4) {
            pixelData[i + 0] = 0;
            pixelData[i + 1] = 0;
            pixelData[i + 2] = 0;
            pixelData[i + 3] = 255;
        }
    }

    BYTE *backgroundPtr = NULL;
    if (EnsureBufferCapacity(&g_backgroundSnapshot, &g_backgroundSnapshotSize, bufferSize)) {
        backgroundPtr = g_backgroundSnapshot;
        memcpy(backgroundPtr, pixelData, bufferSize);
    } else if (blurActive) {
        // 内存不足时退回到半透明背景
        blurActive = FALSE;
        baseAlpha = (BYTE)WINDOW_ALPHA;
        for (size_t i = 0; i < bufferSize; i += 4) {
            pixelData[i + 0] = 0;
            pixelData[i + 1] = 0;
            pixelData[i + 2] = 0;
            pixelData[i + 3] = 255;
        }
        if (EnsureBufferCapacity(&g_backgroundSnapshot, &g_backgroundSnapshotSize, bufferSize)) {
            backgroundPtr = g_backgroundSnapshot;
            memcpy(backgroundPtr, pixelData, bufferSize);
        }
    }

    DrawTimetable(memDC, rc, viewMode);

    BYTE *ptr = pixelData;
    const BYTE *bgPtr = backgroundPtr;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
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
                    mask = (rPlus2 - dist2) / (rPlus2 - rMinus2);
                    if (mask < 0.0f) mask = 0.0f;
                    if (mask > 1.0f) mask = 1.0f;
                }
            }

            BYTE cornerMask = (BYTE)(mask * 255.0f + 0.5f);
            BOOL isBackgroundPixel = FALSE;
            if (bgPtr) {
                isBackgroundPixel = (ptr[0] == bgPtr[0] && ptr[1] == bgPtr[1] && ptr[2] == bgPtr[2]);
            } else if (!blurActive) {
                isBackgroundPixel = (ptr[0] == 0 && ptr[1] == 0 && ptr[2] == 0);
            }

            BYTE finalAlpha;
            if (isBackgroundPixel) {
                finalAlpha = (BYTE)(((int)baseAlpha * cornerMask) / 255);
            } else {
                finalAlpha = cornerMask;
            }

            ptr[3] = finalAlpha;
            ptr[0] = (BYTE)((ptr[0] * (int)finalAlpha) / 255);
            ptr[1] = (BYTE)((ptr[1] * (int)finalAlpha) / 255);
            ptr[2] = (BYTE)((ptr[2] * (int)finalAlpha) / 255);

            ptr += 4;
            if (bgPtr) {
                bgPtr += 4;
            }
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