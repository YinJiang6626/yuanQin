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

bool Win32KeySender::sendKeyState(char note, bool keyUp) {
    if (targetWindow_ && GetForegroundWindow() != targetWindow_ &&
        !GenshinWindowTarget::activate(targetWindow_, nullptr)) {
        return false;
    }
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = VkKeyScanA(note);
    if (input.ki.wVk == 0xFFFF) {
        return false;
    }
    input.ki.dwFlags = keyUp ? KEYEVENTF_KEYUP : 0;
    return SendInput(1, &input, sizeof(input)) == 1;
}

bool Win32KeySender::pressChord(std::span<const char> notes) {
    for (const char note : notes) {
        if (!sendKeyState(note, false)) {
            return false;
        }
    }
    return true;
}

bool Win32KeySender::releaseChord(std::span<const char> notes) {
    for (const char note : notes) {
        if (!sendKeyState(note, true)) {
            return false;
        }
    }
    return true;
}

void Win32KeySender::releaseAll() noexcept {
    constexpr char notes[] = "QWERTYUASDFGHJZXCVBNM";
    for (const char note : notes) {
        if (note != '\0') {
            (void)sendKeyState(note, true);
        }
    }
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
