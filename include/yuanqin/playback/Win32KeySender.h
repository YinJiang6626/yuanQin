#pragma once

#include "yuanqin/playback/IKeySender.h"

#include <Windows.h>

#include <span>

namespace yuanqin::playback {

// Uses the same virtual-key SendInput sequence as the original yinfu.h implementation.
class Win32KeySender final : public IKeySender {
public:
    explicit Win32KeySender(HWND targetWindow = nullptr) noexcept;
    bool sendKey(char note) override;
    bool pressChord(std::span<const char> notes) override;
    bool releaseChord(std::span<const char> notes) override;
    void releaseAll() noexcept override;

private:
    bool sendKeyState(char note, bool keyUp);
    static void push_keybd(char data);
    HWND targetWindow_{nullptr};
};

}  // namespace yuanqin::playback
