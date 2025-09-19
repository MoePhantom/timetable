#include "timetable_data.h"

// 课程表数据（UTF-16）
ClassInfo timetable[DAYS][CLASSES] = {
    {  // 周一
        {L"高数高数高数高数高数", L"教学楼A101"}, 
        {L"英语", L"外语楼B205"}, 
        {L"C语言", L"实验楼C301"}, 
        {L"体育", L"体育馆"}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}
    },
    {  // 周二
        {L"离散", L"教学楼A205"}, 
        {L"英语", L"外语楼B301"}, 
        {L"线代", L"教学楼A108"}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}
    },
    {  // 周三
        {L"概率", L"教学楼B102"}, 
        {L"物理", L"实验楼A201"}, 
        {L"C实验", L"实验楼C405"}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}
    },
    {  // 周四
        {L"毛概", L"教学楼C101"}, 
        {L"英语", L"外语楼B101"}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}
    },
    {  // 周五
        {L"操作系统", L"实验楼D201"}, 
        {L"编译原理", L"教学楼A301"}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}
    },
    {  // 周六
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}
    },
    {  // 周日
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}, 
        {NULL, NULL}
    }
};