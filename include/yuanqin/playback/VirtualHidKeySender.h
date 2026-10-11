#pragma once

#include "yuanqin/playback/IKeySender.h"

#include <Windows.h>

#include <span>
#include <vector>

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
    bool pressChord(std::span<const char> notes) override;
    bool releaseChord(std::span<const char> notes) override;
    void releaseAll() noexcept override;

private:
    bool sendCommand(unsigned char command, std::span<const unsigned char> usages) noexcept;
    bool sendPressedReport(std::span<const unsigned char> transientUsages) noexcept;
    void close() noexcept;

    HANDLE handle_{INVALID_HANDLE_VALUE};
    DWORD lastError_{ERROR_SUCCESS};
    unsigned short holdMilliseconds_{12};
    std::vector<unsigned char> heldUsages_;
};

}  // namespace yuanqin::playback
