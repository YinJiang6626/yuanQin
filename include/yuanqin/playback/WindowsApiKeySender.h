#pragma once

#include "yuanqin/playback/IKeySender.h"

#include <Windows.h>

namespace yuanqin::playback {

// Windows API backend carried forward from the yuanQin_windowsAPI reference
// project. It emits scan-code key-down/key-up pairs through SendInput.
class WindowsApiKeySender final : public IKeySender {
public:
    explicit WindowsApiKeySender(HWND targetWindow = nullptr) noexcept;
    bool sendKey(char note) override;

private:
    HWND targetWindow_{nullptr};
};

}  // namespace yuanqin::playback
