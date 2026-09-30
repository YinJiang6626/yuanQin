#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace yuanqin::ui {

constexpr std::size_t kBeatsPerMeasure = 4;
constexpr std::size_t kMeasuresPerRow = 4;

struct EditableMeasure {
    std::array<std::string, kBeatsPerMeasure> beats;

    [[nodiscard]] bool empty() const noexcept;
};

// Editable representation used by the UI. The playback Score remains a separate,
// immutable representation produced when playback starts.
class ScoreDocument {
public:
    ScoreDocument() = default;

    [[nodiscard]] static ScoreDocument fromText(std::string_view text);
    [[nodiscard]] std::string toText() const;

    [[nodiscard]] std::size_t measureCount() const noexcept;
    [[nodiscard]] const EditableMeasure* measure(std::size_t index) const noexcept;
    [[nodiscard]] const std::string& beat(std::size_t measureIndex,
                                          std::size_t beatIndex) const noexcept;
    [[nodiscard]] bool hasNotes() const noexcept;

    void setBeat(std::size_t measureIndex, std::size_t beatIndex, std::string value);
    void typeNote(std::size_t measureIndex, std::size_t beatIndex, char note);
    void beginGroup(std::size_t measureIndex, std::size_t beatIndex, char opening);
    void backspace(std::size_t measureIndex, std::size_t beatIndex);
    void clearBeat(std::size_t measureIndex, std::size_t beatIndex);

private:
    void ensureMeasure(std::size_t index);

    std::vector<EditableMeasure> measures_;
};

}  // namespace yuanqin::ui
