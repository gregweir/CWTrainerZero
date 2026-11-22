// SPDX-License-Identifier: MIT
// Developed for Tartanleaf.com by Greg Weir VA1GW
// Framebuffer initialization follows the CardputerZero Template (MIT).
// See licenses/CardputerZero-Template-MIT.txt and THIRD-PARTY-NOTICES.md.
#include "audio.hpp"
#include "trainer.hpp"
#include "lvgl.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>

#if CW_CARDPUTER_ZERO
#include "src/drivers/display/fb/lv_linux_fbdev.h"
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>
#else
#include <SDL.h>
#endif

namespace {
constexpr int width = 320, height = 170;
using Phase = cw::Lesson::Phase;
enum Key { none, listen, submit, backspace, slower, faster, quit, level_up, level_down, sample, settings,
    symbol_a, symbol_z = symbol_a + 25, symbol_0, symbol_9 = symbol_0 + 9,
    period, comma, question, slash, equals };

const char* practice_name(cw::Practice choice) {
    switch (choice) {
        case cw::Practice::numbers: return "Numbers";
        case cw::Practice::punctuation: return "Punctuation";
        default: return "Letters";
    }
}

char typed_symbol(Key key) {
    if (key >= symbol_a && key <= symbol_z) return static_cast<char>('A' + key - symbol_a);
    if (key >= symbol_0 && key <= symbol_9) return static_cast<char>('0' + key - symbol_0);
    if (key >= period && key <= equals) return cw::kPunctuation[key - period];
    return '\0';
}

std::uint32_t ticks() {
    using namespace std::chrono;
    static const auto start = steady_clock::now();
    return static_cast<std::uint32_t>(duration_cast<milliseconds>(steady_clock::now() - start).count());
}

void idle() { std::this_thread::sleep_for(std::chrono::milliseconds(4)); }

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, int x, int y, int w, std::uint32_t colour) {
    auto* item = lv_label_create(parent);
    lv_obj_set_pos(item, x, y);
    lv_obj_set_width(item, w);
    lv_label_set_long_mode(item, LV_LABEL_LONG_CLIP);
    lv_obj_set_style_text_font(item, font, 0);
    lv_obj_set_style_text_color(item, lv_color_hex(colour), 0);
    return item;
}

class Screen {
public:
    Screen() {
        auto* root = lv_screen_active();
        lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_set_style_bg_color(root, lv_color_hex(0x10252C), 0);
        auto* heading = label(root, &lv_font_montserrat_20, 9, 5, 301, 0xf4f3ea);
        lv_label_set_text(heading, "CWTrainerZero");
        auto* shortcut = label(root, &lv_font_montserrat_12, 203, 11, 107, 0xffdf9f);
        lv_obj_set_style_text_align(shortcut, LV_TEXT_ALIGN_RIGHT, 0);
#if CW_CARDPUTER_ZERO
        lv_label_set_text(shortcut, "Fn+1 settings");
#else
        lv_label_set_text(shortcut, "F1 settings");
#endif
        drill_ = panel(root);
        info_ = label(drill_, &lv_font_montserrat_12, 10, 30, 302, 0xb9d7d9);
        step_ = label(drill_, &lv_font_montserrat_14, 10, 48, 302, 0x88e4c4);
        for (int i = 0; i < cw::kMaxGroup; ++i) {
            auto* cell = cells_[i] = lv_obj_create(drill_);
            lv_obj_remove_style_all(cell);
            lv_obj_set_size(cell, 34, 34);
            lv_obj_set_pos(cell, 10 + i * 39, 69);
            lv_obj_set_style_radius(cell, 5, 0);
            lv_obj_set_style_bg_color(cell, lv_color_hex(0x18353e), 0);
            lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
            lv_obj_set_style_border_width(cell, 1, 0);
            lv_obj_set_style_border_color(cell, lv_color_hex(0x5d8385), 0);
            lv_obj_remove_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
            letters_[i] = label(cell, &lv_font_montserrat_24, 0, 1, 34, 0xffffff);
            lv_obj_set_style_text_align(letters_[i], LV_TEXT_ALIGN_CENTER, 0);
        }
        note_ = label(drill_, &lv_font_montserrat_12, 168, 72, 148, 0xe9eee7);
        level_ = label(drill_, &lv_font_montserrat_12, 168, 89, 148, 0xb9d7d9);
        detail_ = label(drill_, &lv_font_montserrat_12, 10, 107, 301, 0xffdf9f);
        lv_label_set_text(label(drill_, &lv_font_montserrat_12, 10, 125, 303, 0xb9d7d9),
            "Space play/replay    Enter check/next");
        mode_help_ = label(drill_, &lv_font_montserrat_12, 10, 140, 303, 0xb9d7d9);
        lv_label_set_text(label(drill_, &lv_font_montserrat_12, 10, 155, 303, 0xb9d7d9),
            "Left/Right speed  Backsp edit  Esc quit");

        menu_ = panel(root);
        lv_label_set_text(label(menu_, &lv_font_montserrat_14, 10, 36, 301, 0x88e4c4), "PRACTICE SETTINGS");
        mode_ = label(menu_, &lv_font_montserrat_14, 14, 66, 301, 0xf4f3ea);
        group_ = label(menu_, &lv_font_montserrat_14, 14, 92, 301, 0xf4f3ea);
        lv_label_set_text(label(menu_, &lv_font_montserrat_12, 10, 124, 303, 0xb9d7d9),
            "Up/Down select    Left/Right change");
        lv_label_set_text(label(menu_, &lv_font_montserrat_12, 10, 147, 303, 0xffdf9f),
            "Enter save    Esc cancel");
        lv_obj_add_flag(menu_, LV_OBJ_FLAG_HIDDEN);
    }

