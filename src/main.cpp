#include "Audio.hpp"
#include "Game.hpp"
#include "Renderer.hpp"

#include <raylib.h>

#include <array>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

namespace {

constexpr float kRepeatDelay = 0.2f;
constexpr float kRepeatRate = 0.1f;

std::string savePath() {
    const char* xdg = std::getenv("XDG_DATA_HOME");
    if (xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/pyramid-maker/save.txt";
    }
    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        return std::string(home) + "/.local/share/pyramid-maker/save.txt";
    }
    return "pyramid-maker-save.txt";
}

struct KeyRepeat {
    int key{};
    float timer{};

    bool fire(bool down, bool pressed, float dt) {
        if (pressed) {
            timer = kRepeatDelay;
            return true;
        }
        if (!down) {
            return false;
        }
        timer -= dt;
        if (timer <= 0.0f) {
            timer += kRepeatRate;
            return true;
        }
        return false;
    }
};

std::optional<pm::Dir> heldDirection(std::array<KeyRepeat, 4>& repeats, float dt) {
    static constexpr std::array<std::pair<int, int>, 4> keys{{
        {KEY_UP, KEY_W},
        {KEY_DOWN, KEY_S},
        {KEY_LEFT, KEY_A},
        {KEY_RIGHT, KEY_D},
    }};
    std::optional<pm::Dir> result;
    for (std::size_t i = 0; i < keys.size(); ++i) {
        const bool down = IsKeyDown(keys[i].first) || IsKeyDown(keys[i].second);
        const bool pressed = IsKeyPressed(keys[i].first) || IsKeyPressed(keys[i].second);
        if (repeats[i].fire(down, pressed, dt) && (!result || pressed)) {
            result = pm::kDirs[i];
        }
    }
    return result;
}

}

int main() {
    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 800, "Pyramid Maker");
    SetWindowMinSize(640, 400);
    SetExitKey(KEY_NULL);
    SetTargetFPS(60);
    {
        pm::Audio audio;
        pm::Game game{pm::parseWorld(pm::worldRows(), pm::gardenNames())};
        const std::string path = savePath();
        game.load(path);
        pm::Renderer renderer;
        renderer.snap(game);
        bool title = true;
        bool help = false;
        bool quit = false;
        std::array<KeyRepeat, 4> moveRepeat{};
        KeyRepeat undoRepeat{};
        while (!quit && !WindowShouldClose()) {
            const float dt = std::min(GetFrameTime(), 0.05f);
            if (title) {
                if (GetKeyPressed() != 0 || IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
                    title = false;
                    if (game.solvedCount() == 0 && game.state().moves == 0) {
                        help = true;
                    }
                }
            } else if (IsKeyPressed(KEY_ESCAPE)) {
                if (help) {
                    help = false;
                } else {
                    quit = true;
                }
            } else if (IsKeyPressed(KEY_H) || IsKeyPressed(KEY_F1)) {
                help = !help;
            } else if (IsKeyPressed(KEY_M)) {
                audio.toggleMute();
            } else if (undoRepeat.fire(IsKeyDown(KEY_Z) || IsKeyDown(KEY_BACKSPACE),
                                       IsKeyPressed(KEY_Z) || IsKeyPressed(KEY_BACKSPACE), dt)) {
                if (game.undo()) {
                    audio.play(pm::Sfx::Undo);
                    renderer.snap(game);
                }
            } else if (IsKeyPressed(KEY_R)) {
                if (game.resetRoom()) {
                    audio.play(pm::Sfx::Reset);
                    renderer.snap(game);
                }
            } else if (const auto dir = heldDirection(moveRepeat, dt)) {
                help = false;
                const int before = game.solvedCount();
                const pm::MoveEvent ev = game.move(*dir);
                if (!ev.moved) {
                    renderer.onBlocked(*dir);
                    audio.play(pm::Sfx::Bump);
                } else {
                    renderer.onMove(game, ev);
                    if (ev.stacked) {
                        audio.play(pm::Sfx::Stack, 1.0f + 0.1f * static_cast<float>(3 - ev.sizeAfter));
                    } else if (ev.clearedSand) {
                        audio.play(pm::Sfx::Grow, ev.sizeAfter != ev.sizeBefore ? 1.0f : 1.25f);
                    } else if (ev.pushed) {
                        audio.play(pm::Sfx::Push, 1.2f - 0.1f * static_cast<float>(ev.sizeAfter));
                    } else {
                        audio.play(pm::Sfx::Step, 0.9f + 0.2f * static_cast<float>(GetRandomValue(0, 100)) / 100.0f);
                    }
                    if (ev.solvedRoom > 0) {
                        audio.play(pm::Sfx::Complete);
                        const int after = game.solvedCount();
                        const std::string sub = after == game.levelCount()
                                                    ? "Every pyramid in the desert stands."
                                                    : std::to_string(after) + " of " + std::to_string(game.levelCount()) + " pyramids raised";
                        renderer.banner(game.pyramidName(ev.solvedRoom), sub, 4.5f);
                        game.save(path);
                    }
                    if (game.solvedCount() > before) {
                        game.save(path);
                    }
                }
            }
            renderer.update(game, dt);
            BeginDrawing();
            renderer.draw(game, title, help, audio.muted());
            EndDrawing();
        }
        game.save(path);
    }
    CloseWindow();
    return 0;
}
