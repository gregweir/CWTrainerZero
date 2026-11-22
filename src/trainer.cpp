#include "trainer.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace cw {
namespace {
constexpr unsigned max_score = 999999;
constexpr long double pi = 3.1415926535897932384626433832795L;
constexpr std::array<std::string_view, 26> alphabet{{
    ".-", "-...", "-.-.", "-..", ".", "..-.", "--.", "....", "..", ".---", "-.-", ".-..", "--",
    "-.", "---", ".--.", "--.-", ".-.", "...", "-", "..-", "...-", ".--", "-..-", "-.--", "--.."}};

constexpr std::array<std::string_view, 10> number_codes{{
    "-----", ".----", "..---", "...--", "....-", ".....", "-....", "--...", "---..", "----."}};
constexpr std::array<std::string_view, 5> punctuation_codes{{
    ".-.-.-", "--..--", "..--..", "-..-.", "-...-"}};

std::string_view morse(char c) {
    if (c >= 'A' && c <= 'Z') return alphabet[static_cast<unsigned>(c - 'A')];
    if (const auto i = kNumbers.find(c); i != std::string_view::npos) return number_codes[i];
    if (const auto i = kPunctuation.find(c); i != std::string_view::npos) return punctuation_codes[i];
    throw std::invalid_argument("Unsupported Morse character");
}
int group_size(int value) { return std::clamp(value, kMinGroup, kMaxGroup); }
bool valid_practice(Practice value) { return value >= Practice::letters && value <= Practice::punctuation; }

int wpm(int value) { return std::clamp(value, kMinEffectiveWpm, kMaxEffectiveWpm); }
int letters(int value) { return std::clamp(value, kMinLetters, kMaxLetters); }

void little(std::ostream& out, std::uint32_t value, int bytes) {
    for (int i = 0; i < bytes; ++i) out.put(static_cast<char>((value >> (8 * i)) & 0xff));
}
} // namespace

std::vector<Segment> build_timeline(std::string_view text, int effective_wpm, bool trailing_word_gap) {
    if (text.size() > 256) throw std::invalid_argument("Morse text exceeds 256 bytes");
    const long double dit = 1.2L * kSampleRate / kCharacterWpm;
    // A standard PARIS word contains 31 fixed dit units and 19 units of expanded spacing.
    const long double gap = (60.0L * kSampleRate / wpm(effective_wpm) - 31 * dit) / 19;
    long double cursor = 0;
    std::vector<Segment> plan;
    const auto add = [&](const bool tone, const long double samples) {
        const auto first = static_cast<std::uint64_t>(std::llround(cursor));
        cursor += samples;
        const auto end = static_cast<std::uint64_t>(std::llround(cursor));
        plan.push_back({tone, first, end});
    };
    bool need_gap = false;
    bool word_gap = false;
    for (const char raw : text) {
        if (raw == ' ') {
            if (need_gap) word_gap = true;
            continue;
        }
        const char c = static_cast<char>(std::toupper(static_cast<unsigned char>(raw)));
        const auto code = morse(c);
        if (need_gap) add(false, gap * (word_gap ? 7 : 3));
        for (std::size_t i = 0; i < code.size(); ++i) {
            if (i != 0) add(false, dit);
            add(true, dit * (code[i] == '.' ? 1 : 3));
        }
        need_gap = true;
        word_gap = false;
    }
    if (trailing_word_gap && need_gap) add(false, 7 * gap);
    return plan;
}

std::vector<std::int16_t> render_audio(std::string_view text, int effective_wpm) {
    constexpr std::uint64_t lead = kSampleRate / 20; // Let the playback path settle.
    constexpr std::uint64_t tail = kSampleRate / 20;
    constexpr std::uint64_t ramp = 192; // 4 ms, each edge, included in the symbol duration.
    constexpr long double amplitude = 6553.0L; // 20% digital peak; system volume remains independent.
    const auto plan = build_timeline(text, effective_wpm);
    if (plan.empty()) return {};
    std::vector<std::int16_t> pcm(plan.back().end + lead + tail);
    for (const auto& seg : plan) {
        if (!seg.tone) continue;
        const auto size = seg.end - seg.first;
        for (std::uint64_t i = 0; i < size; ++i) {
            const auto distance = std::min(i, size - 1 - i);
            const auto fade = static_cast<long double>(std::min(distance, ramp)) / ramp;
            const auto envelope = (1 - std::cos(pi * fade)) / 2;
            const auto angle = 2 * pi * kToneHz * i / kSampleRate;
            pcm[lead + seg.first + i] = static_cast<std::int16_t>(std::llround(amplitude * envelope * std::sin(angle)));
        }
    }
    return pcm;
}