    void show_menu(cw::Practice practice, int size, int row) {
        lv_obj_add_flag(drill_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(menu_, LV_OBJ_FLAG_HIDDEN);
        const auto choice = std::string(row == 0 ? "> " : "   ") + "Set: " + practice_name(practice);
        const auto group = std::string(row == 1 ? "> " : "   ") + "Characters per group: " + std::to_string(size);
        lv_label_set_text(mode_, choice.c_str());
        lv_label_set_text(group_, group.c_str());
    }

    void update(const cw::Lesson& lesson, std::string_view message = {}, bool listening_sample = false,
                std::size_t picker = 0) {
        lv_obj_add_flag(menu_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(drill_, LV_OBJ_FLAG_HIDDEN);
        const auto& s = lesson.stats();
        const auto info = "18/" + std::to_string(s.effective_wpm) + " WPM   Groups "
            + score(s.correct, s.attempts) + "   Chars " + score(s.char_correct, s.char_attempts);
        const bool letters = s.practice == cw::Practice::letters;
        const bool punct = s.practice == cw::Practice::punctuation;
        const auto level = letters ? "Letters L" + std::to_string(s.letters - 1) + "/25"
            : std::string(practice_name(s.practice));
        lv_label_set_text(level_, level.c_str());
        lv_label_set_text(info_, info.c_str());
        const auto& input = lesson.answer();
        for (std::size_t i = 0; i < letters_.size(); ++i) {
            if (i >= static_cast<std::size_t>(s.group)) lv_obj_add_flag(cells_[i], LV_OBJ_FLAG_HIDDEN);
            else lv_obj_remove_flag(cells_[i], LV_OBJ_FLAG_HIDDEN);
            char first[2]{i < input.size() ? input[i] : '_', 0};
            lv_label_set_text(letters_[i], first);
        }
        const auto hint = letters ? "Up/Down level  0 A-Z  Tab new"
            : punct ? "Up/Down pick punctuation  Tab type" : "Type digits 0-9 after listening";
        lv_label_set_text(mode_help_, hint);
        std::string state_hint(message);
        switch (lesson.phase()) {
            case Phase::ready:
                lv_label_set_text(step_, "1  LISTEN");
                if (punct) lv_label_set_text(note_, ". , ? / =");
                else if (!letters) lv_label_set_text(note_, "Digits 0-9");
                else if (s.letters == cw::kMinLetters) lv_label_set_text(note_, "Start with K and M");
                else if (s.letters == cw::kMaxLetters) lv_label_set_text(note_, "Full alphabet: A-Z");
                else {
                    const auto eligible = std::string("Newest: ") + lesson.newest();
                    lv_label_set_text(note_, eligible.c_str());
                }
                break;
            case Phase::playing:
                lv_label_set_text(step_, "1  LISTENING...");
                lv_label_set_text(note_, "Wait for the tones");
                break;
            case Phase::answer: {
                const bool ready = input.size() == static_cast<std::size_t>(s.group);
                lv_label_set_text(step_, ready ? "3  CHECK" : "2  TYPE WHAT YOU HEARD");
                lv_label_set_text(note_, ready ? "Enter to check" : "Space to replay");
                if (punct && !ready) state_hint = std::string("Selected: ") + cw::kPunctuation[picker] + "    Tab to type";
                break;
            }
            case Phase::checked: {
                const auto heard = "Heard: " + lesson.prompt();
                const auto result = std::string(lesson.correct() ? "CORRECT" : "MATCHED") + "   "
                    + std::to_string(lesson.matched()) + "/" + std::to_string(s.group);
                lv_label_set_text(step_, result.c_str());
                lv_label_set_text(note_, heard.c_str());
                break;
            }
        }
        if (listening_sample) lv_label_set_text(step_, "NEW SOUND PRACTICE");
        lv_label_set_text(detail_, state_hint.c_str());
    }

private:
    static lv_obj_t* panel(lv_obj_t* root) {
        auto* box = lv_obj_create(root);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, width, height);
        lv_obj_set_pos(box, 0, 0);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_remove_flag(box, LV_OBJ_FLAG_CLICKABLE);
        return box;
    }
    static std::string score(unsigned correct, unsigned attempted) {
        if (attempted < 10000) return std::to_string(correct) + "/" + std::to_string(attempted);
        // Keep counters legible even on a long-running 320px display.
        return std::to_string(correct * 100 / attempted) + "%";
    }
    lv_obj_t *drill_, *menu_, *info_, *step_, *note_, *detail_, *level_, *mode_help_, *mode_, *group_;
    std::array<lv_obj_t*,cw::kMaxGroup> letters_{}, cells_{};
};

class App {
public:
    App(std::filesystem::path state, bool silent_test)
        : state_file_(std::move(state)), lesson_(cw::load_profile(state_file_), silent_test ? 7 : std::random_device{}()),
          audio_(silent_test) {
        screen_.update(lesson_);
    }

