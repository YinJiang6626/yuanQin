#include "yuanqin/ui/CompositionDocument.h"

#include "yuanqin/core/ScoreParser.h"

#include <algorithm>
#include <cctype>

namespace yuanqin::ui {
namespace {

std::string extractHand(std::string_view text, char hand) {
    std::string result;
    for (std::size_t index = 0; index + 1 < text.size(); ++index) {
        if (std::toupper(static_cast<unsigned char>(text[index])) != hand ||
            text[index + 1] != '(') {
            continue;
        }
        int depth = 1;
        const std::size_t start = index + 2;
        for (std::size_t cursor = start; cursor < text.size(); ++cursor) {
            if (text[cursor] == '(') {
                ++depth;
            } else if (text[cursor] == ')' && --depth == 0) {
                result.append(text.substr(start, cursor - start));
                index = cursor;
                break;
            }
        }
    }
    return result;
}

std::string measureText(const ScoreDocument& source, std::size_t measureIndex) {
    std::string result;
    for (std::size_t beatIndex = 0; beatIndex < kBeatsPerMeasure; ++beatIndex) {
        const auto& beat = source.beat(measureIndex, beatIndex);
        result += beat.empty() ? " " : beat;
    }
    result.push_back('/');
    return result;
}

bool isPipa(const std::string& value) {
    return value.size() >= 2 && value.front() == '[' && value.back() == ']';
}

std::string playableNotes(std::string_view value) {
    std::string notes;
    for (const char note : value) {
        if (core::ScoreParser::isPlayableNote(note)) notes.push_back(note);
    }
    return notes;
}

std::vector<std::string> pipaSteps(const std::string& value) {
    std::vector<std::string> result;
    if (!isPipa(value)) {
        return result;
    }

    for (std::size_t index = 1; index + 1 < value.size();) {
        if (core::ScoreParser::isPlayableNote(value[index])) {
            result.emplace_back(1, value[index++]);
            continue;
        }
        if (value[index] == '(') {
            const std::size_t start = ++index;
            int depth = 1;
            while (index + 1 < value.size() && depth > 0) {
                if (value[index] == '(') {
                    ++depth;
                } else if (value[index] == ')') {
                    --depth;
                }
                ++index;
            }
            const std::size_t end = depth == 0 ? index - 1 : index;
            const std::string notes = playableNotes(
                std::string_view(value).substr(start, end - start));
            if (!notes.empty()) {
                result.push_back("(" + notes + ")");
            }
            continue;
        }
        ++index;
    }
    return result;
}

std::string combineEvents(const std::string& first, const std::string& second) {
    if (first.empty()) return second;
    if (second.empty()) return first;
    std::string notes;
    for (const char note : playableNotes(first) + playableNotes(second)) {
        // A simultaneous note only needs one key press. Keep the first hand's
        // order while removing repeated notes from both ordinary and pipa merges.
        if (notes.find(note) == std::string::npos) {
            notes.push_back(note);
        }
    }
    if (notes.empty()) {
        return {};
    }
    return notes.size() == 1 ? notes : "(" + notes + ")";
}

std::string makePipa(const std::vector<std::string>& steps) {
    std::string result{"["};
    for (const auto& step : steps) {
        result += step;
    }
    result += ']';
    return result;
}

std::string mergePipaAndBeat(const std::string& pipa, const std::string& beat,
                              bool pipaComesFirst) {
    auto steps = pipaSteps(pipa);
    if (steps.empty()) {
        const std::string notes = playableNotes(beat);
        if (!notes.empty()) {
            steps.push_back(notes.size() == 1 ? notes : "(" + notes + ")");
        }
        return makePipa(steps);
    }
    steps.front() = pipaComesFirst ? combineEvents(steps.front(), beat)
                                   : combineEvents(beat, steps.front());
    return makePipa(steps);
}

std::string mergePipas(const std::string& right, const std::string& left) {
    const auto rightSteps = pipaSteps(right);
    const auto leftSteps = pipaSteps(left);
    std::vector<std::string> merged;
    merged.reserve(std::max(rightSteps.size(), leftSteps.size()));
    for (std::size_t index = 0; index < std::max(rightSteps.size(), leftSteps.size()); ++index) {
        const std::string rightStep = index < rightSteps.size() ? rightSteps[index] : std::string{};
        const std::string leftStep = index < leftSteps.size() ? leftSteps[index] : std::string{};
        merged.push_back(combineEvents(rightStep, leftStep));
    }
    return makePipa(merged);
}

std::string combinedBeat(const std::string& right, const std::string& left) {
    if (right.empty()) return left;
    if (left.empty()) return right;
    if (isPipa(right) && isPipa(left)) {
        return mergePipas(right, left);
    }
    if (isPipa(right)) {
        return mergePipaAndBeat(right, left, true);
    }
    if (isPipa(left)) {
        return mergePipaAndBeat(left, right, false);
    }
    return combineEvents(right, left);
}

}  // namespace

CompositionDocument CompositionDocument::fromText(std::string_view text) {
    CompositionDocument result;
    result.right_ = ScoreDocument::fromText(extractHand(text, 'R'));
    result.left_ = ScoreDocument::fromText(extractHand(text, 'L'));
    return result;
}

std::string CompositionDocument::toText() const {
    const std::size_t measureCount = std::max(right_.measureCount(), left_.measureCount());
    if (measureCount == 0) {
        return "R()\nL()\n";
    }
    std::string result;
    for (std::size_t measure = 0; measure < measureCount; ++measure) {
        result += "R(" + measureText(right_, measure) + ")\n";
        result += "L(" + measureText(left_, measure) + ")\n";
    }
    return result;
}

ScoreDocument CompositionDocument::mergedScore() const {
    ScoreDocument result;
    const std::size_t count = std::max(right_.beatCount(), left_.beatCount());
    for (std::size_t tick = 0; tick < count; ++tick) {
        const auto& rightBeat = right_.beat(tick / kBeatsPerMeasure, tick % kBeatsPerMeasure);
        const auto& leftBeat = left_.beat(tick / kBeatsPerMeasure, tick % kBeatsPerMeasure);
        result.setBeatAt(tick, combinedBeat(rightBeat, leftBeat));
    }
    return result;
}

ScoreDocument& CompositionDocument::right() noexcept { return right_; }
ScoreDocument& CompositionDocument::left() noexcept { return left_; }
const ScoreDocument& CompositionDocument::right() const noexcept { return right_; }
const ScoreDocument& CompositionDocument::left() const noexcept { return left_; }

}  // namespace yuanqin::ui
