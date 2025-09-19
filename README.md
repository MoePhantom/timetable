# Timetable Widget

一个使用 Win32 API 开发的课程表桌面小组件示例。程序采用分层窗口呈现半透明、圆角的课程表界面，并支持日视图和周视图切换，适合作为了解 Windows 桌面小组件渲染流程的参考。

## 功能特性

- 🌤️ **分层窗口渲染**：通过 `UpdateLayeredWindow` 输出带透明度的圆角窗口。
- 📅 **双视图切换**：左键拖动窗口，右键单击托盘图标可在日视图与周视图之间切换。
- 🔔 **托盘常驻**：程序启动后最小化为系统托盘图标，支持托盘菜单退出。
- 🖥️ **高 DPI 支持**：运行时自动检测系统 DPI，对窗口尺寸、圆角半径及字体大小做缩放。
- ⏱️ **自动刷新**：每分钟重绘一次，以便后续扩展成实时数据。

## 目录结构

```
.
├── renderer.c/.h       # 分层窗口绘制逻辑
├── sys_utils.c/.h     # 与系统 DPI 相关的辅助方法
├── timetable.c        # 程序入口和窗口消息循环
├── timetable_data.c/.h# 示例课程表数据
└── complie.bat        # Windows 下的编译脚本（MinGW / gcc）
```

## 构建与运行

> ⚠️ 本项目依赖 Windows 平台和 Win32 API，需在 Windows 环境下编译运行。

1. 安装 [MinGW-w64](https://www.mingw-w64.org/) 或其他提供 `gcc` 的工具链，并确保 `gcc`、`windres` 等命令可用。
2. 在项目根目录执行批处理脚本：

   ```bat
   complie.bat
   ```

   脚本等价于执行：

   ```bat
   gcc -municode timetable.c timetable_data.c sys_utils.c renderer.c -o timetable.exe -lgdi32 -lshell32 -luser32
   ```

3. 双击运行 `timetable.exe`。窗口默认出现在屏幕右上角，拖动即可移动，靠近屏幕边缘会自动吸附。

## 自定义课程表

课程数据定义在 [`timetable_data.c`](timetable_data.c) 中的 `timetable` 数组。每个元素使用 UTF-16 宽字符字符串，可将示例课程替换为自己的课程信息。数组大小由 [`timetable_data.h`](timetable_data.h) 中的 `DAYS`（一周天数）和 `CLASSES`（每日节数）常量控制。

## 可能的扩展方向

- 从文件或网络加载课程数据，实现实时更新。
- 为课程单元格添加颜色、图标或详细提示信息。
- 增加设置窗口，允许用户调整透明度、主题或刷新间隔。

欢迎根据实际需求对代码进行拓展或改造！