    void key(Key key) {
        bool redraw = false;
        std::string_view message;
        if (menu_open_) { configure(key); return; }
        if (key == quit) { running = false; return; }
        if (sampling_) return;
        if (key == settings && lesson_.phase() != Phase::playing) {
            menu_open_ = true;
            mode_ = lesson_.stats().practice;
            group_ = lesson_.stats().group;
            settings_row_ = 0;
            screen_.show_menu(mode_, group_, settings_row_);
            return;
        }
        const auto practice = lesson_.stats().practice;
        const char typed = typed_symbol(key);
        if (typed != '\0') {
            if (key == symbol_0 && practice == cw::Practice::letters) {
                if (lesson_.select_letters(cw::kMaxLetters))
                    draw(persist() ? "Level saved. Space starts a new group." : "Level not saved to disk");
            } else if (lesson_.enter(typed)) draw();
            return;
        }
        if (practice == cw::Practice::punctuation && lesson_.phase() == Phase::answer) {
            if (key == level_up || key == level_down) {
                const auto size = cw::kPunctuation.size();
                picker_ = (picker_ + (key == level_up ? size - 1 : 1)) % size;
                draw();
                return;
            }
            if (key == sample) {
                if (lesson_.enter(cw::kPunctuation[picker_])) draw();
                return;
            }
        }
        switch (key) {
            case listen:
                if (lesson_.start_audio()) {
                    if (!audio_.start(cw::render_audio(lesson_.prompt(), lesson_.stats().effective_wpm))) {
                        lesson_.audio_failed();
                        message = "Audio unavailable; check sound output";
                        std::cerr << "CWTrainerZero audio: " << audio_.error() << '\n';
                    }
                    redraw = true;
                }
                break;
            case submit:
                if (lesson_.phase() == Phase::checked) redraw = lesson_.next();
                else if (lesson_.check()) {
                    redraw = true;
                    message = persist() ? "Enter for the next group" : "Progress not saved to disk";
                }
                break;
            case backspace: redraw = lesson_.erase(); break;
            case slower: case faster:
                if (lesson_.adjust_effective(key == slower ? -1 : 1)) {
                    redraw = true;
                    message = persist() ? "Character speed stays at 18 WPM" : "Speed not saved to disk";
                }
                break;
            case level_up: case level_down:
                if (lesson_.select_letters(lesson_.stats().letters + (key == level_up ? 1 : -1))) {
                    redraw = true;
                    message = persist() ? "Level saved. Space starts a new group." : "Level not saved to disk";
                }
                break;
            case sample:
                if (practice == cw::Practice::letters && lesson_.phase() != Phase::playing) {
                    const char letter = lesson_.newest();
                    sampling_ = audio_.start(cw::render_audio(std::string(1, letter), lesson_.stats().effective_wpm));
                    if (sampling_) {
                        screen_.update(lesson_, std::string("This is ") + letter + ". Not scored.", true);
                    } else screen_.update(lesson_, "Audio unavailable; check sound output");
                }
                break;
            default: break;
        }
        if (redraw) draw(message);
    }

