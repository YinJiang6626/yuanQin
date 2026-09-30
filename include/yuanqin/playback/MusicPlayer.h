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

    // Retains the timing used by the original program for bracketed pipa notes.
    double arpeggioNoteSpanTicks{2.0};
    std::size_t arpeggioOccupiedTicks{4};
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
