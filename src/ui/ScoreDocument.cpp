#include "yuanqin/ui/ScoreDocument.h"

#include "yuanqin/core/ScoreParser.h"

#include <algorithm>
#include <cctype>

namespace yuanqin::ui {
namespace {

const std::string kEmptyBeat;

char normalize(char value) noexcept {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
}

EditableMeasure parseMeasure(std::string_view source) {
    EditableMeasure result;
    std::size_t beatIndex = 0;

    for (std::size_t index = 0; index < source.size() && beatIndex < kBeatsPerMeasure;) {
        const char value = source[index++];

        if (value == '\r' || value == '\n') {
            continue;
        }
        if (value == ' ' || value == '\t') {
            ++beatIndex;
            continue;
        }

        if (value == '(' || value == '[') {
            const char closing = value == '(' ? ')' : ']';
            std::string group(1, value);
            while (index < source.size() && source[index] != closing) {
                if (core::ScoreParser::isPlayableNote(source[index])) {
                    group.push_back(normalize(source[index]));
                }
                ++index;
            }
            if (index < source.size() && source[index] == closing) {
                ++index;
            }
            group.push_back(closing);
            result.beats[beatIndex++] = std::move(group);
            continue;
        }

        if (core::ScoreParser::isPlayableNote(value)) {
            result.beats[beatIndex++] = std::string(1, normalize(value));
        }
        // Numeric notation and unrelated characters are intentionally ignored.
    }

    return result;
}

std::string sanitizeBeat(std::string_view value) {
    if (value.empty()) {
        return {};
    }

    const char first = value.front();
    if (first == '(' || first == '[') {
        const char closing = first == '(' ? ')' : ']';
        std::string result(1, first);
        for (std::size_t index = 1; index < value.size(); ++index) {
            if (core::ScoreParser::isPlayableNote(value[index])) {
                result.push_back(normalize(value[index]));
            }
        }
        result.push_back(closing);
        return result;
    }

    if (core::ScoreParser::isPlayableNote(first)) {
        return std::string(1, normalize(first));
    }
    return {};
}

}  // namespace

bool EditableMeasure::empty() const noexcept {
    return std::all_of(beats.begin(), beats.end(), [](const auto& beat) { return beat.empty(); });
}

ScoreDocument ScoreDocument::fromText(std::string_view text) {
    ScoreDocument document;
    std::size_t measureStart = 0;

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '/') {
            document.measures_.push_back(parseMeasure(text.substr(measureStart, index - measureStart)));
            measureStart = index + 1;
        }
    }

    const auto tail = text.substr(measureStart);
    const bool hasVisibleTail = std::any_of(tail.begin(), tail.end(), [](char value) {
        return value != '\r' && value != '\n' && value != ' ' && value != '\t';
    });
    if (hasVisibleTail) {
        document.measures_.push_back(parseMeasure(tail));
    }

    return document;
}

std::string ScoreDocument::toText() const {
    std::string result;
    for (std::size_t measureIndex = 0; measureIndex < measures_.size(); ++measureIndex) {
        const auto& current = measures_[measureIndex];
        for (const auto& currentBeat : current.beats) {
            result += currentBeat.empty() ? " " : currentBeat;
        }
        result.push_back('/');
        if ((measureIndex + 1) % kMeasuresPerRow == 0 && measureIndex + 1 < measures_.size()) {
            result.push_back('\n');
        }
    }
    return result;
}

std::size_t ScoreDocument::measureCount() const noexcept {
    return measures_.size();
}

const EditableMeasure* ScoreDocument::measure(std::size_t index) const noexcept {
    return index < measures_.size() ? &measures_[index] : nullptr;
}

const std::string& ScoreDocument::beat(std::size_t measureIndex,
                                       std::size_t beatIndex) const noexcept {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure) {
        return kEmptyBeat;
    }
    return measures_[measureIndex].beats[beatIndex];
}

bool ScoreDocument::hasNotes() const noexcept {
    return std::any_of(measures_.begin(), measures_.end(), [](const auto& current) {
        return !current.empty();
    });
}

void ScoreDocument::ensureMeasure(std::size_t index) {
    if (index >= measures_.size()) {
        measures_.resize(index + 1);
    }
}

void ScoreDocument::setBeat(std::size_t measureIndex, std::size_t beatIndex, std::string value) {
    if (beatIndex >= kBeatsPerMeasure) {
        return;
    }
    ensureMeasure(measureIndex);
    measures_[measureIndex].beats[beatIndex] = sanitizeBeat(value);
}

void ScoreDocument::typeNote(std::size_t measureIndex, std::size_t beatIndex, char note) {
    if (beatIndex >= kBeatsPerMeasure || !core::ScoreParser::isPlayableNote(note)) {
        return;
    }

    ensureMeasure(measureIndex);
    auto& value = measures_[measureIndex].beats[beatIndex];
    const char normalized = normalize(note);
    if (value.size() >= 2 && (value.front() == '(' || value.front() == '[')) {
        value.insert(value.end() - 1, normalized);
    } else {
        value.assign(1, normalized);
    }
}

void ScoreDocument::beginGroup(std::size_t measureIndex, std::size_t beatIndex, char opening) {
    if (beatIndex >= kBeatsPerMeasure || (opening != '(' && opening != '[')) {
        return;
    }
    ensureMeasure(measureIndex);
    measures_[measureIndex].beats[beatIndex] = opening == '(' ? "()" : "[]";
}

void ScoreDocument::backspace(std::size_t measureIndex, std::size_t beatIndex) {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure) {
        return;
    }

    auto& value = measures_[measureIndex].beats[beatIndex];
    if (value.size() > 2 && (value.front() == '(' || value.front() == '[')) {
        value.erase(value.end() - 2);
    } else {
        value.clear();
    }
}

void ScoreDocument::clearBeat(std::size_t measureIndex, std::size_t beatIndex) {
    if (measureIndex < measures_.size() && beatIndex < kBeatsPerMeasure) {
        measures_[measureIndex].beats[beatIndex].clear();
    }
}

}  // namespace yuanqin::ui
