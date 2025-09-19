@echo off
gcc -municode timetable.c timetable_data.c sys_utils.c renderer.c -o timetable.exe -lgdi32 -lshell32 -luser32