#include "audio.hpp"
#include "trainer.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

#define MA_NO_DECODING
#define MA_NO_ENCODING
#define MA_NO_RESOURCE_MANAGER
#define MA_NO_NODE_GRAPH
#define MA_NO_ENGINE
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include <miniaudio.h>

namespace cw {
struct Audio::Impl {
    ma_context context{};
    ma_device device{};
    bool ready = false, has_context = false, running = false;
    std::atomic<std::size_t> cursor{0};
    std::vector<std::int16_t> pcm;
    std::string error;

    static void output(ma_device* dev, void* dst, const void*, ma_uint32 frames) {
        auto& player = *static_cast<Impl*>(dev->pUserData);
        auto* data = static_cast<std::int16_t*>(dst);
        std::fill_n(data, frames, std::int16_t{0});
        const auto first = player.cursor.load(std::memory_order_relaxed);
        const auto n = std::min<std::size_t>(frames, player.pcm.size() - first);
        if (n) std::memcpy(data, player.pcm.data() + first, n * sizeof(*data));
        player.cursor.store(first + n, std::memory_order_release);
    }

    explicit Impl(bool silent_test) {
        const ma_backend null_backend = ma_backend_null;
#if CW_CARDPUTER_ZERO
        const ma_backend backends[] = {ma_backend_pulseaudio};
#else
        const ma_backend backends[] = {ma_backend_pulseaudio, ma_backend_alsa};
#endif
        auto result = ma_context_init(silent_test ? &null_backend : backends,
                                      silent_test ? 1 : sizeof(backends) / sizeof(*backends), nullptr, &context);
        if (result != MA_SUCCESS) { error = ma_result_description(result); return; }
        has_context = true;
        auto cfg = ma_device_config_init(ma_device_type_playback);
        cfg.playback.format = ma_format_s16;
        cfg.playback.channels = 1;
        cfg.sampleRate = kSampleRate;
        cfg.periodSizeInMilliseconds = 10;
        cfg.dataCallback = output;
        cfg.pUserData = this;
        result = ma_device_init(&context, &cfg, &device);
        ready = result == MA_SUCCESS;
        if (!ready) error = ma_result_description(result);
    }
    ~Impl() {
        if (ready) ma_device_uninit(&device);
        if (has_context) ma_context_uninit(&context);
    }
};

Audio::Audio(bool silent_test) : self_(std::make_unique<Impl>(silent_test)) {}
Audio::~Audio() = default;

bool Audio::start(std::vector<std::int16_t> pcm) {
    if (!self_->ready || self_->running || pcm.empty()) return false;
    self_->pcm = std::move(pcm);
    self_->cursor.store(0, std::memory_order_relaxed);
    const auto result = ma_device_start(&self_->device);
    if (result != MA_SUCCESS) {
        self_->error = ma_result_description(result);
        return false;
    }
    self_->running = true;
    return true;
}

bool Audio::finished() {
    if (!self_->running || self_->cursor.load(std::memory_order_acquire) < self_->pcm.size()) return false;
    // PCM ends in 50 ms of silence, longer than the requested output period.
    ma_device_stop(&self_->device);
    self_->running = false;
    return true;
}

const std::string& Audio::error() const { return self_->error; }
}
