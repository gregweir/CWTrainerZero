#include "trainer.hpp"
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <unistd.h>

#define CHECK(condition) do { if (!(condition)) throw std::runtime_error(#condition); } while (false)

int main() try {
    using namespace cw;
    const auto paris = build_timeline("PARIS", 8, true);
    CHECK(paris.back().end == 360000);
    const auto repeated = build_timeline("PARIS PARIS", 8, true);
    CHECK(repeated.back().end == 720000);
    CHECK(build_timeline("PARIS", 18, true).back().end == 160000);
    const auto km = build_timeline("KM", 8);
    const auto normal = build_timeline("KM", 18);
    CHECK(km.size() == 9);
    CHECK(km[0].end - km[0].first == 9600);
    CHECK(km[1].end - km[1].first == 3200);
    CHECK(km[2].end - km[2].first == 3200);
    CHECK(km[3].end - km[3].first == 3200);
    CHECK(km[4].end - km[4].first == 9600);
    CHECK(km[5].end - km[5].first > normal[5].end - normal[5].first);
    CHECK(km[6].end - km[6].first == 9600 && km[7].end - km[7].first == 3200);
    for (std::size_t i = 1; i < km.size(); ++i) CHECK(km[i].first == km[i - 1].end);
    auto audio = render_audio("KM", 8);
    CHECK(audio.size() == km.back().end + 4800);
    for (const auto& s : km) {
        CHECK(audio[s.first + 2400] == 0);
        CHECK(audio[s.end + 2399] == 0);
        const auto first = audio.begin() + s.first + 2400;
        const auto last = audio.begin() + s.end + 2400;
        if (s.tone) {
            const auto peak = *std::max_element(first, last);
            CHECK(peak > 6000 && peak <= 6553);
        } else {
            CHECK(std::all_of(first, last, [](auto value){ return value == 0; }));
        }
    }
    // A 600 Hz period occupies 80 samples exactly at 48 kHz.
    CHECK(audio[2400 + 360] == audio[2400 + 440]);
    CHECK(audio[2400 + 380] == audio[2400 + 460]);

    const auto code = [](char c) {
        std::string marks;
        for (const auto& s : build_timeline(std::string(1, c), 8))
            if (s.tone) marks += s.end - s.first == 3200 ? '.' : '-';
        return marks;
    };
    const std::string_view numerals[]{"-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."};
    for (std::size_t i = 0; i < kNumbers.size(); ++i) CHECK(code(kNumbers[i]) == numerals[i]);
    const std::string_view punctuation[]{".-.-.-", "--..--", "..--..", "-..-.", "-...-"};
    for (std::size_t i = 0; i < kPunctuation.size(); ++i) CHECK(code(kPunctuation[i]) == punctuation[i]);

    Lesson lesson({}, 7);
    CHECK(lesson.stats().effective_wpm == 8);
    CHECK(!lesson.enter('K') && !lesson.check() && !lesson.next());
    CHECK(lesson.start_audio() && !lesson.start_audio() && !lesson.adjust_effective(1));
    lesson.audio_failed();
    CHECK(lesson.phase() == Lesson::Phase::ready);
    CHECK(lesson.start_audio()); lesson.audio_finished();
    CHECK(!lesson.check() && !lesson.enter('1'));
    const auto prompt = lesson.prompt();
    CHECK(lesson.enter(prompt.front()) && lesson.erase() && !lesson.erase());
    CHECK(lesson.enter(prompt.front()));
    CHECK(lesson.start_audio()); lesson.audio_finished();
    CHECK(lesson.answer().size() == 1 && lesson.prompt() == prompt);
    CHECK(lesson.enter(prompt.back()) && !lesson.enter('M'));
    CHECK(lesson.check() && lesson.correct() && !lesson.check());
    CHECK(lesson.stats().attempts == 1 && lesson.stats().correct == 1);
    CHECK(lesson.next() && lesson.prompt() != prompt && lesson.answer().empty());
    CHECK(lesson.adjust_effective(-99) && lesson.stats().effective_wpm == 5);
    CHECK(!lesson.adjust_effective(-1) && lesson.adjust_effective(99) && lesson.stats().effective_wpm == 18);
    CHECK(lesson.start_audio()); lesson.audio_finished();
    for (const char c : lesson.prompt()) CHECK(lesson.enter(c == 'K' ? 'M' : 'K'));
    CHECK(lesson.check() && !lesson.correct());
    CHECK(lesson.stats().attempts == 2 && lesson.stats().correct == 1);
    CHECK(lesson.alphabet() == "KM" && lesson.newest() == 'M');
    CHECK(lesson.select_letters(3));
    CHECK(lesson.alphabet() == "KMU" && lesson.newest() == 'U' && lesson.answer().empty());
    std::string given;
    for (int count = kMinLetters; count <= kMaxLetters; ++count) {
        Lesson level({8, 0, 0, count}, static_cast<unsigned>(count));
        CHECK(level.alphabet().size() == static_cast<std::size_t>(count));
        CHECK(level.alphabet().find(level.prompt()[0]) != std::string_view::npos);
        CHECK(level.alphabet().find(level.prompt()[1]) != std::string_view::npos);
        if (count == kMaxLetters) given = level.alphabet();
    }
    std::sort(given.begin(), given.end());
    CHECK(given == "ABCDEFGHIJKLMNOPQRSTUVWXYZ");
    for (char c = 'A'; c <= 'Z'; ++c) CHECK(!render_audio(std::string(1, c), 8).empty());
    CHECK(lesson.select_letters(26));
    CHECK(lesson.start_audio() && !lesson.select_letters(2));
    lesson.audio_finished();
    CHECK(lesson.enter('z') && lesson.enter('c') && lesson.answer() == "ZC");
    CHECK(lesson.stats().effective_wpm == 18);
    CHECK(lesson.select_letters(2) && lesson.alphabet() == "KM");
    CHECK(lesson.phase() == Lesson::Phase::ready && lesson.answer().empty());
    CHECK(lesson.stats().attempts == 2 && lesson.stats().correct == 1);

    CHECK(lesson.stats().char_correct == 2 && lesson.stats().char_attempts == 4);
    for (const auto set : {Practice::letters, Practice::numbers, Practice::punctuation}) {
        for (int group = kMinGroup; group <= kMaxGroup; ++group) {
            Lesson practice({8, 18, 11, 12, set, group}, 23);
            const auto original = practice.prompt();
            CHECK(original.size() == static_cast<std::size_t>(group));
            CHECK(original.find_first_not_of(practice.pool()) == std::string::npos);
            CHECK(practice.start_audio());
            CHECK(!practice.set_practice(set, 2) && !practice.set_practice(set, 3));
            practice.audio_finished();
            CHECK(!practice.enter('#'));
            for (int i = 0; i < group; ++i) {
                const auto pool = practice.pool();
                const char guess = i == group - 1 ? pool[(pool.find(original[i]) + 1) % pool.size()] : original[i];
                CHECK(practice.enter(guess));
            }
            CHECK(practice.check() && !practice.check() && !practice.correct());
            CHECK(practice.matched() == static_cast<unsigned>(group - 1));
            CHECK(practice.stats().char_attempts == static_cast<unsigned>(group));
            CHECK(practice.stats().char_correct == practice.matched());
            CHECK(practice.stats().attempts == 19 && practice.stats().correct == 11);
            CHECK(practice.next() && practice.prompt() != original);
            CHECK(!practice.set_practice(set, 0));
            CHECK(practice.stats().letters == 12);
        }
    }
    CHECK(lesson.set_practice(Practice::numbers, 4) && lesson.pool() == kNumbers);
    CHECK(!lesson.select_letters(kMaxLetters) && lesson.stats().letters == 2);
    CHECK(lesson.start_audio()); lesson.audio_finished();
    CHECK(lesson.enter('0') && lesson.enter('9') && !lesson.enter('M'));
    CHECK(lesson.set_practice(Practice::punctuation, 1) && lesson.pool() == kPunctuation && lesson.answer().empty());
    CHECK(lesson.start_audio()); lesson.audio_finished();
    CHECK(!lesson.enter('0') && lesson.enter('='));

    auto temp = std::string("/tmp/cardputer-cw-test-XXXXXX");
    CHECK(mkdtemp(temp.data()) != nullptr);
    const auto path = std::filesystem::path(temp) / "settings" / "stats";
    CHECK(load_stats(path).effective_wpm == 8);
    std::ofstream(path.parent_path().parent_path() / "legacy") << "CW1 11 54 30\n";
    const auto legacy = load_stats(path.parent_path().parent_path() / "legacy");
    CHECK(legacy.effective_wpm == 11 && legacy.attempts == 54 && legacy.correct == 30 && legacy.letters == 2);
    CHECK(save_stats(path, lesson.stats()));
    CHECK(load_stats(path).effective_wpm == 18 && load_stats(path).correct == 1);
    CHECK(!save_stats(path, {18, 1, 2}) && load_stats(path).attempts == 2);
    CHECK(save_stats(path, {7, 1, 1, 26}) && load_stats(path).effective_wpm == 7);
    CHECK(load_stats(path).letters == 26);
    CHECK(!save_stats(path, {7, 1, 1, 27}));
    CHECK(load_stats(path).letters == 26);
    std::ofstream(path) << "CW1 -20 9 3\n";
    CHECK(load_stats(path).effective_wpm == 8 && load_stats(path).attempts == 0);
    const auto newfile = std::filesystem::path(temp) / "stats-v3";
    const auto oldfile = std::filesystem::path(temp) / "stats";
    for (const auto magic : {"CW1 12 54 30", "CW2 12 54 30 16"}) {
        std::ofstream(oldfile) << magic << '\n';
        const auto migrated = load_profile(newfile);
        CHECK(migrated.effective_wpm == 12 && migrated.attempts == 54 && migrated.correct == 30);
        CHECK(migrated.letters == (std::string_view(magic).substr(0, 3) == "CW1" ? 2 : 16));
        CHECK(migrated.group == 2 && migrated.practice == Practice::letters);
        CHECK(migrated.char_attempts == 0 && migrated.char_correct == 0);
        CHECK(!std::filesystem::exists(newfile));
    }
    CHECK(save_stats(newfile, {7, 55, 31, 16, Practice::punctuation, 4, 4, 3}));
    CHECK(load_profile(newfile).group == 4 && load_profile(newfile).char_correct == 3);
    CHECK(load_profile(newfile).practice == Practice::punctuation);
    CHECK(load_stats(oldfile).effective_wpm == 12 && load_stats(oldfile).attempts == 54);
    std::ofstream(newfile) << "CW4 future data\n";
    CHECK(!save_stats(newfile, {}));
    CHECK(load_profile(newfile).attempts == 0);
    std::string original;
    std::getline(std::ifstream(newfile), original);
    CHECK(original == "CW4 future data");
    write_wav(std::filesystem::path(temp) / "km.wav", audio);
    CHECK(std::filesystem::file_size(std::filesystem::path(temp) / "km.wav") == 44 + audio.size() * 2);
    std::filesystem::remove_all(temp);
    std::cout << "Morse timing, audio samples, lesson gating, replay, scores and persistence passed\n";
    return 0;
} catch (const std::exception& err) {
    std::cerr << "FAIL: " << err.what() << '\n';
    return 1;
}
