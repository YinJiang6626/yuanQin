#include "yuanqin/ui/CompositionDocument.h"
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

}  // namespace

int main() {
    using yuanqin::ui::CompositionDocument;

    const auto document = CompositionDocument::fromText("R(Q  [AS])\nL( Z W)\n");
    require(document.right().beat(0, 0) == "Q", "right hand is parsed independently");
    require(document.left().beat(0, 1) == "Z", "left hand is parsed independently");
    require(document.right().beat(0, 3) == "[AS]", "nested groups survive dtxt parsing");
    require(document.toText() == "R(Q  [AS]/)\nL( Z W/)\n", "dtxt serializes both hands");

    const auto merged = document.mergedScore();
    require(merged.beat(0, 0) == "Q", "a solo right-hand beat stays playable");
    require(merged.beat(0, 1) == "Z", "a solo left-hand beat stays playable");
    require(merged.beat(0, 2).empty(), "matching rests remain rests");
    require(merged.beat(0, 3) == "(W[AS])",
            "an ordinary note and pipa retain their independent parallel forms");

    const auto pairedPipas = CompositionDocument::fromText(
        "R([ASDF]/)\nL([QW(ER)]/)\n");
    require(pairedPipas.mergedScore().beat(0, 0) == "[(AQ)(SW)(DER)F]",
            "two pipa arpeggios merge step by step in order");

    const auto duplicateNotes = CompositionDocument::fromText(
        "R(A[AS]/)\nL(A[AQ]/)\n");
    const auto duplicateMerged = duplicateNotes.mergedScore();
    require(duplicateMerged.beat(0, 0) == "A",
            "ordinary simultaneous notes are deduplicated");
    require(duplicateMerged.beat(0, 1) == "[A(SQ)]",
            "matching pipa steps are deduplicated while preserving order");

    const auto holdAndPipa = CompositionDocument::fromText(
        "R({A---}/)\nL([QWER]/)\n");
    require(holdAndPipa.mergedScore().beat(0, 0) == "({A---}[QWER])",
            "a right-hand hold and left-hand pipa use an explicit parallel group");

    const auto chordAndPipa = CompositionDocument::fromText(
        "R((QW)/)\nL([ASDFGH]/)\n");
    require(chordAndPipa.mergedScore().beat(0, 0) == "((QW)[ASDFGH])",
            "a chord and pipa retain both bracketed streams in a parallel group");

    const auto duplicateChordAndPipa = CompositionDocument::fromText(
        "R((QW)/)\nL([WAS]/)\n");
    require(duplicateChordAndPipa.mergedScore().beat(0, 0) == "(Q[WAS])",
            "a duplicate ordinary chord note is removed when the pipa already supplies it");

    const auto pipaWithHoldAndPipa = CompositionDocument::fromText(
        "R([{A---}QWER]/)\nL([ZX]/)\n");
    require(pipaWithHoldAndPipa.mergedScore().beat(0, 0) == "[{A---}(QZ)(WX)ER]",
            "an existing pipa-held note survives two-hand pipa step merging");

    const auto chordAndHold = CompositionDocument::fromText(
        "R((AD)/)\nL({(AD)---}/)\n");
    require(chordAndHold.mergedScore().beat(0, 0) == "((AD){(AD)---})",
            "a normal chord and a held chord retain both parallel bracket forms");

    const auto duplicateChordAndHold = CompositionDocument::fromText(
        "R((QW)/)\nL({W------}/)\n");
    require(duplicateChordAndHold.mergedScore().beat(0, 0) == "(Q{W------})",
            "a duplicated note is removed from the ordinary chord beside a direct hold");

    const auto offsetHoldAndPipa = CompositionDocument::fromText(
        "R({A---}   /)\nL( [QWER]  /)\n");
    const auto offsetMerged = offsetHoldAndPipa.mergedScore();
    require(offsetMerged.beat(0, 0) == "{A---}" && offsetMerged.beat(0, 1) == "[QWER]",
            "offset long holds and pipa notes retain their separate score cells");
    const auto offsetPlayback = yuanqin::core::ScoreParser::parseText(offsetMerged.toText()).score;
    require(offsetPlayback.ticks()[0]->style == yuanqin::core::PlayStyle::Hold &&
                offsetPlayback.ticks()[1]->style == yuanqin::core::PlayStyle::Arpeggio,
            "the merged two-hand score parses an offset hold before its pipa");

    std::cout << "Composition document tests passed.\n";
}
