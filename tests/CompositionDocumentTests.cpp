#include "yuanqin/ui/CompositionDocument.h"

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
    require(merged.beat(0, 3) == "[(AW)S]",
            "a note paired with a pipa arpeggio joins its first arpeggio step");

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

    std::cout << "Composition document tests passed.\n";
}
