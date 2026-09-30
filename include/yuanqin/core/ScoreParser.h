#pragma once

#include "yuanqin/core/Score.h"

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace yuanqin::core {

struct ParseDiagnostic {
    std::size_t offset{};
    std::string message;
};

struct ParseResult {
    Score score;
    std::vector<ParseDiagnostic> diagnostics;
    bool success{true};
};

// Parses the existing letter-score format. Numeric notation is intentionally ignored.
class ScoreParser {
public:
    [[nodiscard]] static ParseResult parseText(std::string_view text);
    [[nodiscard]] static ParseResult parseFile(const std::filesystem::path& path);
    [[nodiscard]] static bool isPlayableNote(char value) noexcept;
};

}  // namespace yuanqin::core
