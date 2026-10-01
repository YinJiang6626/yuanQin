#include "yuanqin/core/ScoreParser.h"
#include "yuanqin/playback/MusicPlayer.h"

#include <cstdlib>
#include <iostream>
#include <string_view>
#include <vector>

namespace {

class RecordingSender final : public yuanqin::playback::IKeySender {
public:
    bool sendKey(char note) override {
        notes.push_back(note);
        return true;
    }

    bool sendChord(std::span<const char> chord) override {
        chords.emplace_back(chord.begin(), chord.end());
        notes.insert(notes.end(), chord.begin(), chord.end());
        return true;
    }

    void releaseAll() noexcept override {
        ++releaseCount;
    }

    std::vector<char> notes;
    std::vector<std::vector<char>> chords;
    int releaseCount{};
};

void require(bool condition, std::string_view message) {
    if (!condition) {
        std::cerr << "FAILED: " << message << '\n';
        std::exit(1);
    }
}

}  // namespace

int main() {
    using yuanqin::core::ScoreParser;
    using yuanqin::playback::MusicPlayer;
    using yuanqin::playback::PlaybackOptions;
    using yuanqin::playback::PlaybackStatus;

    MusicPlayer player;
    RecordingSender sender;
    const auto score = ScoreParser::parseText("ASDF/").score;
    PlaybackOptions options;
    options.bpm = 1.0e9;
    options.startTick = 2;
    const auto result = player.play(score, sender, options);
    require(result.status == PlaybackStatus::Completed, "playback from a later tick completes");
    require(sender.notes == std::vector<char>({'D', 'F'}),
            "startTick skips notes before the requested position");
    require(sender.releaseCount == 1, "completed playback releases all keys");

    RecordingSender chordSender;
    const auto chordScore = ScoreParser::parseText("(AS)   /").score;
    options.startTick = 0;
    const auto chordResult = player.play(chordScore, chordSender, options);
    require(chordResult.status == PlaybackStatus::Completed,
            "chord playback completes");
    require(chordSender.chords == std::vector<std::vector<char>>{{'A', 'S'}},
            "a chord is sent as one simultaneous backend operation");

    RecordingSender pipaSender;
    std::vector<std::size_t> progress;
    const auto pipaScore = ScoreParser::parseText("[AS]   /").score;
    options.startTick = 0;
    const auto pipaResult = player.play(
        pipaScore, pipaSender, options, {},
        [&progress](std::size_t tick) { progress.push_back(tick); });
    require(pipaResult.status == PlaybackStatus::Completed, "arpeggio playback completes");
    require(progress == std::vector<std::size_t>({0, 1, 2, 3}),
            "arpeggio reports every occupied minimum interval");

    RecordingSender nestedPipaSender;
    const auto nestedPipaScore = ScoreParser::parseText("[(ASD)ADF]   /").score;
    const auto nestedPipaResult = player.play(nestedPipaScore, nestedPipaSender, options);
    require(nestedPipaResult.status == PlaybackStatus::Completed,
            "pipa chord playback completes");
    require(nestedPipaSender.chords == std::vector<std::vector<char>>{
                {'A', 'S', 'D'}, {'A'}, {'D'}, {'F'}},
            "a pipa chord is emitted as one simultaneous step before individual notes");

    RecordingSender cancelledSender;
    const auto cancelled = player.play(score, cancelledSender, options, [] { return true; });
    require(cancelled.status == PlaybackStatus::Cancelled && cancelledSender.notes.empty(),
            "cancelled playback sends no notes");
    require(cancelledSender.releaseCount == 1, "cancelled playback releases all keys");

    std::cout << "Music player tests passed.\n";
}
