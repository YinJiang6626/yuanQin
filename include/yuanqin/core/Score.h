#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace yuanqin::core {

// A normal event presses every note at once. An arpeggio plays its steps in order;
// each step may itself contain a chord.
enum class PlayStyle {
    Chord,
    Arpeggio,
    // A note or chord whose key-down state spans several score ticks.
    Hold,
};

struct NoteEvent {
    PlayStyle style{PlayStyle::Chord};
    std::vector<char> notes;
    std::vector<std::vector<char>> arpeggioSteps;
    // Includes the tick on which the hold starts.  For example, {W-} is 2.
    std::size_t holdTicks{1};
};

// One sixteenth-note-sized position in a score. An empty value is a rest.
using Tick = std::optional<NoteEvent>;

class Score {
public:
    explicit Score(std::vector<Tick> ticks = {});

    [[nodiscard]] const std::vector<Tick>& ticks() const noexcept;
    [[nodiscard]] std::size_t tickCount() const noexcept;
    // The musical timeline may outlast the final written cell when it ends
    // with a long hold. This is the duration used by playback and transport.
    [[nodiscard]] std::size_t playbackTickCount() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool hasNotes() const noexcept;

private:
    std::vector<Tick> ticks_;
};

}  // namespace yuanqin::core