    void tick() {
        if (audio_.finished()) {
            if (sampling_) sampling_ = false;
            else lesson_.audio_finished();
            draw();
        }
    }

    const cw::Lesson& lesson() const { return lesson_; }
    bool sampling() const { return sampling_; }
    bool running = true;
private:
    void draw(std::string_view message = {}) { screen_.update(lesson_, message, false, picker_); }
    void configure(Key key) {
        if (key == quit || key == settings) {
            menu_open_ = false;
            draw();
            return;
        }
        if (key == submit) {
            menu_open_ = false;
            if (lesson_.set_practice(mode_, group_)) {
                picker_ = 0;
                draw(persist() ? "Settings saved. Space starts a new group." : "Settings not saved to disk");
            } else draw();
            return;
        }
        if (key == level_up || key == level_down) settings_row_ = 1 - settings_row_;
        if (key == slower || key == faster) {
            const int change = key == slower ? -1 : 1;
            if (settings_row_ == 0) mode_ = static_cast<cw::Practice>((static_cast<int>(mode_) + 3 + change) % 3);
            else group_ = std::clamp(group_ + change, cw::kMinGroup, cw::kMaxGroup);
        }
        screen_.show_menu(mode_, group_, settings_row_);
    }
    bool persist() const { return cw::save_stats(state_file_, lesson_.stats()); }
    std::filesystem::path state_file_;
    cw::Lesson lesson_;
    cw::Audio audio_;
    Screen screen_;
    bool sampling_ = false;
    bool menu_open_ = false;
    cw::Practice mode_ = cw::Practice::letters;
    int group_ = 2, settings_row_ = 0;
    std::size_t picker_ = 0;
};

#if CW_CARDPUTER_ZERO
constexpr Key map_key(unsigned key, bool internal, bool shifted) {
    if (internal) {
        // M5Stack's FactoryTest defines these TCA8418 direct event codes (see DEVICE-TEST.md).
        // On a generic Linux keyboard, e.g. code 73 is keypad 9, not equals.
        switch (key) {
            case 73: return equals; case 90: return comma; case 91: return period;
            case 92: return slash; case 93: return question;
        }
    }
    constexpr std::array<unsigned, 10> digit_keys{KEY_0, KEY_1, KEY_2, KEY_3, KEY_4,
        KEY_5, KEY_6, KEY_7, KEY_8, KEY_9};
    constexpr std::array<unsigned, 10> keypad_keys{KEY_KP0, KEY_KP1, KEY_KP2, KEY_KP3, KEY_KP4,
        KEY_KP5, KEY_KP6, KEY_KP7, KEY_KP8, KEY_KP9};
    for (std::size_t i = 0; i < digit_keys.size(); ++i)
        if (key == digit_keys[i] || (!internal && key == keypad_keys[i]))
            return static_cast<Key>(symbol_0 + i);
    constexpr std::array<unsigned, 26> letter_keys{KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H,
        KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
        KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z};
    for (std::size_t i = 0; i < letter_keys.size(); ++i)
        if (key == letter_keys[i]) return static_cast<Key>(symbol_a + i);
    switch (key) {
        case KEY_SPACE: return listen;
        case KEY_ENTER: case KEY_KPENTER: return submit;
        case KEY_BACKSPACE: case KEY_DELETE: return backspace;
        case KEY_LEFT: return slower;
        case KEY_RIGHT: return faster;
        case KEY_UP: return level_up;
        case KEY_DOWN: return level_down;
        case KEY_F1: return settings;
        case KEY_DOT: case KEY_KPDOT: return period;
        case KEY_COMMA: case KEY_KPCOMMA: return comma;
        case KEY_QUESTION: return question;
        case KEY_SLASH: return shifted ? question : slash;
        case KEY_KPSLASH: return slash;
        case KEY_EQUAL: case KEY_KPEQUAL: return equals;
        case KEY_TAB: return sample;
        case KEY_ESC: return quit;
        default: return none;
    }
}

static_assert(map_key(KEY_F1, true, false) == settings &&
    map_key(KEY_0, true, false) == symbol_0 && map_key(KEY_KP9, false, false) == symbol_9 &&
    map_key(73, true, false) == equals && map_key(90, true, false) == comma &&
    map_key(91, true, false) == period && map_key(92, true, false) == slash &&
    map_key(93, true, false) == question && map_key(KEY_SLASH, false, true) == question,
    "Keep TCA8418 direct symbol and generic keyboard mappings separate");

class DeviceKeyboard {
public:
    explicit DeviceKeyboard(const std::string& named) {
        if (!named.empty()) { open_candidate(named.c_str()); return; }
        const auto* dir_path = "/dev/input";
        auto* dir = opendir(dir_path);
        if (!dir) return;
        while (auto* entry = readdir(dir)) {
            if (std::strncmp(entry->d_name, "event", 5) != 0) continue;
            if (open_candidate((std::string(dir_path) + "/" + entry->d_name).c_str())) break;
        }
        closedir(dir);
    }
    ~DeviceKeyboard() { if (fd_ >= 0) close(fd_); }
    bool valid() const { return fd_ >= 0; }
    void dispatch(App& app) {
        input_event event{};
        while (::read(fd_, &event, sizeof(event)) == sizeof(event)) {
            if (event.type != EV_KEY) continue;
            if (event.code == KEY_LEFTSHIFT) shift_left_ = event.value != 0;
            if (event.code == KEY_RIGHTSHIFT) shift_right_ = event.value != 0;
            if (event.value == 1) app.key(map_key(event.code, internal_, shift_left_ || shift_right_));
        }
    }
private:
    int fd_ = -1;
    bool internal_ = false, shift_left_ = false, shift_right_ = false;
    bool open_candidate(const char* path) {
        const auto fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) return false;
        constexpr auto bits = sizeof(unsigned long) * 8;
        std::array<unsigned long, KEY_MAX / bits + 1> keys{};
        const auto has = [&](unsigned n){ return keys[n / bits] & (1UL << (n % bits)); };
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys.data()) < 0 ||
            !has(KEY_K) || !has(KEY_M) || !has(KEY_ENTER) || !has(KEY_SPACE)) {
            close(fd); return false;
        }
        std::array<char, 128> name{};
        if (ioctl(fd, EVIOCGNAME(name.size()), name.data()) >= 0) {
            name.back() = '\0';
            internal_ = std::string_view(name.data()).find("tca8418") != std::string_view::npos;
        }
        fd_ = fd;
        return true;
    }
};
#else
Key map_key(SDL_Keycode key, Uint16 mods) {
    if (key >= SDLK_0 && key <= SDLK_9) return static_cast<Key>(symbol_0 + key - SDLK_0);
    if (key >= SDLK_KP_1 && key <= SDLK_KP_9) return static_cast<Key>(symbol_0 + 1 + key - SDLK_KP_1);
    if (key >= SDLK_a && key <= SDLK_z) return static_cast<Key>(symbol_a + (key - SDLK_a));
    switch (key) {
        case SDLK_SPACE: return listen;
        case SDLK_RETURN: case SDLK_KP_ENTER: return submit;
        case SDLK_BACKSPACE: case SDLK_DELETE: return backspace;
        case SDLK_LEFT: return slower;
        case SDLK_RIGHT: return faster;
        case SDLK_UP: return level_up;
        case SDLK_DOWN: return level_down;
        case SDLK_KP_0: return symbol_0;
        case SDLK_F1: return settings;
        case SDLK_PERIOD: case SDLK_KP_PERIOD: return period;
        case SDLK_COMMA: return comma;
        case SDLK_QUESTION: return question;
        case SDLK_SLASH: return (mods & KMOD_SHIFT) ? question : slash;
        case SDLK_KP_DIVIDE: return slash;
        case SDLK_EQUALS: case SDLK_KP_EQUALS: return equals;
        case SDLK_TAB: return sample;
        case SDLK_ESCAPE: return quit;
        default: return none;
    }
}

