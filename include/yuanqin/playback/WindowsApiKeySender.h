#pragma once

#include "yuanqin/playback/IKeySender.h"

#include <Windows.h>

#include <span>

namespace yuanqin::playback {

// Windows API backend carried forward from the yuanQin_windowsAPI reference
// project. It emits scan-code key-down/key-up pairs through SendInput.
class WindowsApiKeySender final : public IKeySender {
public:
    explicit WindowsApiKeySender(HWND targetWindow = nullptr) noexcept;
    bool sendKey(char note) override;
    bool pressChord(std::span<const char> notes) override;
    bool releaseChord(std::span<const char> notes) override;
    void releaseAll() noexcept override;

private:
    bool sendKeyState(char note, bool keyUp);
    bool activateTarget() const;
    HWND targetWindow_{nullptr};
};

}  // namespace yuanqin::playback
