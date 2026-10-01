#include "yuanqin/ui/ScoreDocument.h"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    using yuanqin::ui::ScoreDocument;

    constexpr std::string_view example = "(CNW) H /J W /  H /W H /";
    auto loaded = ScoreDocument::fromText(example);
    require(loaded.measureCount() == 4, "four slash sections become one display row");
    require(loaded.beat(0, 0) == "(CNW)", "chord remains in one beat");
    require(loaded.beat(0, 1).empty(), "space becomes an empty beat");
    require(loaded.beat(0, 2) == "H", "single note is retained");
    require(loaded.beat(2, 0).empty() && loaded.beat(2, 1).empty(),
            "consecutive spaces remain separate rests");
    require(loaded.toText() == example, "formatted text round-trips");

    const auto trailingRest = ScoreDocument::fromText("Q   /    /");
    require(trailingRest.toText() == "Q   /    /", "explicit trailing rest measures are preserved");

    ScoreDocument edited;
    edited.typeNote(0, 0, 'q');
    require(edited.beat(0, 0) == "Q", "single note is normalized");
    edited.typeNote(0, 0, 'w');
    require(edited.beat(0, 0) == "W", "single beat accepts only one plain note");
    edited.beginGroup(0, 1, '(');
    require(edited.beat(0, 1) == "()", "opening parenthesis is auto-completed");
    edited.typeNote(0, 1, 'a');
    edited.typeNote(0, 1, 's');
    require(edited.beat(0, 1) == "(AS)", "a chord accepts multiple notes");
    edited.backspace(0, 1);
    require(edited.beat(0, 1) == "(A)", "backspace removes the last grouped note");
    edited.beginGroup(0, 2, '[');
    edited.typeNote(0, 2, 'z');
    require(edited.beat(0, 2) == "[Z]", "brackets auto-complete for arpeggios");

    const auto nestedPipa = ScoreDocument::fromText("[(ASD)ADF]/");
    require(nestedPipa.beat(0, 0) == "[(ASD)ADF]",
            "the editor preserves chords embedded in a pipa note");
    ScoreDocument sanitizedNested;
    sanitizedNested.setBeat(0, 0, "[(asd)adf]");
    require(sanitizedNested.beat(0, 0) == "[(ASD)ADF]",
            "editor normalization retains embedded pipa chords");
    ScoreDocument typedNested;
    typedNested.beginGroup(0, 0, '[');
    std::size_t pipaCaret = 1;
    require(typedNested.insertPipaChord(0, 0, pipaCaret),
            "a pipa beat accepts a nested chord insertion");
    require(typedNested.insertGroupNote(0, 0, pipaCaret, 'a') &&
                typedNested.insertGroupNote(0, 0, pipaCaret, 's'),
            "nested pipa chord accepts multiple notes");
    require(typedNested.beat(0, 0) == "[(AS)]",
            "nested pipa chord typing preserves its delimiters");

    ScoreDocument groupBackspace;
    groupBackspace.beginGroup(0, 0, '(');
    std::size_t groupCaret = 1;
    require(groupBackspace.eraseGroupCharacterBefore(0, 0, groupCaret) &&
                groupBackspace.beat(0, 0).empty(),
            "backspace clears an empty pair of brackets while editing it");
    groupBackspace.beginGroup(0, 0, '[');
    groupCaret = 1;
    require(groupBackspace.insertGroupNote(0, 0, groupCaret, 'a') &&
                groupBackspace.eraseGroupCharacterBefore(0, 0, groupCaret) &&
                groupBackspace.beat(0, 0) == "[]" && groupCaret == 1,
            "backspace removes the preceding group character and moves the inner caret");

    auto linear = ScoreDocument::fromText("ASDF/");
    linear.insertBeat(1, "Q");
    require(linear.beat(0, 0) == "A" && linear.beat(0, 1) == "Q" &&
                linear.beat(0, 2) == "S" && linear.beat(0, 3) == "D" &&
                linear.beat(1, 0) == "F",
            "insertion shifts all following beats without overwriting them");
    linear.eraseBeats(1, 2);
    require(linear.beat(0, 0) == "A" && linear.beat(0, 1) == "D" &&
                linear.beat(0, 2) == "F" && linear.beat(0, 3).empty(),
            "erasing a range pulls later beats forward");
    linear.clearBeats(0, 2);
    require(linear.beat(0, 0).empty() && linear.beat(0, 1).empty() &&
                linear.beat(0, 2) == "F",
            "clearing a range leaves following beats in place");

    std::cout << "Score document tests passed.\n";
}