class Desktop {
public:
    explicit Desktop(bool hidden) {
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) fail();
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        window_ = SDL_CreateWindow("CWTrainerZero (320 x 170 simulator)",
            SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
            width * 2, height * 2, hidden ? SDL_WINDOW_HIDDEN : SDL_WINDOW_SHOWN);
        if (!window_) fail();
        renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
        if (!renderer_) renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_SOFTWARE);
        if (!renderer_) fail();
        SDL_RenderSetLogicalSize(renderer_, width, height);
        texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, width, height);
        if (!texture_) fail();
        display_ = lv_display_create(width, height);
        if (!display_) throw std::runtime_error("LVGL display creation failed");
        lv_display_set_color_format(display_, LV_COLOR_FORMAT_RGB565);
        lv_display_set_user_data(display_, this);
        lv_display_set_buffers(display_, buffer_.data(), nullptr, sizeof(buffer_), LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(display_, [](lv_display_t* display, const lv_area_t*, std::uint8_t* px){
            auto& ui = *static_cast<Desktop*>(lv_display_get_user_data(display));
            SDL_UpdateTexture(ui.texture_, nullptr, px, width * 2);
            SDL_RenderClear(ui.renderer_);
            SDL_RenderCopy(ui.renderer_, ui.texture_, nullptr, nullptr);
            SDL_RenderPresent(ui.renderer_);
            lv_display_flush_ready(display);
        });
    }

    ~Desktop() {
        if (display_) lv_display_delete(display_);
        if (texture_) SDL_DestroyTexture(texture_);
        if (renderer_) SDL_DestroyRenderer(renderer_);
        if (window_) SDL_DestroyWindow(window_);
        SDL_Quit();
    }

    void dispatch(App& app) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) app.key(quit);
            if (event.type == SDL_KEYDOWN && !event.key.repeat) app.key(map_key(event.key.keysym.sym, event.key.keysym.mod));
        }
    }

    void screenshot(const std::filesystem::path& path) {
        lv_refr_now(display_);
        auto* rgb = SDL_CreateRGBSurfaceWithFormatFrom(buffer_.data(), width, height, 16, width * 2, SDL_PIXELFORMAT_RGB565);
        if (!rgb) fail();
        const auto result = SDL_SaveBMP(rgb, path.c_str());
        SDL_FreeSurface(rgb);
        if (result != 0) fail();
    }

