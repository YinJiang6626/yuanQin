#include "yuanqin/core/Score.h"

#include <utility>

namespace yuanqin::core {

Score::Score(std::vector<Tick> ticks) : ticks_(std::move(ticks)) {}

const std::vector<Tick>& Score::ticks() const noexcept {
    return ticks_;
}

std::size_t Score::tickCount() const noexcept {
    return ticks_.size();
}

bool Score::empty() const noexcept {
    return ticks_.empty();
}

bool Score::hasNotes() const noexcept {
    for (const auto& tick : ticks_) {
        if (tick && !tick->notes.empty()) {
            return true;
        }
    }
    return false;
}

}  // namespace yuanqin::core
