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

        if (value == '(' || value == '[') {
            const char closing = value == '(' ? ')' : ']';
            auto notes = parseGroup(source, index, closing, sourceOffset, result);
            if (notes.empty()) {
                if (tick < kTicksPerMeasure) {
                    ++tick;
                }
                addDiagnostic(result, sourceOffset + index - 1, "空音符组会被当作休止符。");
                continue;
            }
            addEvent(measure, tick,
                     {value == '(' ? PlayStyle::Chord : PlayStyle::Arpeggio, std::move(notes)},
                     result, sourceOffset + index - 1);
            continue;
        }

        if (value == ')' || value == ']') {
            addDiagnostic(result, sourceOffset + index - 1, "发现未匹配的音符组结束符，已忽略。");
            continue;
        }

        if (ScoreParser::isPlayableNote(value)) {
            addEvent(measure, tick, {PlayStyle::Chord, {normalizeNote(value)}}, result,
                     sourceOffset + index - 1);
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