private:
    [[noreturn]] static void fail() { throw std::runtime_error(SDL_GetError()); }
    alignas(64) std::array<std::uint16_t, width * height> buffer_{};
    SDL_Window* window_{};
    SDL_Renderer* renderer_{};
    SDL_Texture* texture_{};
    lv_display_t* display_{};
};

void require(bool valid, std::string_view description) {
    if (!valid) throw std::runtime_error(std::string(description));
}

int smoke(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto settings = directory / "stats";
    require(cw::save_stats(settings, {}), "Isolated test storage");
    Desktop desktop(true);
    App app(settings, true);
    const auto key = [&](SDL_Keycode k, Uint16 mods = 0) {
        SDL_Event event{};
        event.type = SDL_KEYDOWN;
        event.key.keysym.sym = k;
        event.key.keysym.mod = mods;
        require(SDL_PushEvent(&event) == 1, "SDL event queue");
        desktop.dispatch(app);
        app.tick();
        lv_timer_handler();
    };
    const auto wait = [&] {
        const auto until = ticks() + 13000;
        while (app.lesson().phase() == Phase::playing && ticks() < until) {
            desktop.dispatch(app); app.tick(); lv_timer_handler(); idle();
        }
        require(app.lesson().phase() == Phase::answer, "Null backend must consume the sample buffer");
    };
    desktop.screenshot(directory / "listen.bmp");
    key(SDLK_LEFT);
    require(app.lesson().stats().effective_wpm == 7, "Keyboard effective speed");
    key(SDLK_RETURN);
    require(app.lesson().phase() == Phase::ready, "Check blocked until listen");
    const auto prompt = app.lesson().prompt();
    key(SDLK_SPACE); wait();
    key(prompt.front() == 'K' ? SDLK_k : SDLK_m);
    key(SDLK_BACKSPACE);
    require(app.lesson().answer().empty(), "Edit over the SDL route");
    key(prompt.front() == 'K' ? SDLK_k : SDLK_m);
    key(SDLK_SPACE); wait();
    require(app.lesson().answer().size() == 1 && app.lesson().prompt() == prompt, "Replay keeps prompt and answer");
    key(prompt.back() == 'K' ? SDLK_k : SDLK_m);
    key(SDLK_RETURN);
    require(app.lesson().correct(), "Complete lesson via keyboard");
    desktop.screenshot(directory / "checked.bmp");
    const auto restored = cw::load_stats(settings);
    require(restored.effective_wpm == 7 && restored.attempts == 1 && restored.correct == 1, "Persist settings and score");
    key(SDLK_RETURN);
    require(app.lesson().phase() == Phase::ready && app.lesson().prompt() != prompt, "Next pair clears answer");
    key(SDLK_UP);
    require(app.lesson().alphabet() == "KMU", "Arrow advances one letter");
    key(SDLK_TAB);
    require(app.sampling(), "Sample starts independently");
    key(SDLK_RETURN);
    const auto sample_until = ticks() + 3500;
    while (app.sampling() && ticks() < sample_until) {
        desktop.dispatch(app); app.tick(); lv_timer_handler(); idle();
    }
    require(!app.sampling() && app.lesson().phase() == Phase::ready, "Sample cannot unlock an unplayed pair");
    require(app.lesson().stats().attempts == 1, "Sample does not change scores");
    key(SDLK_0);
    require(app.lesson().alphabet().size() == 26, "Digit jumps ahead without a score gate");
    desktop.screenshot(directory / "all-letters.bmp");
    key(SDLK_SPACE); wait();
    key(SDLK_z); key(SDLK_c);
    require(app.lesson().answer() == "ZC" && app.lesson().stats().effective_wpm == 7, "Z and C type, not speed");
    key(SDLK_DOWN);
    require(app.lesson().alphabet().size() == 25 && app.lesson().answer().empty(), "Level change clears unfinished pair");
    key(SDLK_0);
    require(cw::load_stats(settings).letters == 26 && cw::load_stats(settings).attempts == 1, "Level and legacy aggregate persist");
    key(SDLK_F1);
    desktop.screenshot(directory / "settings.bmp");
    key(SDLK_RIGHT); key(SDLK_DOWN); key(SDLK_RIGHT); key(SDLK_ESCAPE);
    require(app.running && app.lesson().stats().practice == cw::Practice::letters &&
        app.lesson().stats().group == 2, "Escape cancels draft settings");
    key(SDLK_F1); key(SDLK_RIGHT); key(SDLK_DOWN); key(SDLK_RIGHT); key(SDLK_RIGHT); key(SDLK_RETURN);
    require(app.lesson().stats().practice == cw::Practice::numbers && app.lesson().prompt().size() == 4,
        "Numbers and groups of four selected with settings keyboard");
    desktop.screenshot(directory / "numbers.bmp");
    key(SDLK_SPACE); wait();
    key(SDLK_0); key(SDLK_1); key(SDLK_9); key(SDLK_KP_8);
    require(app.lesson().answer() == "0198", "Numbers mode types 0 and keypad digits");
    key(SDLK_RETURN);
    const auto numeral_result = app.lesson().matched();
    require(cw::load_stats(settings).char_correct == 2 + numeral_result &&
        cw::load_stats(settings).char_attempts == 6, "Character counter after four-symbol group");

    key(SDLK_F1); key(SDLK_RIGHT); key(SDLK_DOWN); key(SDLK_LEFT); key(SDLK_LEFT); key(SDLK_LEFT); key(SDLK_RETURN);
    require(app.lesson().stats().practice == cw::Practice::punctuation && app.lesson().prompt().size() == 1,
        "Punctuation and single-character practice are available");
    key(SDLK_SPACE); wait();
    key(SDLK_PERIOD); require(app.lesson().answer() == ".", "Period key");
    key(SDLK_BACKSPACE); key(SDLK_COMMA); require(app.lesson().answer() == ",", "Comma key");
    key(SDLK_BACKSPACE); key(SDLK_SLASH, KMOD_LSHIFT); require(app.lesson().answer() == "?", "Shift-slash question key");
    key(SDLK_BACKSPACE); key(SDLK_SLASH); require(app.lesson().answer() == "/", "Plain slash is distinct");
    key(SDLK_BACKSPACE); key(SDLK_EQUALS); require(app.lesson().answer() == "=", "Equals key");
    key(SDLK_BACKSPACE);
    desktop.screenshot(directory / "punctuation.bmp");
    const auto position = cw::kPunctuation.find(app.lesson().prompt()[0]);
    for (std::size_t i = 0; i < position; ++i) key(SDLK_DOWN);
    key(SDLK_TAB); key(SDLK_RETURN);
    require(app.lesson().correct(), "Punctuation dial works without a physical punctuation key");
    const auto final = cw::load_stats(settings);
    require(final.practice == cw::Practice::punctuation && final.group == 1 && final.letters == 26 &&
        final.char_attempts == 7 && final.char_correct == 3 + numeral_result && final.attempts == 3,
        "Practice, group size, letter level and counters all survive reload");
    key(SDLK_ESCAPE);
    require(!app.running, "Escape quits");
    std::cout << "Simulator keyboard, LVGL frames and playback callbacks passed. Audio backend: silent test.\n";
    return 0;
}
#endif
} // namespace

