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

    std::cout << "Score document tests passed.\n";
}
