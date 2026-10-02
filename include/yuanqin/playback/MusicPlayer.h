#pragma once

#include "yuanqin/core/Score.h"
#include "yuanqin/playback/IKeySender.h"

#include <chrono>
#include <cstddef>
#include <functional>

namespace yuanqin::playback {

struct PlaybackOptions {
    double bpm{80.0};
    std::chrono::milliseconds startDelay{0};
    std::size_t startTick{0};

    // Each bracketed pipa step is emitted at this fixed interval.  Unlike the
    // score tempo, this is intentionally expressed in real milliseconds.
    std::chrono::milliseconds arpeggioStepInterval{107};
};

enum class PlaybackStatus {
    Completed,
    Cancelled,
    InvalidTempo,
    SendFailed,
};

struct PlaybackResult {
    PlaybackStatus status{PlaybackStatus::Completed};
    std::size_t tickIndex{};
};

using CancelCallback = std::function<bool()>;
using TickCallback = std::function<void(std::size_t tickIndex)>;

class MusicPlayer {
public:
    [[nodiscard]] PlaybackResult play(
        const core::Score& score,
        IKeySender& sender,
        const PlaybackOptions& options = {},
        const CancelCallback& shouldCancel = {},
        const TickCallback& onTick = {}) const;
};

}  // namespace yuanqin::playback
