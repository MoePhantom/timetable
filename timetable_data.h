#ifndef TIMETABLE_DATA_H
#define TIMETABLE_DATA_H

#include <windows.h>

#define DAYS 7
#define CLASSES 8

// 课程表数据（UTF-16）
extern WCHAR* timetable[DAYS][CLASSES];

#endif // TIMETABLE_DATA_H