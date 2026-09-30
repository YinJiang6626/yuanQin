#pragma once

#include "yuanqin/playback/IKeySender.h"

#include <string>
#include <string_view>

namespace yuanqin::playback {

// Serial adapter for the existing STM32 keyboard-emulation firmware.
class SerialKeySender final : public IKeySender {
public:
    explicit SerialKeySender(std::string_view portName);
    ~SerialKeySender() override;

    SerialKeySender(const SerialKeySender&) = delete;
    SerialKeySender& operator=(const SerialKeySender&) = delete;

    [[nodiscard]] bool isOpen() const noexcept;
    bool sendKey(char note) override;

private:
    void* handle_{nullptr};
};

}  // namespace yuanqin::playback
