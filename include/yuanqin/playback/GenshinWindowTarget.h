#pragma once

#include <Windows.h>

namespace yuanqin::playback {

// Finds the running Genshin Impact game window and makes it the keyboard target.
class GenshinWindowTarget {
public:
    [[nodiscard]] static HWND find() noexcept;
    [[nodiscard]] static bool activate(HWND overlayWindow = nullptr) noexcept;
    [[nodiscard]] static bool activate(HWND targetWindow, HWND overlayWindow) noexcept;
};

}  // namespace yuanqin::playback
