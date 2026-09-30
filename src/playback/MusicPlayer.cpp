#include "yuanqin/playback/MusicPlayer.h"

#include <algorithm>
#include <chrono>
#include <thread>

namespace yuanqin::playback {
namespace {

bool cancelled(const CancelCallback& shouldCancel) {
    return shouldCancel && shouldCancel();
}

bool waitUntil(std::chrono::steady_clock::time_point target, const CancelCallback& shouldCancel) {
    using namespace std::chrono_literals;

    while (std::chrono::steady_clock::now() < target) {
        if (cancelled(shouldCancel)) {
            return false;
        }
        std::this_thread::sleep_until(std::min(target, std::chrono::steady_clock::now() + 10ms));
    }
    return !cancelled(shouldCancel);
}

}  // namespace

PlaybackResult MusicPlayer::play(const core::Score& score, IKeySender& sender,
                                 const PlaybackOptions& options,
                                 const CancelCallback& shouldCancel,
                                 const TickCallback& onTick) const {
    if (options.bpm <= 0.0) {
        return {PlaybackStatus::InvalidTempo, 0};
    }

    const auto tickDuration = std::chrono::duration<double>(60.0 / options.bpm / 4.0);
    const auto startTime = std::chrono::steady_clock::now() + options.startDelay;

    for (std::size_t tickIndex = 0; tickIndex < score.tickCount();) {
        const auto tickTime = startTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                              tickDuration * static_cast<double>(tickIndex));
        if (!waitUntil(tickTime, shouldCancel)) {
            return {PlaybackStatus::Cancelled, tickIndex};
        }

        if (onTick) {
            onTick(tickIndex);
        }

        const auto& tick = score.ticks()[tickIndex];
        if (!tick) {
            ++tickIndex;
            continue;
        }

        const auto& event = *tick;
        if (event.style == core::PlayStyle::Chord) {
            for (const char note : event.notes) {
                if (cancelled(shouldCancel)) {
                    return {PlaybackStatus::Cancelled, tickIndex};
                }
                if (!sender.sendKey(note)) {
                    return {PlaybackStatus::SendFailed, tickIndex};
                }
            }
            ++tickIndex;
            continue;
        }

        const auto noteInterval = tickDuration * options.arpeggioNoteSpanTicks /
                                  static_cast<double>(event.notes.size());
        for (std::size_t noteIndex = 0; noteIndex < event.notes.size(); ++noteIndex) {
            const auto noteTime = tickTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                                 noteInterval * static_cast<double>(noteIndex));
            if (!waitUntil(noteTime, shouldCancel)) {
                return {PlaybackStatus::Cancelled, tickIndex};
            }
            if (!sender.sendKey(event.notes[noteIndex])) {
                return {PlaybackStatus::SendFailed, tickIndex};
            }
        }

        // In the original score format a bracketed pipa note uses the rest of its measure.
        tickIndex += std::max<std::size_t>(1, options.arpeggioOccupiedTicks);
    }

    return {PlaybackStatus::Completed, score.tickCount()};
}

}  // namespace yuanqin::playback
