#pragma once

#include "yuanqin/ui/ScoreDocument.h"

#include <string>
#include <string_view>

namespace yuanqin::ui {

// A two-hand score used by the composition workspace. The on-disk .dtxt
// notation stores each hand in its own R(...) or L(...) block.
class CompositionDocument {
public:
    [[nodiscard]] static CompositionDocument fromText(std::string_view text);
    [[nodiscard]] std::string toText() const;
    [[nodiscard]] ScoreDocument mergedScore() const;

    ScoreDocument& right() noexcept;
    ScoreDocument& left() noexcept;
    [[nodiscard]] const ScoreDocument& right() const noexcept;
    [[nodiscard]] const ScoreDocument& left() const noexcept;

private:
    ScoreDocument right_;
    ScoreDocument left_;
};

}  // namespace yuanqin::ui
