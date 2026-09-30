#include "MainWindow.h"

#include <Windows.h>

int WINAPI WinMain(HINSTANCE instance, HINSTANCE, LPSTR, int showCommand) {
    SetProcessDPIAware();

    yuanqin::app::MainWindow mainWindow;
    if (!mainWindow.create(instance, showCommand)) {
        MessageBoxW(nullptr, L"无法创建应用窗口。", L"幽歌琴谱", MB_OK | MB_ICONERROR);
        return 1;
    }

    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (mainWindow.processShortcut(message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return static_cast<int>(message.wParam);
}
