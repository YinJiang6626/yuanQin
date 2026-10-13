#include "yuanqin/core/ScoreParser.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <optional>
#include <utility>

namespace yuanqin::core {
namespace {

constexpr std::size_t kTicksPerMeasure = 4;

char normalizeNote(char value) noexcept {
    return static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
}

bool isRest(char value) noexcept {
    return value == ' ' || value == '\t';
}

void addDiagnostic(ParseResult& result, std::size_t offset, std::string message) {
    result.diagnostics.push_back({offset, std::move(message)});
}

void addEvent(std::vector<Tick>& measure, std::size_t& tick, NoteEvent event,
              ParseResult& result, std::size_t offset) {
    if (tick >= kTicksPerMeasure) {
        addDiagnostic(result, offset, "一个小节最多只能包含 4 个节拍，后续音符已忽略。");
        return;
    }

    measure[tick] = std::move(event);
    ++tick;
}

std::vector<char> parseGroup(std::string_view source, std::size_t& index, char closing,
                             std::size_t sourceOffset, ParseResult& result) {
    std::vector<char> notes;
    const std::size_t groupStart = index - 1;

    while (index < source.size() && source[index] != closing) {
        const char value = source[index++];
        if (ScoreParser::isPlayableNote(value)) {
            notes.push_back(normalizeNote(value));
        } else if (std::isalpha(static_cast<unsigned char>(value))) {
            addDiagnostic(result, sourceOffset + index - 1,
                          "此字符不是原神竖琴的有效按键，已忽略。");
        }
    }

    if (index == source.size()) {
        addDiagnostic(result, sourceOffset + groupStart, "音符组缺少结束符。");
        return {};
    }

    ++index;  // closing delimiter
    return notes;
}

struct ParsedArpeggio {
    std::vector<char> notes;
    std::vector<std::vector<char>> steps;
    std::vector<EmbeddedHold> holds;
};

struct ParsedHold {
    std::vector<char> notes;
    std::size_t ticks{1};
};

ParsedHold parseHold(std::string_view source, std::size_t& index,
                     std::size_t sourceOffset, ParseResult& result) {
    ParsedHold hold;
    const std::size_t groupStart = index - 1;

    if (index < source.size() && source[index] == '(') {
        ++index;
        hold.notes = parseGroup(source, index, ')', sourceOffset, result);
    } else if (index < source.size() && ScoreParser::isPlayableNote(source[index])) {
        hold.notes.push_back(normalizeNote(source[index++]));
    } else if (index < source.size() && std::isalpha(static_cast<unsigned char>(source[index]))) {
        addDiagnostic(result, sourceOffset + index,
                      "此字符不是原神竖琴的有效按键，已忽略。");
        ++index;
    }

    while (index < source.size() && source[index] == '-') {
        ++hold.ticks;
        ++index;
    }

    if (index >= source.size() || source[index] != '}') {
        addDiagnostic(result, sourceOffset + groupStart, "长按音符组缺少结束符或包含无效内容。");
        while (index < source.size() && source[index] != '}') {
            ++index;
        }
    }
    if (index < source.size() && source[index] == '}') {
        ++index;
    }
    return hold;
}

struct ParsedChord {
    std::vector<char> notes;
    std::vector<EmbeddedHold> holds;
    std::optional<ParsedArpeggio> arpeggio;
    bool hasStopMarker{false};
};

void appendUniqueNotes(std::vector<char>& destination, const std::vector<char>& source) {
    for (const char note : source) {
        if (std::find(destination.begin(), destination.end(), note) == destination.end()) {
            destination.push_back(note);
        }
    }
}

void mergeArpeggios(ParsedArpeggio& destination, ParsedArpeggio source) {
    appendUniqueNotes(destination.notes, source.notes);
    destination.holds.insert(destination.holds.end(),
                             std::make_move_iterator(source.holds.begin()),
                             std::make_move_iterator(source.holds.end()));
    const std::size_t count = std::max(destination.steps.size(), source.steps.size());
    destination.steps.resize(count);
    for (std::size_t step = 0; step < source.steps.size(); ++step) {
        appendUniqueNotes(destination.steps[step], source.steps[step]);
    }
}

ParsedArpeggio parseArpeggio(std::string_view source, std::size_t& index,
                             std::size_t sourceOffset, ParseResult& result);

// A parenthesized group is the parallel-composition container. It can carry
// ordinary chord notes, long holds, and pipa streams at the same score cell.
ParsedChord parseChord(std::string_view source, std::size_t& index,
                       std::size_t sourceOffset, ParseResult& result) {
    ParsedChord chord;
    const std::size_t groupStart = index - 1;

    const auto mergeNestedChord = [&chord](ParsedChord nested) {
        appendUniqueNotes(chord.notes, nested.notes);
        chord.holds.insert(chord.holds.end(),
                           std::make_move_iterator(nested.holds.begin()),
                           std::make_move_iterator(nested.holds.end()));
        chord.hasStopMarker = chord.hasStopMarker || nested.hasStopMarker;
        if (nested.arpeggio) {
            if (chord.arpeggio) {
                mergeArpeggios(*chord.arpeggio, std::move(*nested.arpeggio));
            } else {
                chord.arpeggio = std::move(nested.arpeggio);
            }
        }
    };

    while (index < source.size() && source[index] != ')') {
        const char value = source[index++];
        if (ScoreParser::isPlayableNote(value)) {
            appendUniqueNotes(chord.notes, std::vector<char>{normalizeNote(value)});
            continue;
        }
        if (value == '{') {
            auto hold = parseHold(source, index, sourceOffset, result);
            if (!hold.notes.empty()) {
                chord.holds.push_back({std::move(hold.notes), hold.ticks});
            }
            continue;
        }
        if (value == '[') {
            auto arpeggio = parseArpeggio(source, index, sourceOffset, result);
            if (!arpeggio.steps.empty()) {
                if (chord.arpeggio) {
                    mergeArpeggios(*chord.arpeggio, std::move(arpeggio));
                } else {
                    chord.arpeggio = std::move(arpeggio);
                }
            }
            continue;
        }
        if (value == '(') {
            mergeNestedChord(parseChord(source, index, sourceOffset, result));
            continue;
        }
        if (value == '|') {
            chord.hasStopMarker = true;
            continue;
        }
        if (std::isalpha(static_cast<unsigned char>(value))) {
            addDiagnostic(result, sourceOffset + index - 1,
                          "此字符不是原神竖琴的有效按键，已忽略。");
        }
    }

    if (index == source.size()) {
        addDiagnostic(result, sourceOffset + groupStart, "音符组缺少结束符。");
        return {};
    }
    ++index;  // ')'
    return chord;
}

ParsedArpeggio parseArpeggio(std::string_view source, std::size_t& index,
                             std::size_t sourceOffset, ParseResult& result) {
    ParsedArpeggio arpeggio;
    const std::size_t groupStart = index - 1;

    while (index < source.size() && source[index] != ']') {
        const char value = source[index++];
        if (ScoreParser::isPlayableNote(value)) {
            const char note = normalizeNote(value);
            arpeggio.notes.push_back(note);
            arpeggio.steps.push_back({note});
            continue;
        }
        if (value == '(') {
            auto chord = parseChord(source, index, sourceOffset, result);
            if (!chord.notes.empty()) {
                appendUniqueNotes(arpeggio.notes, chord.notes);
                arpeggio.steps.push_back(std::move(chord.notes));
            } else {
                addDiagnostic(result, sourceOffset + index - 1,
                              "琵琶音内的空和弦已忽略。");
            }
            arpeggio.holds.insert(arpeggio.holds.end(),
                                  std::make_move_iterator(chord.holds.begin()),
                                  std::make_move_iterator(chord.holds.end()));
            continue;
        }
        if (value == '{') {
            auto hold = parseHold(source, index, sourceOffset, result);
            if (!hold.notes.empty()) {
                arpeggio.holds.push_back({std::move(hold.notes), hold.ticks});
            }
            continue;
        }
        if (value == ')') {
            addDiagnostic(result, sourceOffset + index - 1,
                          "琵琶音内发现未匹配的和弦结束符，已忽略。");
        } else if (std::isalpha(static_cast<unsigned char>(value))) {
            addDiagnostic(result, sourceOffset + index - 1,
                          "此字符不是原神竖琴的有效按键，已忽略。");
        }
    }

    if (index == source.size()) {
        addDiagnostic(result, sourceOffset + groupStart, "音符组缺少结束符。");
        return {};
    }
    ++index;  // ']'
    return arpeggio;
}

enum class SourceCellKind {
    Ignore,
    Rest,
    Arpeggio,
    SilentStopMarker,
    SoundingStopMarker,
    Other,
};

// Reads one *score cell* from the source text.  This intentionally works on
// the original character stream instead of the four-cell measure buffer: an
// adaptive pipa may look across a measure boundary and must still see a `|`
// written after the fourth cell.
SourceCellKind readSourceCell(std::string_view source, std::size_t& index) {
    if (index >= source.size()) {
        return SourceCellKind::Ignore;
    }
    const char value = source[index++];
    if (isRest(value)) {
        return SourceCellKind::Rest;
    }
    if (value == '/' || value == '\r' || value == '\n') {
        return SourceCellKind::Ignore;
    }
    if (value == '|') {
        return SourceCellKind::SilentStopMarker;
    }
    if (value == '[') {
        bool hasNotes = false;
        while (index < source.size() && source[index] != ']') {
            hasNotes = hasNotes || ScoreParser::isPlayableNote(source[index]);
            ++index;
        }
        if (index < source.size()) {
            ++index;
        }
        return hasNotes ? SourceCellKind::Arpeggio : SourceCellKind::Other;
    }
    if (value == '(') {
        bool hasStopMarker = false;
        bool hasNotes = false;
        std::size_t braceDepth = 0;
        std::size_t parenthesisDepth = 1;
        bool hasArpeggio = false;
        while (index < source.size()) {
            const char current = source[index++];
            if (current == '{') {
                ++braceDepth;
            } else if (current == '}' && braceDepth > 0) {
                --braceDepth;
            } else if (braceDepth == 0 && current == '(') {
                ++parenthesisDepth;
            } else if (current == '|' && braceDepth == 0) {
                hasStopMarker = true;
            } else if (braceDepth == 0 && current == '[') {
                hasArpeggio = true;
            } else if (current == ')' && braceDepth == 0) {
                if (--parenthesisDepth == 0) {
                    break;
                }
            } else if (braceDepth == 0 && ScoreParser::isPlayableNote(current)) {
                hasNotes = true;
            }
        }
        if (hasArpeggio) {
            return SourceCellKind::Arpeggio;
        }
        return hasStopMarker ? (hasNotes ? SourceCellKind::SoundingStopMarker
                                         : SourceCellKind::SilentStopMarker)
                             : SourceCellKind::Other;
    }
    if (value == '{') {
        while (index < source.size() && source[index] != '}') {
            ++index;
        }
        if (index < source.size()) {
            ++index;
        }
        return SourceCellKind::Other;
    }
    return ScoreParser::isPlayableNote(value) ? SourceCellKind::Other : SourceCellKind::Ignore;
}

std::size_t adaptiveSpanAfterArpeggio(std::string_view source, std::size_t afterArpeggio) {
    constexpr std::size_t kLookAheadLimit = 63;
    std::size_t cursor = afterArpeggio;
    std::size_t followingCells = 0;
    std::size_t leadingRests = 0;
    bool onlyLeadingRests = true;

    while (cursor < source.size() && followingCells < kLookAheadLimit) {
        const auto cell = readSourceCell(source, cursor);
        if (cell == SourceCellKind::Ignore) {
            continue;
        }
        if (cell == SourceCellKind::Arpeggio) {
            break;
        }
        ++followingCells;
        if (cell == SourceCellKind::SilentStopMarker ||
            cell == SourceCellKind::SoundingStopMarker) {
            // | is always a pure pipa control marker. Even in (|E), E keeps
            // its own normal playback, but that cell is outside the pipa's
            // reserved duration.
            return followingCells;
        }
        if (cell == SourceCellKind::Rest && onlyLeadingRests) {
            ++leadingRests;
        } else {
            onlyLeadingRests = false;
        }
    }
    // Without an explicit stop marker, only the uninterrupted rests directly
    // after the pipa reserve time; a following ordinary note ends that gap.
    return leadingRests + 1;
}

void prepareAdaptiveArpeggioSpans(std::string_view source, std::vector<Tick>& ticks) {
    std::vector<std::size_t> sourceSpans;
    for (std::size_t index = 0; index < source.size();) {
        const auto before = index;
        const auto cell = readSourceCell(source, index);
        if (cell == SourceCellKind::Arpeggio) {
            sourceSpans.push_back(adaptiveSpanAfterArpeggio(source, index));
        }
        // Protect against malformed input should a future cell reader decide
        // not to consume a character.
        if (index == before) {
            ++index;
        }
    }

    std::size_t arpeggioIndex = 0;
    for (auto& tick : ticks) {
        if (!tick || tick->style != PlayStyle::Arpeggio) {
            continue;
        }
        if (arpeggioIndex < sourceSpans.size()) {
            tick->adaptiveArpeggioSpanTicks = sourceSpans[arpeggioIndex];
        }
        ++arpeggioIndex;
    }
}

void parseMeasure(std::string_view source, std::size_t sourceOffset, ParseResult& result,
                  std::vector<Tick>& destination) {
    std::vector<Tick> measure(kTicksPerMeasure);
    std::size_t tick = 0;

    for (std::size_t index = 0; index < source.size();) {
        const char value = source[index++];

        if (isRest(value)) {
            if (tick < kTicksPerMeasure) {
                ++tick;
            }
            continue;
        }

        if (value == '(') {
            auto chord = parseChord(source, index, sourceOffset, result);
            if (chord.notes.empty() && chord.holds.empty() && !chord.arpeggio &&
                !chord.hasStopMarker) {
                if (tick < kTicksPerMeasure) {
                    ++tick;
                }
                addDiagnostic(result, sourceOffset + index - 1, "空音符组会被当作休止符。");
                continue;
            }
            NoteEvent event;
            event.style = chord.arpeggio ? PlayStyle::Arpeggio : PlayStyle::Chord;
            if (chord.arpeggio) {
                event.arpeggioSteps = std::move(chord.arpeggio->steps);
                event.notes = std::move(chord.arpeggio->notes);
                if (!chord.notes.empty()) {
                    if (event.arpeggioSteps.empty()) {
                        event.arpeggioSteps.push_back(chord.notes);
                    } else {
                        appendUniqueNotes(event.arpeggioSteps.front(), chord.notes);
                    }
                    appendUniqueNotes(event.notes, chord.notes);
                }
                event.embeddedHolds = std::move(chord.arpeggio->holds);
            } else {
                event.notes = std::move(chord.notes);
            }
            event.arpeggioStop = chord.hasStopMarker;
            event.embeddedHolds.insert(event.embeddedHolds.end(),
                                       std::make_move_iterator(chord.holds.begin()),
                                       std::make_move_iterator(chord.holds.end()));
            addEvent(measure, tick, std::move(event), result, sourceOffset + index - 1);
            continue;
        }

        if (value == '[') {
            auto arpeggio = parseArpeggio(source, index, sourceOffset, result);
            if (arpeggio.steps.empty()) {
                if (tick < kTicksPerMeasure) {
                    ++tick;
                }
                addDiagnostic(result, sourceOffset + index - 1, "空音符组会被当作休止符。");
                continue;
            }
            NoteEvent event;
            event.style = PlayStyle::Arpeggio;
            event.notes = std::move(arpeggio.notes);
            event.arpeggioSteps = std::move(arpeggio.steps);
            event.embeddedHolds = std::move(arpeggio.holds);
            addEvent(measure, tick, std::move(event), result, sourceOffset + index - 1);
            continue;
        }

        if (value == '{') {
            auto hold = parseHold(source, index, sourceOffset, result);
            if (hold.notes.empty()) {
                if (tick < kTicksPerMeasure) {
                    ++tick;
                }
                addDiagnostic(result, sourceOffset + index - 1,
                              "空长按音符组会被当作休止符。");
                continue;
            }
            NoteEvent event;
            event.style = PlayStyle::Hold;
            event.notes = std::move(hold.notes);
            event.holdTicks = hold.ticks;
            addEvent(measure, tick, std::move(event), result, sourceOffset + index - 1);
            continue;
        }

        if (value == '|') {
            NoteEvent event;
            event.arpeggioStop = true;
            // A stop marker after the visible fourth cell can still delimit
            // the preceding adaptive pipa.  Its timing is already recorded
            // from the source stream, so it does not need a fifth event in
            // this fixed-width measure buffer.
            if (tick < kTicksPerMeasure) {
                addEvent(measure, tick, std::move(event), result, sourceOffset + index - 1);
            }
            continue;
        }

        if (value == ')' || value == ']' || value == '}') {
            addDiagnostic(result, sourceOffset + index - 1, "发现未匹配的音符组结束符，已忽略。");
            continue;
        }

        if (ScoreParser::isPlayableNote(value)) {
            NoteEvent event;
            event.notes = {normalizeNote(value)};
            addEvent(measure, tick, std::move(event), result, sourceOffset + index - 1);
        } else if (std::isalpha(static_cast<unsigned char>(value))) {
            addDiagnostic(result, sourceOffset + index - 1,
                          "此字符不是原神竖琴的有效按键，已忽略。");
        }
        // Digits and their +/- octave notation are deliberately ignored for now.
    }

    destination.insert(destination.end(), std::make_move_iterator(measure.begin()),
                       std::make_move_iterator(measure.end()));
}

}  // namespace

bool ScoreParser::isPlayableNote(char value) noexcept {
    switch (normalizeNote(value)) {
        case 'Q': case 'W': case 'E': case 'R': case 'T': case 'Y': case 'U':
        case 'A': case 'S': case 'D': case 'F': case 'G': case 'H': case 'J':
        case 'Z': case 'X': case 'C': case 'V': case 'B': case 'N': case 'M':
            return true;
        default:
            return false;
    }
}

ParseResult ScoreParser::parseText(std::string_view text) {
    ParseResult result;
    std::vector<Tick> ticks;
    std::size_t measureStart = 0;

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] == '/') {
            parseMeasure(text.substr(measureStart, index - measureStart), measureStart, result, ticks);
            measureStart = index + 1;
        }
    }

    // This makes text pasted into a future score editor usable even before its final '/'.
    if (measureStart < text.size()) {
        parseMeasure(text.substr(measureStart), measureStart, result, ticks);
    }

    prepareAdaptiveArpeggioSpans(text, ticks);
    result.score = Score(std::move(ticks));
    return result;
}

ParseResult ScoreParser::parseFile(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        ParseResult result;
        result.success = false;
        result.diagnostics.push_back({0, "无法打开乐谱文件：" + path.string()});
        return result;
    }

    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    return parseText(text);
}

}  // namespace yuanqin::core
