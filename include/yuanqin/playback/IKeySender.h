#pragma once

#include <span>

namespace yuanqin::playback {

// Platform and transport implementations convert Genshin notes into key taps.
class IKeySender {
public:
    virtual ~IKeySender() = default;
    virtual bool sendKey(char note) = 0;

    // Backends that can emit one HID report should override this so a chord is
    // genuinely simultaneous. Sequential transports retain the safe default.
    virtual bool sendChord(std::span<const char> notes) {
        for (const char note : notes) {
            if (!sendKey(note)) {
                return false;
            }
        }
        return true;
    }

    // Called when playback ends or is interrupted. Stateful backends use this
    // to prevent a key from remaining pressed after an error.
    virtual void releaseAll() noexcept {}
};

}  // namespace yuanqin::playback
