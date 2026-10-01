#pragma once

#include "yuanqin/playback/IKeySender.h"

#include <Windows.h>

namespace yuanqin::playback {

// Uses the same virtual-key SendInput sequence as the original yinfu.h implementation.
class Win32KeySender final : public IKeySender {
public:
    explicit Win32KeySender(HWND targetWindow = nullptr) noexcept;
    bool sendKey(char note) override;

private:
    static void push_keybd(char data);
    HWND targetWindow_{nullptr};
};

}  // namespace yuanqin::playback
