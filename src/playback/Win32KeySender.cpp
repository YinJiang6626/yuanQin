#include "yuanqin/playback/Win32KeySender.h"

#include <Windows.h>

namespace yuanqin::playback {

bool Win32KeySender::sendKey(char note) {
    const SHORT virtualKey = VkKeyScanA(note);
    if (virtualKey == -1) {
        return false;
    }

    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = static_cast<WORD>(virtualKey & 0xFF);

    if (SendInput(1, &input, sizeof(input)) != 1) {
        return false;
    }

    input.ki.dwFlags = KEYEVENTF_KEYUP;
    return SendInput(1, &input, sizeof(input)) == 1;
}

}  // namespace yuanqin::playback
