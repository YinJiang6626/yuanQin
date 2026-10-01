#include "yuanqin/playback/WindowsApiKeySender.h"

#include "yuanqin/playback/GenshinWindowTarget.h"

#include <Windows.h>

#include <chrono>
#include <cctype>
#include <thread>

namespace yuanqin::playback {

WindowsApiKeySender::WindowsApiKeySender(HWND targetWindow) noexcept
    : targetWindow_(targetWindow) {}

bool WindowsApiKeySender::sendKey(char note) {
    if (targetWindow_ && GetForegroundWindow() != targetWindow_ &&
        !GenshinWindowTarget::activate(targetWindow_, nullptr)) {
        return false;
    }

    const UINT virtualKey = static_cast<UINT>(
        std::toupper(static_cast<unsigned char>(note)));
    const UINT scanCode = MapVirtualKeyW(virtualKey, MAPVK_VK_TO_VSC);
    if (scanCode == 0) {
        return false;
    }

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = 0;
    input.ki.wScan = static_cast<WORD>(scanCode);
    input.ki.dwFlags = KEYEVENTF_SCANCODE;

    if (SendInput(1, &input, sizeof(input)) != 1) {
        return false;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    input.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    return SendInput(1, &input, sizeof(input)) == 1;
}

}  // namespace yuanqin::playback
