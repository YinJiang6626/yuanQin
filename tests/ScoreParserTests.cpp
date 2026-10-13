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

    const auto pipaWithHold = ScoreParser::parseText("[{A---}QWER]/");
    const auto& pipaWithHoldEvent = *pipaWithHold.score.ticks()[0];
    require(pipaWithHoldEvent.style == PlayStyle::Arpeggio &&
                pipaWithHoldEvent.arpeggioSteps.size() == 4 &&
                pipaWithHoldEvent.embeddedHolds.size() == 1 &&
                pipaWithHoldEvent.embeddedHolds[0].notes == std::vector<char>({'A'}) &&
                pipaWithHoldEvent.embeddedHolds[0].holdTicks == 4,
            "a pipa can carry an independent long hold in the same score cell");

    const auto pipaMeasure = ScoreParser::parseText("[XVAF]   /");
    require(pipaMeasure.score.ticks()[0]->style == PlayStyle::Arpeggio, "pipa event is retained");
    require(!pipaMeasure.score.ticks()[1] && !pipaMeasure.score.ticks()[2] &&
                !pipaMeasure.score.ticks()[3],
            "spaces after pipa are rests");
    require(pipaMeasure.score.ticks()[0]->adaptiveArpeggioSpanTicks == 4,
            "an adaptive pipa precomputes its span from following empty beats");

    const auto stoppedPipa = ScoreParser::parseText("[AS]Q(|W) /");
    require(stoppedPipa.score.ticks()[2]->arpeggioStop,
            "a stop marker inside a chord is retained without suppressing its notes");
    require(stoppedPipa.score.ticks()[0]->adaptiveArpeggioSpanTicks == 2,
            "an attached stop marker ends the pipa before its own chord cell");

    const auto nextPipa = ScoreParser::parseText("[A] [S] /");
    require(nextPipa.score.ticks()[0]->adaptiveArpeggioSpanTicks == 2,
            "the following pipa bounds an unstopped adaptive pipa span");

    const auto stoppedAfterFourCells = ScoreParser::parseText("[ASDFG]    |");
    require(stoppedAfterFourCells.score.ticks()[0]->adaptiveArpeggioSpanTicks == 5,
            "a standalone stop marker ends the pipa without adding a sounding cell");
    const auto stoppedAfterTwoCells = ScoreParser::parseText("[ASDFG]  |");
    require(stoppedAfterTwoCells.score.ticks()[0]->adaptiveArpeggioSpanTicks == 3,
            "two rests followed by a standalone stop make a three-cell pipa span");
    const auto stoppedInsideChord = ScoreParser::parseText("[ASDFG]  (A|)");
    require(stoppedInsideChord.score.ticks()[0]->adaptiveArpeggioSpanTicks == 3,
            "a stop marker attached to a chord does not add that chord cell to the pipa span");
    const auto stopWithConcurrentNote = ScoreParser::parseText("[ASDFG]QW(|E)");
    require(stopWithConcurrentNote.score.ticks()[0]->adaptiveArpeggioSpanTicks == 3,
            "a note sharing a stop-marker cell plays outside the pipa duration");
    const auto unstoppedPipa = ScoreParser::parseText("[ASDFG]  A");
    require(unstoppedPipa.score.ticks()[0]->adaptiveArpeggioSpanTicks == 3,
            "without a stop marker, only directly following rests extend a pipa span");

    const auto held = ScoreParser::parseText("{W-}{(ASD)---}  /");
    require(held.success, "long-hold notation parses successfully");
    require(held.score.ticks()[0]->style == PlayStyle::Hold,
            "braces make a long-hold event");
    require(held.score.ticks()[0]->notes == std::vector<char>({'W'}) &&
                held.score.ticks()[0]->holdTicks == 2,
            "one dash extends a held note to two minimum units");
    require(held.score.ticks()[1]->notes == std::vector<char>({'A', 'S', 'D'}) &&
                held.score.ticks()[1]->holdTicks == 4,
            "a held chord and its duration are retained");

    const auto embeddedHoldFirst = ScoreParser::parseText("({W--}A)/");
    const auto embeddedHoldLast = ScoreParser::parseText("(A{W--})/");
    require(embeddedHoldFirst.score.ticks()[0]->notes == std::vector<char>({'A'}) &&
                embeddedHoldFirst.score.ticks()[0]->embeddedHolds.size() == 1 &&
                embeddedHoldFirst.score.ticks()[0]->embeddedHolds[0].notes ==
                    std::vector<char>({'W'}) &&
                embeddedHoldFirst.score.ticks()[0]->embeddedHolds[0].holdTicks == 3,
            "a chord can contain an independently held note");
    require(embeddedHoldLast.score.ticks()[0]->notes == embeddedHoldFirst.score.ticks()[0]->notes &&
                embeddedHoldLast.score.ticks()[0]->embeddedHolds[0].notes ==
                    embeddedHoldFirst.score.ticks()[0]->embeddedHolds[0].notes &&
                embeddedHoldLast.score.ticks()[0]->embeddedHolds[0].holdTicks ==
                    embeddedHoldFirst.score.ticks()[0]->embeddedHolds[0].holdTicks,
            "embedded holds have the same meaning regardless of their order in a chord");

    const auto parallelPipaAndHold = ScoreParser::parseText("({W------}[ASDFGH])/");
    const auto& parallelPipaAndHoldEvent = *parallelPipaAndHold.score.ticks()[0];
    require(parallelPipaAndHoldEvent.style == PlayStyle::Arpeggio &&
                parallelPipaAndHoldEvent.arpeggioSteps.size() == 6 &&
                parallelPipaAndHoldEvent.embeddedHolds.size() == 1 &&
                parallelPipaAndHoldEvent.embeddedHolds[0].notes == std::vector<char>({'W'}) &&
                parallelPipaAndHoldEvent.embeddedHolds[0].holdTicks == 7,
            "a parenthesized parallel group recognizes a pipa and long hold together");

    const auto parallelChordAndPipa = ScoreParser::parseText("((QW)[ASDF])/");
    const auto& parallelChordAndPipaEvent = *parallelChordAndPipa.score.ticks()[0];
    require(parallelChordAndPipaEvent.style == PlayStyle::Arpeggio &&
                parallelChordAndPipaEvent.arpeggioSteps.size() == 4 &&
                parallelChordAndPipaEvent.arpeggioSteps[0] == std::vector<char>({'A', 'Q', 'W'}),
            "a normal chord is simultaneous with the first step of its nested pipa");

    const auto nestedPipaHold = ScoreParser::parseText("[({W--}A)S]/");
    require(nestedPipaHold.score.ticks()[0]->embeddedHolds.size() == 1 &&
                nestedPipaHold.score.ticks()[0]->embeddedHolds[0].notes ==
                    std::vector<char>({'W'}) &&
                nestedPipaHold.score.ticks()[0]->embeddedHolds[0].holdTicks == 3,
            "a pipa chord continues to recognize a long hold nested inside its first step");

    const auto endingHold = ScoreParser::parseText("ABC{W---}/");
    require(endingHold.score.tickCount() == 4 && endingHold.score.playbackTickCount() == 7,
            "a final long hold extends the score timeline through its release tick");

    const auto invalidNote = ScoreParser::parseText("K/");
    require(!invalidNote.diagnostics.empty(), "unsupported letter reports a diagnostic");
    require(!invalidNote.score.ticks()[0], "unsupported letter is not played");
    require(!invalidNote.score.hasNotes(), "a score with no valid notes is not playable");

    const auto editorSnippet = ScoreParser::parseText("QWER");
    require(editorSnippet.score.tickCount() == 4, "final slash is optional for editor text");

    std::cout << "Score parser tests passed.\n";
}
