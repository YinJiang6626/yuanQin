#include "yuanqin/playback/MusicPlayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <thread>

namespace yuanqin::playback {
namespace {

class ReleaseGuard {
public:
    explicit ReleaseGuard(IKeySender& sender) noexcept : sender_(sender) {}
    ~ReleaseGuard() { sender_.releaseAll(); }

private:
    IKeySender& sender_;
};

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
    ReleaseGuard releaseGuard(sender);
    if (options.bpm <= 0.0) {
        return {PlaybackStatus::InvalidTempo, 0};
    }

    const auto tickDuration = std::chrono::duration<double>(60.0 / options.bpm / 4.0);
    const auto startTime = std::chrono::steady_clock::now() + options.startDelay;
    const std::size_t startTick = std::min(options.startTick, score.tickCount());
    auto nextTickTime = startTime;

    for (std::size_t tickIndex = startTick; tickIndex < score.tickCount();) {
        if (!waitUntil(nextTickTime, shouldCancel)) {
            return {PlaybackStatus::Cancelled, tickIndex};
        }

        if (onTick) {
            onTick(tickIndex);
        }

        const auto& tick = score.ticks()[tickIndex];
        if (!tick) {
            ++tickIndex;
            nextTickTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(tickDuration);
            continue;
        }

        const auto& event = *tick;
        if (event.style == core::PlayStyle::Chord) {
            if (cancelled(shouldCancel)) {
                return {PlaybackStatus::Cancelled, tickIndex};
            }
            if (!sender.sendChord(event.notes)) {
                return {PlaybackStatus::SendFailed, tickIndex};
            }
            ++tickIndex;
            nextTickTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(tickDuration);
            continue;
        }

        const auto& steps = event.arpeggioSteps;
        const std::size_t stepCount = steps.empty() ? event.notes.size() : steps.size();
        const auto stepInterval = std::max(options.arpeggioStepInterval,
                                           std::chrono::milliseconds::zero());
        const auto pipaDuration = stepInterval * static_cast<long long>(stepCount);
        const auto pipaSeconds = std::chrono::duration<double>(pipaDuration).count();
        const std::size_t skippedTicks = static_cast<std::size_t>(std::floor(
            pipaSeconds / tickDuration.count()));

        // Send notes and advance the visual playhead on their respective clocks.
        std::size_t stepIndex = 0;
        std::size_t occupiedOffset = 1;
        while (stepIndex < stepCount || occupiedOffset <= skippedTicks) {
            const auto noteTime = stepIndex < stepCount
                ? nextTickTime + stepInterval * static_cast<long long>(stepIndex)
                : std::chrono::steady_clock::time_point::max();
            const auto occupiedTime = occupiedOffset <= skippedTicks
                ? nextTickTime + std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                    tickDuration * static_cast<double>(occupiedOffset))
                : std::chrono::steady_clock::time_point::max();
            if (noteTime <= occupiedTime) {
                if (!waitUntil(noteTime, shouldCancel)) {
                    return {PlaybackStatus::Cancelled, tickIndex};
                }
                if (steps.empty() ? !sender.sendKey(event.notes[stepIndex])
                                  : !sender.sendChord(steps[stepIndex])) {
                    return {PlaybackStatus::SendFailed, tickIndex};
                }
                ++stepIndex;
            } else {
                if (!waitUntil(occupiedTime, shouldCancel)) {
                    return {PlaybackStatus::Cancelled, tickIndex + occupiedOffset};
                }
                if (onTick && tickIndex + occupiedOffset < score.tickCount()) {
                    onTick(tickIndex + occupiedOffset);
                }
                ++occupiedOffset;
            }
        }
        if (!waitUntil(nextTickTime + pipaDuration, shouldCancel)) {
            return {PlaybackStatus::Cancelled, tickIndex + skippedTicks};
        }
        tickIndex += skippedTicks + 1;
        nextTickTime += pipaDuration;
    }

    if (!waitUntil(nextTickTime, shouldCancel)) {
        return {PlaybackStatus::Cancelled, score.tickCount()};
    }

    return {PlaybackStatus::Completed, score.tickCount()};
}

}  // namespace yuanqin::playback
