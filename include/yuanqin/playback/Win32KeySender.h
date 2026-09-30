#pragma once

#include "yuanqin/playback/IKeySender.h"

namespace yuanqin::playback {

// Sends a key-down/key-up pair to the currently focused Windows application.
class Win32KeySender final : public IKeySender {
public:
    bool sendKey(char note) override;
};

}  // namespace yuanqin::playback