void write_wav(const std::filesystem::path& path, const std::vector<std::int16_t>& pcm) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const auto bytes = static_cast<std::uint32_t>(pcm.size() * sizeof(std::int16_t));
    out.write("RIFF", 4); little(out, 36 + bytes, 4); out.write("WAVEfmt ", 8);
    little(out, 16, 4); little(out, 1, 2); little(out, 1, 2);
    little(out, kSampleRate, 4); little(out, kSampleRate * 2, 4);
    little(out, 2, 2); little(out, 16, 2); out.write("data", 4); little(out, bytes, 4);
    for (const auto value : pcm) little(out, static_cast<std::uint16_t>(value), 2);
    out.close();
    if (!out) throw std::runtime_error("Could not write WAV");
}

namespace {
std::optional<Stats> read_stats(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    std::array<char, 97> buf{};
    in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
    if (!in.eof() || in.gcount() == 0) return {};
    std::istringstream fields(std::string(buf.data(), static_cast<std::size_t>(in.gcount())));
    std::string magic, extra;
    Stats stats{};
    if (!(fields >> magic >> stats.effective_wpm >> stats.attempts >> stats.correct)) return {};
    if (magic == "CW2" || magic == "CW3") {
        if (!(fields >> stats.letters)) return {};
    } else if (magic != "CW1") return {};
    if (magic == "CW3") {
        int mode = -1;
        if (!(fields >> mode >> stats.group >> stats.char_attempts >> stats.char_correct)) return {};
        stats.practice = static_cast<Practice>(mode);
    }
    if (fields >> extra || stats.attempts > max_score || stats.correct > stats.attempts ||
        stats.char_attempts > max_score || stats.char_correct > stats.char_attempts ||
        stats.effective_wpm != wpm(stats.effective_wpm) || stats.letters != letters(stats.letters) ||
        !valid_practice(stats.practice) || stats.group != group_size(stats.group)) return {};
    return stats;
}
} // namespace

Stats load_stats(const std::filesystem::path& file) {
    return read_stats(file).value_or(Stats{});
}

Stats load_profile(const std::filesystem::path& file) {
    if (file.empty()) return {};
    std::error_code ec;
    const bool saved = std::filesystem::exists(file, ec);
    if (ec) return {};
    if (saved || file.filename() != "stats-v3") return load_stats(file);
    // Read earlier releases once on migration; never rewrite the legacy file.
    return load_stats(file.parent_path() / "stats");
}

bool save_stats(const std::filesystem::path& file, Stats stats) {
    if (file.empty() || stats.attempts > max_score || stats.correct > stats.attempts ||
        stats.char_attempts > max_score || stats.char_correct > stats.char_attempts ||
        stats.effective_wpm != wpm(stats.effective_wpm) || stats.letters != letters(stats.letters) ||
        !valid_practice(stats.practice) || stats.group != group_size(stats.group)) return false;
    // Do not overwrite an unreadable/corrupt profile or a future schema.
    std::error_code exists_ec;
    if (std::filesystem::exists(file, exists_ec) && !read_stats(file)) return false;
    if (exists_ec) return false;
    std::error_code ec;
    if (!file.parent_path().empty()) std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) return false;
    auto temporary = file.string() + ".tmp.XXXXXX";
    const auto fd = mkstemp(temporary.data());
    if (fd < 0) return false;
    const auto record = "CW3 " + std::to_string(stats.effective_wpm) + " " +
        std::to_string(stats.attempts) + " " + std::to_string(stats.correct) + " " +
        std::to_string(stats.letters) + " " + std::to_string(static_cast<int>(stats.practice)) + " " +
        std::to_string(stats.group) + " " + std::to_string(stats.char_attempts) + " " +
        std::to_string(stats.char_correct) + "\n";
    std::size_t written = 0;
    while (written < record.size()) {
        const auto count = ::write(fd, record.data() + written, record.size() - written);
        if (count <= 0) break;
        written += static_cast<std::size_t>(count);
    }
    const auto complete = written == record.size() && ::fsync(fd) == 0;
    const auto closed = ::close(fd) == 0;
    if (complete && closed) std::filesystem::rename(temporary, file, ec);
    if (!complete || !closed || ec) {
        std::filesystem::remove(temporary, ec);
        return false;
    }
    return true;
}

std::filesystem::path stats_file() {
    if (const auto* xdg = std::getenv("XDG_CONFIG_HOME"); xdg && std::filesystem::path(xdg).is_absolute())
        return std::filesystem::path(xdg) / "cwtrainerzero" / "stats-v3";
    if (const auto* home = std::getenv("HOME"); home && std::filesystem::path(home).is_absolute())
        return std::filesystem::path(home) / ".config" / "cwtrainerzero" / "stats-v3";
    return {};
}

