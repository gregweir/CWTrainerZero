#pragma once
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>
#include <vector>

namespace cw {
constexpr int kSampleRate = 48000;
constexpr int kCharacterWpm = 18;
constexpr int kToneHz = 600;
constexpr int kMinEffectiveWpm = 5;
constexpr int kMaxEffectiveWpm = 18;
constexpr std::string_view kLessonOrder = "KMURESNAPTLWIJZFOYVGQHDBCX";
constexpr int kMinLetters = 2;
constexpr int kMaxLetters = 26;
constexpr int kMinGroup = 1;
constexpr int kMaxGroup = 4;
constexpr std::string_view kNumbers = "0123456789";
constexpr std::string_view kPunctuation = ".,?/=";
enum class Practice { letters, numbers, punctuation };

struct Segment {
    bool tone;
    std::uint64_t first;
    std::uint64_t end;  // Exclusive.
};
std::vector<Segment> build_timeline(std::string_view text, int effective_wpm, bool trailing_word_gap = false);
std::vector<std::int16_t> render_audio(std::string_view text, int effective_wpm);
void write_wav(const std::filesystem::path& path, const std::vector<std::int16_t>& pcm);

struct Stats {
    int effective_wpm = 8;
    unsigned attempts = 0;
    unsigned correct = 0;
    int letters = kMinLetters;
    Practice practice = Practice::letters;
    int group = 2;
    unsigned char_attempts = 0;
    unsigned char_correct = 0;
};
Stats load_stats(const std::filesystem::path& file);
Stats load_profile(const std::filesystem::path& file);
bool save_stats(const std::filesystem::path& file, Stats stats);
std::filesystem::path stats_file();

class Lesson {
public:
    explicit Lesson(Stats stats = {}, unsigned seed = std::random_device{}());
    enum class Phase { ready, playing, answer, checked };
    Phase phase() const noexcept { return phase_; }
    const std::string& prompt() const noexcept { return prompt_; } // Audio/readout only after Check.
    const std::string& answer() const noexcept { return answer_; }
    const Stats& stats() const noexcept { return stats_; }
    bool correct() const noexcept { return phase_ == Phase::checked && answer_ == prompt_; }

    bool start_audio();
    void audio_failed();
    void audio_finished();
    bool enter(char value);
    bool erase();
    bool check();
    bool next();
    bool adjust_effective(int delta);
    bool select_letters(int count);
    bool set_practice(Practice practice, int group);
    unsigned matched() const noexcept;
    std::string_view pool() const;
    std::string_view alphabet() const { return kLessonOrder.substr(0, stats_.letters); }
    char newest() const { return alphabet().back(); }
private:
    std::mt19937 rng_;
    Stats stats_;
    Phase phase_ = Phase::ready;
    Phase before_audio_ = Phase::ready;
    std::string prompt_;
    std::string answer_;
    void make_prompt();
};
} // namespace cw
