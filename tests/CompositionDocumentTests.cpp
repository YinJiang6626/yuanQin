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
    require(merged.beat(0, 3) == "(ASW)", "simultaneous hands merge into one chord");

    std::cout << "Composition document tests passed.\n";
}
