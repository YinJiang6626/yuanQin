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

char closingFor(char opening) noexcept {
    switch (opening) {
        case '(': return ')';
        case '[': return ']';
        case '{': return '}';
        default: return '\0';
    }
}

// Returns the innermost still-open group at an insertion position.  The
// editor uses it to distinguish a pipa's `()` step from the pipa's `[]`
// container: `{}` is valid in the former, but a second `[]` is not.
char groupAtCaret(std::string_view value, std::size_t caretOffset) {
    std::vector<char> stack;
    caretOffset = std::min(caretOffset, value.size());
    for (std::size_t index = 0; index < caretOffset; ++index) {
        const char current = value[index];
        if (current == '(' || current == '[' || current == '{') {
            stack.push_back(current);
        } else if (!stack.empty() && current == closingFor(stack.back())) {
            stack.pop_back();
        }
    }
    return stack.empty() ? '\0' : stack.back();
}

// Keep nested parallel notation intact when the UI loads, saves, or moves a
// cell.  In particular, two-hand merging may produce `({W--}[ASDF])` and
// `((QW)[ASDF])`; flattening those forms would change their playback meaning.
// `index` points to an opening delimiter on entry.
void appendSanitizedGroup(std::string_view source, std::size_t& index, std::string& target) {
    const char opening = source[index++];
    const char closing = closingFor(opening);
    target.push_back(opening);

    while (index < source.size() && source[index] != closing) {
        const char current = source[index];
        if (core::ScoreParser::isPlayableNote(current)) {
            target.push_back(normalize(current));
            ++index;
            continue;
        }
        if (current == '-' && opening == '{') {
            target.push_back(current);
            ++index;
            continue;
        }
        if (current == '|' && opening == '(') {
            target.push_back(current);
            ++index;
            continue;
        }

        const bool nested = current == '(' || current == '[' || current == '{';
        const bool allowed = nested &&
            (opening == '(' || (opening == '[' && (current == '(' || current == '{')) ||
             (opening == '{' && current == '('));
        if (allowed) {
            appendSanitizedGroup(source, index, target);
            continue;
        }
        ++index;
    }
    if (index < source.size() && source[index] == closing) {
        ++index;
    }
    target.push_back(closing);
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

        if (value == '(' || value == '[' || value == '{') {
            std::string group;
            --index;
            appendSanitizedGroup(source, index, group);
            result.beats[beatIndex++] = std::move(group);
            continue;
        }

        if (value == '|') {
            result.beats[beatIndex++] = "|";
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
    if (first == '(' || first == '[' || first == '{') {
        std::string result;
        std::size_t index = 0;
        appendSanitizedGroup(value, index, result);
        return result;
    }

    if (core::ScoreParser::isPlayableNote(first)) {
        return std::string(1, normalize(first));
    }
    if (first == '|') {
        return "|";
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

std::size_t ScoreDocument::beatCount() const noexcept {
    return measures_.size() * kBeatsPerMeasure;
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
    if (value.size() >= 2 &&
        (value.front() == '(' || value.front() == '[' || value.front() == '{')) {
        value.insert(value.end() - 1, normalized);
    } else {
        value.assign(1, normalized);
    }
}

void ScoreDocument::beginGroup(std::size_t measureIndex, std::size_t beatIndex, char opening) {
    if (beatIndex >= kBeatsPerMeasure ||
        (opening != '(' && opening != '[' && opening != '{')) {
        return;
    }
    ensureMeasure(measureIndex);
    measures_[measureIndex].beats[beatIndex] = opening == '(' ? "()" :
        (opening == '[' ? "[]" : "{}");
}

bool ScoreDocument::insertGroupNote(std::size_t measureIndex, std::size_t beatIndex,
                                    std::size_t& caretOffset, char note) {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure) {
        return false;
    }
    auto& value = measures_[measureIndex].beats[beatIndex];
    if (value.size() < 2 || (value.front() != '(' && value.front() != '[' && value.front() != '{') ||
        (value.back() != ')' && value.back() != ']' && value.back() != '}') ||
        !core::ScoreParser::isPlayableNote(note)) {
        return false;
    }
    caretOffset = std::clamp(caretOffset, std::size_t{1}, value.size() - 1);
    value.insert(value.begin() + static_cast<std::ptrdiff_t>(caretOffset), normalize(note));
    ++caretOffset;
    return true;
}

bool ScoreDocument::insertGroupStopMarker(std::size_t measureIndex, std::size_t beatIndex,
                                          std::size_t& caretOffset) {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure) {
        return false;
    }
    auto& value = measures_[measureIndex].beats[beatIndex];
    if (value.size() < 2 || value.front() != '(' || value.back() != ')') {
        return false;
    }
    caretOffset = std::clamp(caretOffset, std::size_t{1}, value.size() - 1);
    value.insert(value.begin() + static_cast<std::ptrdiff_t>(caretOffset), '|');
    ++caretOffset;
    return true;
}

bool ScoreDocument::insertHoldDash(std::size_t measureIndex, std::size_t beatIndex,
                                   std::size_t& caretOffset) {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure) {
        return false;
    }
    auto& value = measures_[measureIndex].beats[beatIndex];
    if (value.size() < 2) {
        return false;
    }
    caretOffset = std::min(caretOffset, value.size() - 1);
    const std::size_t opening = value.rfind('{', caretOffset);
    if (opening == std::string::npos) {
        return false;
    }
    const std::size_t closing = value.find('}', opening + 1);
    if (closing == std::string::npos || caretOffset > closing) {
        return false;
    }
    caretOffset = std::clamp(caretOffset, opening + 1, closing);
    value.insert(value.begin() + static_cast<std::ptrdiff_t>(caretOffset), '-');
    ++caretOffset;
    return true;
}

