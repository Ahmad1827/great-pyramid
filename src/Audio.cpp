#include "Audio.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <numbers>

namespace pm {

namespace {

constexpr int kRate = 44100;

struct Synth {
    std::uint32_t seed = 0x9E3779B9u;
    float lowpass = 0.0f;

    float noise() {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        return static_cast<float>(seed) / 2147483648.0f - 1.0f;
    }
};

Sound render(float seconds, const std::function<float(float, Synth&)>& fn) {
    const auto frames = static_cast<unsigned>(seconds * kRate);
    auto* data = static_cast<short*>(MemAlloc(frames * sizeof(short)));
    Synth synth;
    for (unsigned i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / kRate;
        const float v = std::clamp(fn(t, synth), -1.0f, 1.0f);
        data[i] = static_cast<short>(v * 30000.0f);
    }
    Wave wave{frames, kRate, 16, 1, data};
    Sound sound = LoadSoundFromWave(wave);
    UnloadWave(wave);
    return sound;
}

float env(float t, float attack, float decay) {
    if (t < attack) {
        return t / attack;
    }
    return std::exp(-(t - attack) / decay);
}

float tone(float t, float freq) { return std::sin(2.0f * std::numbers::pi_v<float> * freq * t); }

}

Audio::Audio() {
    InitAudioDevice();
    ready_ = IsAudioDeviceReady();
    if (!ready_) {
        return;
    }
    auto at = [this](Sfx s) -> Sound& { return sounds_[static_cast<std::size_t>(s)]; };
    at(Sfx::Step) = render(0.07f, [](float t, Synth& s) {
        s.lowpass += (s.noise() - s.lowpass) * 0.08f;
        return s.lowpass * env(t, 0.004f, 0.018f) * 0.9f;
    });
    at(Sfx::Push) = render(0.16f, [](float t, Synth& s) {
        s.lowpass += (s.noise() - s.lowpass) * 0.12f;
        return (s.lowpass * 0.8f + tone(t, 90.0f - t * 120.0f) * 0.35f) * env(t, 0.01f, 0.05f);
    });
    at(Sfx::Grow) = render(0.28f, [](float t, Synth& s) {
        s.lowpass += (s.noise() - s.lowpass) * (0.05f + t * 0.4f);
        return s.lowpass * env(t, 0.03f, 0.08f) * 1.2f + tone(t, 220.0f + t * 300.0f) * env(t, 0.02f, 0.07f) * 0.15f;
    });
    at(Sfx::Stack) = render(0.22f, [](float t, Synth&) {
        return (tone(t, 180.0f) * 0.6f + tone(t, 361.0f) * 0.25f + tone(t, 543.0f) * 0.1f) * env(t, 0.002f, 0.045f);
    });
    at(Sfx::Bump) = render(0.08f, [](float t, Synth&) { return tone(t, 70.0f) * env(t, 0.003f, 0.02f) * 0.6f; });
    at(Sfx::Undo) = render(0.12f, [](float t, Synth&) {
        return tone(t, 520.0f - t * 1600.0f) * env(t, 0.005f, 0.04f) * 0.25f;
    });
    at(Sfx::Reset) = render(0.35f, [](float t, Synth& s) {
        s.lowpass += (s.noise() - s.lowpass) * 0.03f;
        return s.lowpass * env(t, 0.1f, 0.12f) * 1.6f;
    });
    at(Sfx::Complete) = render(1.8f, [](float t, Synth&) {
        constexpr float notes[4] = {392.0f, 493.88f, 587.33f, 783.99f};
        float v = 0.0f;
        for (int i = 0; i < 4; ++i) {
            const float start = static_cast<float>(i) * 0.14f;
            if (t >= start) {
                const float lt = t - start;
                v += (tone(lt, notes[i]) + 0.3f * tone(lt, notes[i] * 2.0f)) * env(lt, 0.01f, 0.5f) * 0.18f;
            }
        }
        return v;
    });
}

Audio::~Audio() {
    if (!ready_) {
        return;
    }
    for (auto& s : sounds_) {
        UnloadSound(s);
    }
    CloseAudioDevice();
}

void Audio::play(Sfx sfx, float pitch) {
    if (!ready_ || muted_) {
        return;
    }
    Sound& s = sounds_[static_cast<std::size_t>(sfx)];
    SetSoundPitch(s, pitch);
    PlaySound(s);
}

}