int main(int argc, char** argv) try {
    if (argc == 4 && std::string_view(argv[1]) == "--wav") {
        cw::write_wav(argv[2], cw::render_audio(argv[3], 8));
        return 0;
    }
    lv_init();
    lv_tick_set_cb(ticks);
#if CW_CARDPUTER_ZERO
    const std::string_view option = argc > 1 ? argv[1] : "";
    if (argc != 1 && !(argc == 3 && option == "--keyboard")) {
        std::cerr << "Usage: cwtrainerzero [--keyboard /dev/input/eventN]\n";
        return 2;
    }
    DeviceKeyboard keyboard(argc == 3 ? argv[2] : "");
    if (!keyboard.valid()) {
        std::cerr << "CWTrainerZero: no readable Cardputer keyboard with K, M, Space and Enter. Check the launcher session.\n";
        return 1;
    }
    auto* display = lv_linux_fbdev_create();
    if (!display || lv_linux_fbdev_set_file(display, "/dev/fb0") != LV_RESULT_OK ||
        lv_display_get_horizontal_resolution(display) != width || lv_display_get_vertical_resolution(display) != height) {
        std::cerr << "CWTrainerZero needs the 320 x 170 Cardputer Zero framebuffer.\n";
        return 1;
    }
    {
        App app(cw::stats_file(), false);
        while (app.running) { keyboard.dispatch(app); app.tick(); lv_timer_handler(); idle(); }
    }
    lv_display_delete(display);
#else
    if (argc == 3 && std::string_view(argv[1]) == "--smoke-test") return smoke(argv[2]);
    if (argc != 1) { std::cerr << "Usage: cwtrainerzero\n"; return 2; }
    {
        Desktop desktop(false);
        App app(cw::stats_file(), false);
        while (app.running) { desktop.dispatch(app); app.tick(); lv_timer_handler(); idle(); }
    }
#endif
    lv_deinit();
    return 0;
} catch (const std::exception& err) {
    std::cerr << "CWTrainerZero: " << err.what() << '\n';
    return 1;
}
