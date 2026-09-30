#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace yuanqin::core {

// A normal event presses every note at once. An arpeggio plays its notes in order.
enum class PlayStyle {
    Chord,
    Arpeggio,
};

struct NoteEvent {
    PlayStyle style{PlayStyle::Chord};
    std::vector<char> notes;
};

// One sixteenth-note-sized position in a score. An empty value is a rest.
using Tick = std::optional<NoteEvent>;

class Score {
public:
    explicit Score(std::vector<Tick> ticks = {});

    [[nodiscard]] const std::vector<Tick>& ticks() const noexcept;
    [[nodiscard]] std::size_t tickCount() const noexcept;
    [[nodiscard]] bool empty() const noexcept;
    [[nodiscard]] bool hasNotes() const noexcept;

private:
    std::vector<Tick> ticks_;
};

}  // namespace yuanqin::core
