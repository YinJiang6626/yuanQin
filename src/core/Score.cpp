#include "yuanqin/core/Score.h"

#include <algorithm>
#include <utility>

namespace yuanqin::core {

Score::Score(std::vector<Tick> ticks) : ticks_(std::move(ticks)) {}

const std::vector<Tick>& Score::ticks() const noexcept {
    return ticks_;
}

std::size_t Score::tickCount() const noexcept {
    return ticks_.size();
}

std::size_t Score::playbackTickCount() const noexcept {
    std::size_t result = ticks_.size();
    for (std::size_t index = 0; index < ticks_.size(); ++index) {
        const auto& tick = ticks_[index];
        if (tick && tick->style == PlayStyle::Hold) {
            result = std::max(result, index + std::max<std::size_t>(1, tick->holdTicks));
        }
    }
    return result;
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
