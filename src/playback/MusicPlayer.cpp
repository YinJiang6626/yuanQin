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

struct PendingArpeggio {
    std::vector<std::vector<char>> steps;
    std::size_t nextStep{};
    std::chrono::steady_clock::time_point nextStepTime;
    std::chrono::steady_clock::duration interval{};
    std::chrono::steady_clock::time_point endTime;
};

class ArpeggioManager {
public:
    void begin(const core::NoteEvent& event, std::chrono::steady_clock::time_point startTime,
               std::chrono::steady_clock::duration interval) {
        PendingArpeggio arpeggio;
        arpeggio.steps = event.arpeggioSteps;
        if (arpeggio.steps.empty()) {
            arpeggio.steps.reserve(event.notes.size());
            for (const char note : event.notes) {
                arpeggio.steps.push_back({note});
            }
        }
        arpeggio.nextStepTime = startTime;
        arpeggio.interval = std::max(interval, std::chrono::steady_clock::duration::zero());
        arpeggio.endTime = startTime + arpeggio.interval * arpeggio.steps.size();
        if (!arpeggio.steps.empty()) {
            arpeggios_.push_back(std::move(arpeggio));
        }
    }

    bool playDue(IKeySender& sender, const HoldManager& holds,
                 std::chrono::steady_clock::time_point now) {
        for (auto iterator = arpeggios_.begin(); iterator != arpeggios_.end();) {
            while (iterator->nextStep < iterator->steps.size() &&
                   iterator->nextStepTime <= now) {
                const auto notes = holds.withoutHeld(iterator->steps[iterator->nextStep]);
                if (!notes.empty() && !sender.sendChord(notes)) {
                    return false;
                }
                ++iterator->nextStep;
                iterator->nextStepTime += iterator->interval;
            }
            if (iterator->nextStep == iterator->steps.size() && now >= iterator->endTime) {
                iterator = arpeggios_.erase(iterator);
            } else {
                ++iterator;
            }
        }
        return true;
    }

    [[nodiscard]] std::chrono::steady_clock::time_point nextStepTime() const {
        auto result = std::chrono::steady_clock::time_point::max();
        for (const auto& arpeggio : arpeggios_) {
            result = std::min(result, arpeggio.nextStep < arpeggio.steps.size()
                                           ? arpeggio.nextStepTime : arpeggio.endTime);
        }
        return result;
    }

    [[nodiscard]] bool empty() const noexcept { return arpeggios_.empty(); }

private:
    std::vector<PendingArpeggio> arpeggios_;
};

enum class WaitResult { ReachedTarget, Cancelled, SendFailed };

WaitResult waitUntil(std::chrono::steady_clock::time_point target,
                     const CancelCallback& shouldCancel, IKeySender& sender,
                     HoldManager& holds, ArpeggioManager& arpeggios) {
    using namespace std::chrono_literals;

    while (true) {
        if (cancelled(shouldCancel)) {
            return WaitResult::Cancelled;
        }
        const auto now = std::chrono::steady_clock::now();
        // If the OS wakes us late, preserve score-time ordering: actions that
        // belong after the requested score tick must not overtake that tick.
        const auto dueThrough = std::min(now, target);
        if (!holds.releaseDue(sender, dueThrough)) {
            return WaitResult::SendFailed;
        }
        if (!arpeggios.playDue(sender, holds, dueThrough)) {
            return WaitResult::SendFailed;
        }
        if (now >= target) {
            return WaitResult::ReachedTarget;
        }
        const auto wakeTime = std::min(
            {target, holds.nextReleaseTime(), arpeggios.nextStepTime(), now + 10ms});
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
    ArpeggioManager arpeggios;

    const auto beginHold = [&](std::span<const char> notes, std::size_t holdTicks,
                               std::chrono::steady_clock::time_point holdStart) {
        const auto duration = tickDuration * static_cast<double>(
            std::max<std::size_t>(1, holdTicks));
        const auto releaseTime = holdStart +
            std::chrono::duration_cast<std::chrono::steady_clock::duration>(duration);
        return holds.begin(sender, notes, releaseTime);
    };

    const auto waitFor = [&](std::chrono::steady_clock::time_point time,
                             std::size_t tickIndex) -> std::optional<PlaybackResult> {
        const auto result = waitUntil(time, shouldCancel, sender, holds, arpeggios);
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
            if (!tick) {
                continue;
            }
            const auto restoreHold = [&](std::span<const char> notes, std::size_t holdTicks) {
                const std::size_t holdEndTick = index + std::max<std::size_t>(1, holdTicks);
                if (holdEndTick <= startTick) {
                    return true;
                }
                const auto remaining = tickDuration * static_cast<double>(holdEndTick - startTick);
                const auto releaseTime = startTime +
                    std::chrono::duration_cast<std::chrono::steady_clock::duration>(remaining);
                return holds.begin(sender, notes, releaseTime);
            };
            if (tick->style == core::PlayStyle::Hold &&
                !restoreHold(tick->notes, tick->holdTicks)) {
                return {PlaybackStatus::SendFailed, startTick};
            }
            for (const auto& hold : tick->embeddedHolds) {
                if (!restoreHold(hold.notes, hold.holdTicks)) {
                    return {PlaybackStatus::SendFailed, startTick};
                }
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
            for (const auto& hold : event.embeddedHolds) {
                if (!beginHold(hold.notes, hold.holdTicks, nextTickTime)) {
                    return {PlaybackStatus::SendFailed, tickIndex};
                }
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
            if (!beginHold(event.notes, event.holdTicks, nextTickTime)) {
                return {PlaybackStatus::SendFailed, tickIndex};
            }
            ++tickIndex;
            nextTickTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(tickDuration);
            continue;
        }

        // A pipa/arpeggio is an independent timed stream. Its following steps
        // are dispatched while later score cells retain their normal tempo.
        const std::size_t stepCount = event.arpeggioSteps.empty()
            ? event.notes.size() : event.arpeggioSteps.size();
        auto arpeggioInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::max(options.arpeggioStepInterval, std::chrono::milliseconds::zero()));
        if (options.arpeggioTimingMode == ArpeggioTimingMode::Adaptive && stepCount > 0) {
            const auto span = tickDuration * static_cast<double>(
                std::max<std::size_t>(1, event.adaptiveArpeggioSpanTicks));
            arpeggioInterval = std::chrono::duration_cast<std::chrono::steady_clock::duration>(span) /
                static_cast<std::chrono::steady_clock::duration::rep>(stepCount);
        }
        arpeggios.begin(event, nextTickTime, arpeggioInterval);
        if (!arpeggios.playDue(sender, holds, std::chrono::steady_clock::now())) {
            return {PlaybackStatus::SendFailed, tickIndex};
        }
        ++tickIndex;
        nextTickTime += std::chrono::duration_cast<std::chrono::steady_clock::duration>(tickDuration);
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

    // A final pipa note may outlast the final written cell. Keep playback alive
    // until all independently scheduled pipa steps and held keys are finished.
    while (!holds.empty() || !arpeggios.empty()) {
        const auto nextPending = std::min(holds.nextReleaseTime(), arpeggios.nextStepTime());
        if (const auto failure = waitFor(nextPending, playbackTickCount)) {
            return *failure;
        }
    }

    return {PlaybackStatus::Completed, playbackTickCount};
}

}  // namespace yuanqin::playback