Lesson::Lesson(Stats stats, unsigned seed) : rng_(seed), stats_(stats) {
    stats_.effective_wpm = wpm(stats_.effective_wpm);
    stats_.letters = letters(stats_.letters);
    stats_.attempts = std::min(stats_.attempts, max_score);
    stats_.correct = std::min(stats_.correct, stats_.attempts);
    if (!valid_practice(stats_.practice)) stats_.practice = Practice::letters;
    stats_.group = group_size(stats_.group);
    stats_.char_attempts = std::min(stats_.char_attempts, max_score);
    stats_.char_correct = std::min(stats_.char_correct, stats_.char_attempts);
    make_prompt();
}

std::string_view Lesson::pool() const {
    switch (stats_.practice) {
        case Practice::numbers: return kNumbers;
        case Practice::punctuation: return kPunctuation;
        default: return alphabet();
    }
}

void Lesson::make_prompt() {
    const auto choices = pool();
    auto draw = [&]() { return choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(rng_)]; };
    std::string group;
    do {
        group.resize(static_cast<std::size_t>(stats_.group));
        std::generate(group.begin(), group.end(), draw);
        // Give newly introduced letter sounds useful repetitions without forcing every trial.
        if (stats_.practice == Practice::letters && choices.size() > 2 &&
            choices.size() < kMaxLetters && std::bernoulli_distribution(.5)(rng_))
            group[std::uniform_int_distribution<int>(0, stats_.group - 1)(rng_)] = choices.back();
    } while (group == prompt_);
    prompt_ = std::move(group);
}

bool Lesson::start_audio() {
    if (phase_ == Phase::playing || phase_ == Phase::checked) return false;
    before_audio_ = phase_;
    phase_ = Phase::playing;
    return true;
}

void Lesson::audio_failed() { if (phase_ == Phase::playing) phase_ = before_audio_; }
void Lesson::audio_finished() { if (phase_ == Phase::playing) phase_ = Phase::answer; }

bool Lesson::enter(char value) {
    if (phase_ != Phase::answer || answer_.size() == prompt_.size()) return false;
    value = static_cast<char>(std::toupper(static_cast<unsigned char>(value)));
    if (stats_.practice == Practice::letters ? value < 'A' || value > 'Z'
        : pool().find(value) == std::string_view::npos) return false;
    answer_ += value;
    return true;
}

bool Lesson::erase() {
    if (phase_ != Phase::answer || answer_.empty()) return false;
    answer_.pop_back();
    return true;
}

unsigned Lesson::matched() const noexcept {
    unsigned correct = 0;
    for (std::size_t i = 0; i < answer_.size(); ++i)
        if (answer_[i] == prompt_[i]) ++correct;
    return correct;
}

bool Lesson::check() {
    if (phase_ != Phase::answer || answer_.size() != prompt_.size()) return false;
    phase_ = Phase::checked;
    if (stats_.attempts < max_score) {
        ++stats_.attempts;
        if (answer_ == prompt_) ++stats_.correct;
    }
    const auto size = static_cast<unsigned>(prompt_.size());
    if (stats_.char_attempts <= max_score - size) {
        stats_.char_attempts += size;
        stats_.char_correct += matched();
    }
    return true;
}

bool Lesson::next() {
    if (phase_ != Phase::checked) return false;
    phase_ = Phase::ready;
    answer_.clear();
    make_prompt();
    return true;
}

bool Lesson::adjust_effective(int delta) {
    if (phase_ == Phase::playing) return false;
    const int updated = wpm(stats_.effective_wpm + std::clamp(delta, -18, 18));
    if (updated == stats_.effective_wpm) return false;
    stats_.effective_wpm = updated;
    return true;
}
bool Lesson::select_letters(int count) {
    const auto updated = letters(count);
    if (phase_ == Phase::playing || stats_.practice != Practice::letters || updated == stats_.letters) return false;
    stats_.letters = updated;
    phase_ = Phase::ready;
    answer_.clear();
    make_prompt();
    return true;
}
bool Lesson::set_practice(Practice practice, int group) {
    if (phase_ == Phase::playing || !valid_practice(practice) || group != group_size(group) ||
        (stats_.practice == practice && stats_.group == group)) return false;
    stats_.practice = practice;
    stats_.group = group;
    phase_ = Phase::ready;
    answer_.clear();
    make_prompt();
    return true;
}
} // namespace cw