bool ScoreDocument::insertEmbeddedHold(std::size_t measureIndex, std::size_t beatIndex,
                                       std::size_t& caretOffset) {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure) {
        return false;
    }
    auto& value = measures_[measureIndex].beats[beatIndex];
    if (value.size() < 2 ||
        !((value.front() == '(' && value.back() == ')') ||
          (value.front() == '[' && value.back() == ']'))) {
        return false;
    }
    caretOffset = std::clamp(caretOffset, std::size_t{1}, value.size() - 1);
    // A direct pipa body does not accept `{}`. It becomes available only
    // after the user has opened the pipa's inner chord, e.g. `[({W--}A)S]`.
    if (groupAtCaret(value, caretOffset) != '(') {
        return false;
    }
    value.insert(caretOffset, "{}");
    ++caretOffset;
    return true;
}

bool ScoreDocument::insertEmbeddedPipa(std::size_t measureIndex, std::size_t beatIndex,
                                       std::size_t& caretOffset) {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure) {
        return false;
    }
    auto& value = measures_[measureIndex].beats[beatIndex];
    if (value.size() < 2 || value.front() != '(' || value.back() != ')') {
        return false;
    }
    caretOffset = std::clamp(caretOffset, std::size_t{1}, value.size() - 1);
    if (groupAtCaret(value, caretOffset) != '(') {
        return false;
    }
    value.insert(caretOffset, "[]");
    ++caretOffset;
    return true;
}

bool ScoreDocument::insertPipaChord(std::size_t measureIndex, std::size_t beatIndex,
                                    std::size_t& caretOffset) {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure ||
        measures_[measureIndex].beats[beatIndex].size() < 2) {
        return false;
    }
    auto& value = measures_[measureIndex].beats[beatIndex];
    if (!((value.front() == '[' && value.back() == ']') ||
          (value.front() == '{' && value.back() == '}'))) {
        return false;
    }
    caretOffset = std::clamp(caretOffset, std::size_t{1}, value.size() - 1);
    value.insert(caretOffset, "()");
    ++caretOffset;
    return true;
}

bool ScoreDocument::eraseGroupCharacterBefore(std::size_t measureIndex,
                                              std::size_t beatIndex,
                                              std::size_t& caretOffset) {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure) {
        return false;
    }
    auto& value = measures_[measureIndex].beats[beatIndex];
    if (value.size() < 2 || (value.front() != '(' && value.front() != '[' && value.front() != '{') ||
        (value.back() != ')' && value.back() != ']' && value.back() != '}')) {
        return false;
    }
    caretOffset = std::clamp(caretOffset, std::size_t{1}, value.size() - 1);
    if (value == "()" || value == "[]" || value == "{}") {
        value.clear();
        caretOffset = 0;
        return true;
    }
    if (caretOffset <= 1) {
        return false;
    }
    const std::size_t eraseIndex = caretOffset - 1;
    if (value[eraseIndex] == '(' && eraseIndex + 1 < value.size() &&
        value[eraseIndex + 1] == ')') {
        value.erase(eraseIndex, 2);
    } else {
        value.erase(eraseIndex, 1);
    }
    --caretOffset;
    return true;
}

