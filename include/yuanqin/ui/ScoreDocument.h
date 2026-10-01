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
    [[nodiscard]] std::size_t beatCount() const noexcept;
    [[nodiscard]] const EditableMeasure* measure(std::size_t index) const noexcept;
    [[nodiscard]] const std::string& beat(std::size_t measureIndex,
                                          std::size_t beatIndex) const noexcept;
    [[nodiscard]] bool hasNotes() const noexcept;

    void setBeat(std::size_t measureIndex, std::size_t beatIndex, std::string value);
    void typeNote(std::size_t measureIndex, std::size_t beatIndex, char note);
    void beginGroup(std::size_t measureIndex, std::size_t beatIndex, char opening);
    [[nodiscard]] bool insertGroupNote(std::size_t measureIndex, std::size_t beatIndex,
                                       std::size_t& caretOffset, char note);
    [[nodiscard]] bool insertPipaChord(std::size_t measureIndex, std::size_t beatIndex,
                                       std::size_t& caretOffset);
    [[nodiscard]] bool eraseGroupCharacterBefore(std::size_t measureIndex,
                                                  std::size_t beatIndex,
                                                  std::size_t& caretOffset);
    void backspace(std::size_t measureIndex, std::size_t beatIndex);
    void clearBeat(std::size_t measureIndex, std::size_t beatIndex);

    // Linear operations used by the grid editor. A tick is one editable beat
    // (four ticks make one measure).
    [[nodiscard]] std::vector<std::string> beatsInRange(std::size_t startTick,
                                                         std::size_t count) const;
    void setBeatAt(std::size_t tickIndex, std::string value);
    void insertBeat(std::size_t tickIndex, std::string value);
    void insertBeats(std::size_t tickIndex, const std::vector<std::string>& values);
    void eraseBeats(std::size_t startTick, std::size_t count);
    void clearBeats(std::size_t startTick, std::size_t count);

private:
    void ensureMeasure(std::size_t index);
    [[nodiscard]] std::size_t contentBeatCount() const noexcept;

    std::vector<EditableMeasure> measures_;
};

}  // namespace yuanqin::ui
