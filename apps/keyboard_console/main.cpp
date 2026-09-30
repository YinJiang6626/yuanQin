#include "yuanqin/core/ScoreParser.h"
#include "yuanqin/playback/MusicPlayer.h"
#include "yuanqin/playback/Win32KeySender.h"

#include <conio.h>

#include <chrono>
#include <cctype>
#include <iostream>
#include <limits>
#include <string>

namespace {

void printDiagnostics(const yuanqin::core::ParseResult& result) {
    for (const auto& diagnostic : result.diagnostics) {
        std::cout << "提示（位置 " << diagnostic.offset << "）：" << diagnostic.message << '\n';
    }
}

}  // namespace

int main() {
    using namespace std::chrono_literals;

    yuanqin::playback::Win32KeySender sender;
    yuanqin::playback::MusicPlayer player;

    std::cout << "原神竖琴自动弹奏（Windows 键盘模式）\n";
    while (true) {
        std::cout << "按任意键载入乐谱，按 G 退出。\n";
        if (std::tolower(static_cast<unsigned char>(_getch())) == 'g') {
            break;
        }

        std::cout << "乐谱路径：";
        std::string path;
        std::getline(std::cin >> std::ws, path);
        const auto parseResult = yuanqin::core::ScoreParser::parseFile(path);
        printDiagnostics(parseResult);
        if (!parseResult.success || !parseResult.score.hasNotes()) {
            std::cout << "乐谱不可播放，请重新载入。\n";
            continue;
        }

        std::cout << "BPM：";
        double bpm = 0.0;
        if (!(std::cin >> bpm)) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            std::cout << "BPM 必须是数字。\n";
            continue;
        }

        std::cout << "按空格键后将在 10 秒后开始，请在此期间切换到游戏窗口。\n";
        while (_getch() != ' ') {}

        const yuanqin::playback::PlaybackOptions options{.bpm = bpm, .startDelay = 10s};
        const auto playbackResult = player.play(parseResult.score, sender, options);
        if (playbackResult.status == yuanqin::playback::PlaybackStatus::Completed) {
            std::cout << "演奏完成。\n";
        } else {
            std::cout << "演奏未完成（状态码：" << static_cast<int>(playbackResult.status) << "）。\n";
        }
    }
}