void ScoreDocument::backspace(std::size_t measureIndex, std::size_t beatIndex) {
    if (measureIndex >= measures_.size() || beatIndex >= kBeatsPerMeasure) {
        return;
    }

    auto& value = measures_[measureIndex].beats[beatIndex];
    if (value.size() > 2 && (value.front() == '(' || value.front() == '{')) {
        value.erase(value.end() - 2);
    } else if (value.size() > 2 && value.front() == '[') {
        const auto last = value.end() - 2;
        if (*last == ')') {
            const auto opening = value.rfind('(', value.size() - 2);
            if (opening != std::string::npos) {
                value.erase(opening, value.size() - opening - 1);
            } else {
                value.erase(last);
            }
        } else {
            value.erase(last);
        }
    } else {
        value.clear();
    }
}

void ScoreDocument::clearBeat(std::size_t measureIndex, std::size_t beatIndex) {
    if (measureIndex < measures_.size() && beatIndex < kBeatsPerMeasure) {
        measures_[measureIndex].beats[beatIndex].clear();
    }
}

std::vector<std::string> ScoreDocument::beatsInRange(std::size_t startTick,
                                                      std::size_t count) const {
    std::vector<std::string> result;
    result.reserve(count);
    for (std::size_t offset = 0; offset < count; ++offset) {
        const std::size_t tick = startTick + offset;
        result.push_back(beat(tick / kBeatsPerMeasure, tick % kBeatsPerMeasure));
    }
    return result;
}

void ScoreDocument::setBeatAt(std::size_t tickIndex, std::string value) {
    setBeat(tickIndex / kBeatsPerMeasure, tickIndex % kBeatsPerMeasure, std::move(value));
}

void ScoreDocument::insertBeat(std::size_t tickIndex, std::string value) {
    const std::size_t used = std::max(contentBeatCount(), tickIndex);
    for (std::size_t tick = used; tick > tickIndex; --tick) {
        setBeatAt(tick, beat((tick - 1) / kBeatsPerMeasure,
                             (tick - 1) % kBeatsPerMeasure));
    }
    setBeatAt(tickIndex, std::move(value));
}

void ScoreDocument::insertBeats(std::size_t tickIndex, const std::vector<std::string>& values) {
    if (values.empty()) {
        return;
    }
    for (std::size_t index = 0; index < values.size(); ++index) {
        insertBeat(tickIndex + index, values[index]);
    }
}

void ScoreDocument::eraseBeats(std::size_t startTick, std::size_t count) {
    if (count == 0) {
        return;
    }
    const std::size_t used = contentBeatCount();
    if (startTick >= used) {
        return;
    }
    const std::size_t endTick = std::min(used, startTick + count);
    const std::size_t removed = endTick - startTick;
    for (std::size_t tick = startTick; tick + removed < used; ++tick) {
        setBeatAt(tick, beat((tick + removed) / kBeatsPerMeasure,
                             (tick + removed) % kBeatsPerMeasure));
    }
    for (std::size_t tick = used - removed; tick < used; ++tick) {
        clearBeat(tick / kBeatsPerMeasure, tick % kBeatsPerMeasure);
    }
}

void ScoreDocument::clearBeats(std::size_t startTick, std::size_t count) {
    for (std::size_t offset = 0; offset < count; ++offset) {
        const std::size_t tick = startTick + offset;
        clearBeat(tick / kBeatsPerMeasure, tick % kBeatsPerMeasure);
    }
}

std::size_t ScoreDocument::contentBeatCount() const noexcept {
    for (std::size_t measureIndex = measures_.size(); measureIndex > 0; --measureIndex) {
        const auto& current = measures_[measureIndex - 1];
        for (std::size_t beatIndex = kBeatsPerMeasure; beatIndex > 0; --beatIndex) {
            if (!current.beats[beatIndex - 1].empty()) {
                return (measureIndex - 1) * kBeatsPerMeasure + beatIndex;
            }
        }
    }
    return 0;
}

}  // namespace yuanqin::ui
