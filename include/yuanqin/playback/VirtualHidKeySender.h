#pragma once

#include "yuanqin/playback/IKeySender.h"

#include <Windows.h>

#include <span>

namespace yuanqin::playback {

// Sends complete keyboard reports to the Yuanqin KMDF/VHF driver. The driver
// must be installed separately; this class never falls back to SendInput.
class VirtualHidKeySender final : public IKeySender {
public:
    explicit VirtualHidKeySender(unsigned short holdMilliseconds = 12) noexcept;
    ~VirtualHidKeySender() override;

    VirtualHidKeySender(const VirtualHidKeySender&) = delete;
    VirtualHidKeySender& operator=(const VirtualHidKeySender&) = delete;
    VirtualHidKeySender(VirtualHidKeySender&& other) noexcept;
    VirtualHidKeySender& operator=(VirtualHidKeySender&& other) noexcept;

    [[nodiscard]] bool isOpen() const noexcept;
    [[nodiscard]] DWORD lastError() const noexcept;

    bool sendKey(char note) override;
    bool sendChord(std::span<const char> notes) override;
    void releaseAll() noexcept override;

private:
    bool sendCommand(unsigned char command, std::span<const unsigned char> usages) noexcept;
    void close() noexcept;

    HANDLE handle_{INVALID_HANDLE_VALUE};
    DWORD lastError_{ERROR_SUCCESS};
    unsigned short holdMilliseconds_{12};
};

}  // namespace yuanqin::playback
