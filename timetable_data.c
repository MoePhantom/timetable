#include "timetable_data.h"

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