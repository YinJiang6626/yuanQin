#include "yuanqin/core/ScoreParser.h"

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

char firstNote(const yuanqin::core::Tick& tick) {
    require(tick.has_value(), "expected a note event");
    require(!tick->notes.empty(), "expected an event containing notes");
    return tick->notes.front();
}

}  // namespace

int main() {
    using yuanqin::core::PlayStyle;
    using yuanqin::core::ScoreParser;

    const auto singleNotes = ScoreParser::parseText("aBcD/");
    require(singleNotes.success, "simple score parses");
    require(singleNotes.score.tickCount() == 4, "a measure contains four ticks");
    require(singleNotes.score.hasNotes(), "notes are detected");
    require(firstNote(singleNotes.score.ticks()[0]) == 'A', "notes normalize to uppercase");
    require(firstNote(singleNotes.score.ticks()[1]) == 'B', "second note is preserved");
    require(firstNote(singleNotes.score.ticks()[2]) == 'C', "third note is preserved");
    require(firstNote(singleNotes.score.ticks()[3]) == 'D', "fourth note is preserved");

    const auto grouped = ScoreParser::parseText("(AZ)[QW]J /");
    require(grouped.score.tickCount() == 4, "grouped score has four ticks");
    require(grouped.score.ticks()[0]->style == PlayStyle::Chord, "parentheses make a chord");
    require(grouped.score.ticks()[0]->notes.size() == 2, "chord retains all notes");
    require(grouped.score.ticks()[1]->style == PlayStyle::Arpeggio, "brackets make an arpeggio");
    require(firstNote(grouped.score.ticks()[2]) == 'J', "note after group is retained");
    require(!grouped.score.ticks()[3], "space creates a rest");

    const auto nestedPipa = ScoreParser::parseText("[(ASD)ADF]   /");
    require(nestedPipa.success, "pipa chords parse successfully");
    const auto& nestedEvent = *nestedPipa.score.ticks()[0];
    require(nestedEvent.style == PlayStyle::Arpeggio,
            "nested notation remains an arpeggio event");
    require(nestedEvent.arpeggioSteps.size() == 4,
            "a pipa chord plus three notes creates four playback steps");
    require(nestedEvent.arpeggioSteps[0] == std::vector<char>({'A', 'S', 'D'}),
            "the first pipa step preserves its chord");
    require(nestedEvent.arpeggioSteps[1] == std::vector<char>({'A'}) &&
                nestedEvent.arpeggioSteps[3] == std::vector<char>({'F'}),
            "remaining pipa notes stay sequential");

    const auto pipaMeasure = ScoreParser::parseText("[XVAF]   /");
    require(pipaMeasure.score.ticks()[0]->style == PlayStyle::Arpeggio, "pipa event is retained");
    require(!pipaMeasure.score.ticks()[1] && !pipaMeasure.score.ticks()[2] &&
                !pipaMeasure.score.ticks()[3],
            "spaces after pipa are rests");

    const auto invalidNote = ScoreParser::parseText("K/");
    require(!invalidNote.diagnostics.empty(), "unsupported letter reports a diagnostic");
    require(!invalidNote.score.ticks()[0], "unsupported letter is not played");
    require(!invalidNote.score.hasNotes(), "a score with no valid notes is not playable");

    const auto editorSnippet = ScoreParser::parseText("QWER");
    require(editorSnippet.score.tickCount() == 4, "final slash is optional for editor text");

    std::cout << "Score parser tests passed.\n";
}
