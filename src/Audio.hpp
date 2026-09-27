#pragma once

#include <raylib.h>

#include <array>
#include <cstddef>

namespace pm {

enum class Sfx : std::size_t { Step, Push, Grow, Stack, Bump, Undo, Reset, Complete, Count };

class Audio {
public:
    Audio();
    ~Audio();
    Audio(const Audio&) = delete;
    Audio& operator=(const Audio&) = delete;

    void play(Sfx sfx, float pitch = 1.0f);
    void toggleMute() { muted_ = !muted_; }
    [[nodiscard]] bool muted() const { return muted_; }

private:
    std::array<Sound, static_cast<std::size_t>(Sfx::Count)> sounds_{};
    bool ready_{};
    bool muted_{};
};

}
