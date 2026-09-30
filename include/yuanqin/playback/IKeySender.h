#pragma once

namespace yuanqin::playback {

// Platform and transport implementations convert a Genshin note into one key press.
class IKeySender {
public:
    virtual ~IKeySender() = default;
    virtual bool sendKey(char note) = 0;
};

}  // namespace yuanqin::playback
