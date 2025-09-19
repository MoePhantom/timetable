# Repository Guidelines

## Project Structure & Module Organization
The widget is a single Win32 app. `timetable.c` hosts `WinMain`, the window procedure, timers, and tray control. `renderer.c/h` draw the layered window and manage scrolling text; `sys_utils.c/h` wrap DPI math. Sample data lives in `timetable_data.c/h`; adjust `DAYS` and `CLASSES` before resizing the matrix. `compile.bat` is the authoritative build entry point, and the generated `timetable.exe` should stay untracked.

## Build, Test, and Development Commands
Run `./compile.bat` in PowerShell or `cmd` to call MinGW `gcc` with `-municode` and the required Win32 libraries. The equivalent manual command is `gcc -municode timetable.c timetable_data.c sys_utils.c renderer.c -o timetable.exe -lgdi32 -lshell32 -luser32 -lwinmm`，确保高精度计时 API 正常链接。Launch `timetable.exe` directly to smoke-test; use `taskkill /IM timetable.exe /F` if the tray icon hides the process.

## Coding Style & Naming Conventions
Indent with 4 spaces and avoid tabs. Keep macros in SCREAMING_SNAKE_CASE, exported functions in PascalCase, and file-local helpers `static`. Prefer wide-character APIs and literals; all user-facing strings stay `WCHAR` arrays. Group window state in globals near the top of `timetable.c`, and gate experimental code paths with `#ifdef DEBUG`.

## Testing Guidelines
There is no automated suite yet, so rely on manual checks. After each change, rebuild, run the app, toggle day/week views, and drag toward screen edges to confirm snap distances. Test at 100%, 125%, and 150% display scaling to verify DPI math and that `RendererHasOverflowingText` stays false unless scrolling is intended. Document any regressions or quirks in the PR description.

## Commit & Pull Request Guidelines
Existing history follows Conventional Commits with optional scopes and Chinese summaries (e.g., `feat(timetable): 调整 DPI 适配`). Keep subjects imperative and under 72 characters. For each PR, provide a short summary, list the manual tests you ran, and attach screenshots or GIFs for UI changes. Reference related issues and flag reviewers when touching `compile.bat` or shared headers.
