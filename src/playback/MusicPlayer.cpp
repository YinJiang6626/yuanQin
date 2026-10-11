#include "yuanqin/playback/MusicPlayer.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <array>
#include <optional>
#include <span>
#include <thread>
#include <vector>

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

struct ActiveHold {
    std::chrono::steady_clock::time_point releaseTime;
    std::vector<char> notes;
};

class HoldManager {
public:
    bool begin(IKeySender& sender, std::span<const char> notes,
               std::chrono::steady_clock::time_point releaseTime) {
        std::vector<char> newlyPressed;
        std::vector<char> uniqueNotes;
        for (const char note : notes) {
            const unsigned char key = static_cast<unsigned char>(note);
            if (std::find(uniqueNotes.begin(), uniqueNotes.end(), note) != uniqueNotes.end()) {
                continue;
            }
            uniqueNotes.push_back(note);
            if (counts_[key] == 0) {
                newlyPressed.push_back(note);
            }
        }
        if (!newlyPressed.empty() && !sender.pressChord(newlyPressed)) {
            return false;
        }
        for (const char note : uniqueNotes) {
            ++counts_[static_cast<unsigned char>(note)];
        }
        holds_.push_back({releaseTime, std::move(uniqueNotes)});
        return true;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point nextReleaseTime() const {
        auto result = std::chrono::steady_clock::time_point::max();
        for (const auto& hold : holds_) {
            result = std::min(result, hold.releaseTime);
        }
        return result;
    }

    bool releaseDue(IKeySender& sender, std::chrono::steady_clock::time_point now) {
        for (auto iterator = holds_.begin(); iterator != holds_.end();) {
            if (iterator->releaseTime > now) {
                ++iterator;
                continue;
            }
            std::vector<char> released;
            for (const char note : iterator->notes) {
                const unsigned char key = static_cast<unsigned char>(note);
                if (counts_[key] > 0 && --counts_[key] == 0) {
                    released.push_back(note);
                }
            }
            if (!released.empty() && !sender.releaseChord(released)) {
                return false;
            }
            iterator = holds_.erase(iterator);
        }
        return true;
    }

    [[nodiscard]] std::vector<char> withoutHeld(std::span<const char> notes) const {
        std::vector<char> result;
        result.reserve(notes.size());
        for (const char note : notes) {
            if (counts_[static_cast<unsigned char>(note)] == 0) {
                result.push_back(note);
            }
        }
        return result;
    }

    [[nodiscard]] bool empty() const noexcept { return holds_.empty(); }

private:
    std::array<unsigned int, 256> counts_{};
    std::vector<ActiveHold> holds_;
};

enum class WaitResult { ReachedTarget, Cancelled, SendFailed };

WaitResult waitUntil(std::chrono::steady_clock::time_point target,
                     const CancelCallback& shouldCancel, IKeySender& sender,
                     HoldManager& holds) {
    using namespace std::chrono_literals;

    while (true) {
        if (cancelled(shouldCancel)) {
            return WaitResult::Cancelled;
        }
        const auto now = std::chrono::steady_clock::now();
        if (!holds.releaseDue(sender, now)) {
            return WaitResult::SendFailed;
        }
        if (now >= target) {
            return WaitResult::ReachedTarget;
        }
        const auto wakeTime = std::min({target, holds.nextReleaseTime(), now + 10ms});
        std::this_thread::sleep_until(wakeTime);
    }
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
    const std::size_t playbackTickCount = score.playbackTickCount();
    const std::size_t startTick = std::min(options.startTick, playbackTickCount);
    auto nextTickTime = startTime;
    HoldManager holds;

    const auto waitFor = [&](std::chrono::steady_clock::time_point time,
                             std::size_t tickIndex) -> std::optional<PlaybackResult> {
        const auto result = waitUntil(time, shouldCancel, sender, holds);
        if (result == WaitResult::Cancelled) {
            return PlaybackResult{PlaybackStatus::Cancelled, tickIndex};
        }
        if (result == WaitResult::SendFailed) {
            return PlaybackResult{PlaybackStatus::SendFailed, tickIndex};
        }
        return std::nullopt;
    };

    // Seeking into the tail of an already-started hold must recreate the held
    // keys for their remaining duration; otherwise the transport would show a
    // hold without actually sounding it.
    const std::size_t sourceStartTick = std::min(startTick, score.tickCount());
    if (sourceStartTick > 0) {
        if (const auto failure = waitFor(startTime, startTick)) {
            return *failure;
        }
        for (std::size_t index = 0; index < sourceStartTick; ++index) {
            const auto& tick = score.ticks()[index];
            if (!tick || tick->style != core::PlayStyle::Hold) {
                continue;
            }
            const std::size_t holdEndTick = index + std::max<std::size_t>(1, tick->holdTicks);
            if (holdEndTick <= startTick) {
                continue;
            }
            const auto remaining = tickDuration * static_cast<double>(holdEndTick - startTick);
            const auto releaseTime = startTime +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(remaining);
            if (!holds.begin(sender, tick->notes, releaseTime)) {
                return {PlaybackStatus::SendFailed, startTick};
            }
        }
    }

    for (std::size_t tickIndex = sourceStartTick; tickIndex < score.tickCount();) {
        if (const auto failure = waitFor(nextTickTime, tickIndex)) {
            return *failure;
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
            const auto notes = holds.withoutHeld(event.notes);
            if (!notes.empty() && !sender.sendChord(notes)) {
                return {PlaybackStatus::SendFailed, tickIndex};
            }
            ++tickIndex;
            nextTickTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(tickDuration);
            continue;
        }

        if (event.style == core::PlayStyle::Hold) {
            if (cancelled(shouldCancel)) {
                return {PlaybackStatus::Cancelled, tickIndex};
            }
            const auto duration = tickDuration * static_cast<double>(std::max<std::size_t>(1, event.holdTicks));
            const auto releaseTime = nextTickTime +
                std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
            if (!holds.begin(sender, event.notes, releaseTime)) {
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
                if (const auto failure = waitFor(noteTime, tickIndex)) {
                    return *failure;
                }
                const auto stepNotes = steps.empty()
                    ? holds.withoutHeld(std::span<const char>(&event.notes[stepIndex], 1))
                    : holds.withoutHeld(steps[stepIndex]);
                if (!stepNotes.empty() && !sender.sendChord(stepNotes)) {
                    return {PlaybackStatus::SendFailed, tickIndex};
                }
                ++stepIndex;
            } else {
                if (const auto failure = waitFor(occupiedTime, tickIndex + occupiedOffset)) {
                    return *failure;
                }
                if (onTick && tickIndex + occupiedOffset < playbackTickCount) {
                    onTick(tickIndex + occupiedOffset);
                }
                ++occupiedOffset;
            }
        }
        if (const auto failure = waitFor(nextTickTime + pipaDuration, tickIndex + skippedTicks)) {
            return *failure;
        }
        tickIndex += skippedTicks + 1;
        nextTickTime += pipaDuration;
    }

    // Continue the transport through a trailing hold, including after the last
    // written cell.  Its ending time, rather than the final cell's start time,
    // is the actual end of the piece.
    for (std::size_t tickIndex = std::max(startTick, score.tickCount());
         tickIndex < playbackTickCount; ++tickIndex) {
        if (const auto failure = waitFor(nextTickTime, tickIndex)) {
            return *failure;
        }
        if (onTick) {
            onTick(tickIndex);
        }
        nextTickTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(tickDuration);
    }

    if (const auto failure = waitFor(nextTickTime, playbackTickCount)) {
        return *failure;
    }

    return {PlaybackStatus::Completed, playbackTickCount};
}

}  // namespace yuanqin::playback
