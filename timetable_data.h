#ifndef TIMETABLE_DATA_H
#define TIMETABLE_DATA_H

#include <windows.h>

#define DAYS 7
#define CLASSES 8

// 课程信息结构
typedef struct {
    WCHAR* name;      // 课程名称
    WCHAR* location;  // 课程位置
} ClassInfo;

// 课程表数据（UTF-16）
extern ClassInfo timetable[DAYS][CLASSES];

#endif // TIMETABLE_DATA_H