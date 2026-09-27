#pragma once

#include "Game.hpp"

#include <raylib.h>

#include <cstdint>
#include <string>
#include <vector>

namespace pm {

class Renderer {
public:
    Renderer();
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void snap(const Game& game);
    void onMove(const Game& game, const MoveEvent& event);
    void onBlocked(Dir dir);
    void banner(std::string title, std::string subtitle, float seconds);
    void update(const Game& game, float dt);
    void draw(const Game& game, bool title, bool help, bool muted) const;
    [[nodiscard]] bool animating() const { return animT_ < 1.0f; }

private:
    struct Particle {
        Vector2 pos{};
        Vector2 vel{};
        float z{};
        float vz{};
        float life{};
        float maxLife{};
        float size{};
        Color color{};
    };

    Font font_{};
    bool ownsFont_{};
    Vector2 camTarget_{};
    float camZoom_{1.0f};
    MoveEvent anim_{};
    float animT_{1.0f};
    float bumpT_{1.0f};
    Dir bumpDir_{Dir::Down};
    float walkPhase_{};
    double time_{};
    std::vector<Particle> particles_;
    std::uint32_t rng_{0x12345678u};
    std::string bannerTitle_;
    std::string bannerSub_;
    float bannerTime_{};
    float bannerDuration_{};
    int lastRoom_{-1};
    float roomTitleTime_{};
    int roomTitleId_{};

    float random01();
    void spawnPuff(Vector2 at, Color color, int count, float speed, float lift);
    void spawnConfetti(Vector2 at);
    [[nodiscard]] Camera2D camera() const;
    void desiredCamera(const Game& game, Vector2& target, float& zoom) const;

    void drawWorld(const Game& game) const;
    void drawGround(const Game& game, int x0, int y0, int x1, int y1) const;
    void drawObjects(const Game& game, int x0, int y0, int x1, int y1) const;
    void drawHud(const Game& game, bool help, bool muted) const;
    void drawTitle() const;
    void text(const std::string& s, Vector2 pos, float size, Color color, bool centered = false) const;
};

}
