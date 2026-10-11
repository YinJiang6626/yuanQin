#include "yuanqin/playback/WindowsApiKeySender.h"

#include "yuanqin/playback/GenshinWindowTarget.h"

#include <Windows.h>

#include <chrono>
#include <cctype>
#include <thread>

namespace yuanqin::playback {

WindowsApiKeySender::WindowsApiKeySender(HWND targetWindow) noexcept
    : targetWindow_(targetWindow) {}

bool WindowsApiKeySender::activateTarget() const {
    if (targetWindow_ && GetForegroundWindow() != targetWindow_ &&
        !GenshinWindowTarget::activate(targetWindow_, nullptr)) {
        return false;
    }
    return true;
}

bool WindowsApiKeySender::sendKeyState(char note, bool keyUp) {
    if (!activateTarget()) {
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
    input.ki.dwFlags = KEYEVENTF_SCANCODE | (keyUp ? KEYEVENTF_KEYUP : 0);
    return SendInput(1, &input, sizeof(input)) == 1;
}

bool WindowsApiKeySender::sendKey(char note) {
    if (!sendKeyState(note, false)) {
        return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(12));
    return sendKeyState(note, true);
}

bool WindowsApiKeySender::pressChord(std::span<const char> notes) {
    for (const char note : notes) {
        if (!sendKeyState(note, false)) {
            return false;
        }
    }
    return true;
}

bool WindowsApiKeySender::releaseChord(std::span<const char> notes) {
    for (const char note : notes) {
        if (!sendKeyState(note, true)) {
            return false;
        }
    }
    return true;
}

void WindowsApiKeySender::releaseAll() noexcept {
    // SendInput has no per-sender key-state query.  Releasing the complete
    // instrument layout is safe and guarantees that cancelling a long hold
    // cannot leave one of our notes physically pressed.
    constexpr char notes[] = "QWERTYUASDFGHJZXCVBNM";
    for (const char note : notes) {
        if (note != '\0') {
            (void)sendKeyState(note, true);
        }
    }
}

}  // namespace yuanqin::playback
