# 原神竖琴自动弹奏

这是一个面向 Windows 的原神竖琴自动弹奏程序。目前包含可视化琴谱编辑器，并提供两种播放输出方式：直接向当前前台窗口发送键盘事件，或按现有 STM32 固件协议经串口输出。

## 目录与职责

```text
include/yuanqin/core/       乐谱领域模型与解析器的公开接口
include/yuanqin/playback/   播放器、键盘/串口输出接口的公开接口
src/core/                   乐谱解析实现（不依赖 Windows）
src/playback/               时间调度与 Windows/串口实现
apps/keyboard_console/      键盘模式的临时控制台入口
apps/serial_console/        串口模式的临时控制台入口
tests/                      无需游戏或硬件的解析器测试
```

图形界面使用独立的 `ui::ScoreDocument` 维护可编辑内容；播放时可将序列化结果交给 `core::ScoreParser`，再由 `playback::MusicPlayer` 调度，因此编辑、保存/加载和播放逻辑彼此解耦。

## 图形界面

运行 `build/yuanqin_gui.exe` 即可启动“幽歌琴谱”。界面支持：

- 新建空白乐谱，以及同时打开多个乐谱并通过顶部标签切换；
- 从 `sheetMusic/` 加载 `.txt` 乐谱，加载后的修改只存在于内存；
- 保存到原文件或另存为新文件；
- 每行显示 4 个乐谱分区，每个分区包含 4 个最小播放拍位；
- 普通拍位只能输入一个字母音符；输入 `(` 或 `[` 会自动补全，并允许在组内连续输入音符；
- 使用方向键、Tab 在拍位间移动，Backspace 删除组内最后一个音，Delete 清空当前拍位。

常用快捷键为 `Ctrl+N`、`Ctrl+O`、`Ctrl+S`、`Ctrl+Shift+S` 和 `Ctrl+W`。

## 字母乐谱格式

- 高音：`Q W E R T Y U`
- 中音：`A S D F G H J`
- 低音：`Z X C V B N M`
- `/` 结束一个小节；每个小节有 4 个节拍位置。
- 单个字母占一个节拍；空格或 Tab 表示一个休止节拍。
- `(QAZ)` 表示和弦，同时按下括号内音符。
- `[QAZ]` 表示琵琶音，按顺序弹出括号内音符。为兼容旧版本，它占用该位置起的一个小节，建议将其放在小节开头，后面留空。

字母大小写不影响演奏。数字谱和 `+`/`-` 八度标识当前会被忽略，尚未实现转换。

例如：`(AZ)[QW]J /` 表示第一拍弹和弦 `AZ`，第二拍弹琵琶音 `QW`，第三拍弹 `J`，第四拍休止。

## 构建与测试

需要 CMake 和支持 C++20 的 Windows 编译器（仓库当前的 MinGW-w64 可直接使用）：

```powershell
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

生成的 `yuanqin_keyboard.exe` 是键盘模式；`yuanqin_serial.exe` 是 115200、8N1 的 STM32 串口模式。键盘模式开始倒计时后，请把原神窗口置于前台。串口模式保留了旧版每键 40ms 的保护间隔，避免现有单字节 STM32 固件吞键。
