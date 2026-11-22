#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cw {
class Audio {
public:
    explicit Audio(bool silent_test = false);
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;
    bool start(std::vector<std::int16_t> pcm);
    bool finished();
    const std::string& error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> self_;
};
}
