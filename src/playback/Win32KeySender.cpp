#include "yuanqin/playback/Win32KeySender.h"

#include "yuanqin/playback/GenshinWindowTarget.h"

#include <Windows.h>

namespace yuanqin::playback {

Win32KeySender::Win32KeySender(HWND targetWindow) noexcept
    : targetWindow_(targetWindow) {}

bool Win32KeySender::sendKey(char note) {
    if (targetWindow_ && GetForegroundWindow() != targetWindow_ &&
        !GenshinWindowTarget::activate(targetWindow_, nullptr)) {
        return false;
    }

    push_keybd(note);
    return true;
}

void Win32KeySender::push_keybd(char data) {
    INPUT input;
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VkKeyScanA(data);
    input.ki.time = 0;
    input.ki.wScan = 0;
    input.ki.dwExtraInfo = 0;

    input.ki.dwFlags = 0;
    SendInput(1, &input, sizeof(INPUT));

    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

}  // namespace yuanqin::playback
