#include "Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <numbers>
#include <utility>

namespace pm {

namespace {

constexpr float TS = 64.0f;
constexpr float TH = TS * 0.8f;
constexpr float kWallH = TS * 0.42f;
constexpr float kAnimTime = 0.12f;
constexpr float kBumpTime = 0.14f;
constexpr float kPi = std::numbers::pi_v<float>;

struct Palette {
    Color voidColor{70, 140, 164, 255};
    Color waterDeep{70, 140, 164, 255};
    Color sand{234, 200, 140, 255};
    Color sandSpeck{204, 164, 104, 255};
    Color sandLight{247, 222, 170, 255};
    Color sandDark{212, 172, 112, 255};
    Color sandRim{196, 154, 96, 255};
    Color bare{206, 182, 146, 255};
    Color bareDark{160, 132, 96, 255};
    Color paveLight{226, 206, 172, 255};
    Color pebble{176, 150, 112, 255};
    Color dirt{156, 102, 64, 255};
    Color dirtDark{120, 78, 50, 255};
    Color hedgeDark{78, 100, 46, 255};
    Color hedgeMid{112, 140, 62, 255};
    Color hedgeLight{142, 170, 80, 255};
    Color hedgeShine{170, 194, 104, 255};
    Color stoneFront{204, 158, 100, 255};
    Color stoneTop{236, 204, 146, 255};
    Color stoneShine{248, 226, 180, 255};
    Color stoneEdge{146, 104, 62, 255};
    Color gold{238, 188, 70, 255};
    Color goldDark{190, 136, 40, 255};
    Color goldLight{255, 226, 132, 255};
    Color water{86, 164, 186, 255};
    Color waterLight{156, 212, 222, 255};
    Color ink{30, 27, 31, 255};
    Color inkLight{58, 52, 58, 255};
    Color eye{250, 236, 200, 255};
    Color trunk{150, 104, 64, 255};
    Color trunkDark{118, 80, 48, 255};
    Color frond{92, 138, 64, 255};
    Color frondLight{128, 172, 82, 255};
    Color urn{188, 100, 62, 255};
    Color urnDark{150, 74, 46, 255};
    Color urnBand{236, 196, 120, 255};
    Color rock{178, 162, 142, 255};
    Color rockDark{140, 124, 106, 255};
    Color rockLight{206, 194, 176, 255};
    Color cactus{104, 146, 78, 255};
    Color cactusDark{78, 116, 58, 255};
    Color cactusLight{142, 180, 104, 255};
    Color wood{132, 90, 54, 255};
    Color text{255, 246, 226, 255};
    Color textDark{70, 46, 30, 255};
};

constexpr Palette P{};

constexpr std::array<float, 4> kBlockW{0.0f, 0.4f, 0.58f, 0.8f};
constexpr std::array<float, 4> kBlockFront{0.0f, 0.2f, 0.27f, 0.34f};

Color al(Color c, float a) {
    c.a = static_cast<unsigned char>(std::clamp(a, 0.0f, 1.0f) * static_cast<float>(c.a));
    return c;
}

Color sh(Color c, float f) {
    const auto ch = [f](unsigned char v) {
        return static_cast<unsigned char>(std::clamp(static_cast<float>(v) * f, 0.0f, 255.0f));
    };
    return {ch(c.r), ch(c.g), ch(c.b), c.a};
}

unsigned hashCell(int x, int y, unsigned salt) {
    unsigned h = static_cast<unsigned>(x) * 73856093u ^ static_cast<unsigned>(y) * 19349663u ^ salt * 83492791u;
    h ^= h >> 13;
    h *= 0x5bd1e995u;
    h ^= h >> 15;
    return h;
}

float hf(unsigned h, int shift) { return static_cast<float>((h >> shift) & 255u) / 255.0f; }

Vector2 center(Vec2i p) { return {(static_cast<float>(p.x) + 0.5f) * TS, (static_cast<float>(p.y) + 0.5f) * TH}; }

Vector2 lerpV(Vector2 a, Vector2 b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }

float ease(float t) { return t * t * (3.0f - 2.0f * t); }

void tri(Vector2 a, Vector2 b, Vector2 c, Color col) {
    const float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
    if (cross > 0.0f) {
        std::swap(b, c);
    }
    DrawTriangle(a, b, c, col);
}

void ellipse(Vector2 c, float rx, float ry, Color col) {
    DrawEllipse(static_cast<int>(c.x), static_cast<int>(c.y), rx, ry, col);
}

float topDepth(int size) { return kBlockW[static_cast<std::size_t>(size)] * TS * 0.5f; }

float stepUp(int size) { return kBlockFront[static_cast<std::size_t>(size)] * TS + topDepth(size) * 0.42f; }

float elevationUnder(Stack st, int size) {
    float e = 0.0f;
    for (int s = kLarge; s > size; --s) {
        if (st.has(s)) {
            e += stepUp(s);
        }
    }
    return e;
}

float stackHeight(Stack st) { return elevationUnder(st, 0); }

bool isBare(const WorldDef& d, const WorldState& s, Vec2i p) {
    if (!d.inBounds(p)) {
        return false;
    }
    const Terrain t = d.at(p);
    if (t != Terrain::Ground && t != Terrain::Gate && t != Terrain::Altar) {
        return false;
    }
    return !s.sand(d.index(p));
}

void drawBlock(Vector2 g, int size, float elev, float scale, bool shadow) {
    const auto i = static_cast<std::size_t>(size);
    const float w = kBlockW[i] * TS * scale;
    const float fh = kBlockFront[i] * TS * scale;
    const float td = w * 0.5f;
    const float baseY = g.y + 0.14f * TS - elev;
    if (shadow) {
        ellipse({g.x + 3.0f, g.y + 0.17f * TS}, w * 0.62f, w * 0.2f, al(BLACK, 0.16f));
    }
    const Rectangle front{g.x - w * 0.5f, baseY - fh - td * 0.5f, w, fh + td * 0.5f};
    const Rectangle top{g.x - w * 0.5f, baseY - fh - td, w, td};
    const float edge = std::max(1.2f, TS * 0.022f);
    DrawRectangleRounded(front, 0.3f, 8, P.stoneFront);
    DrawRectangleRoundedLinesEx(front, 0.3f, 8, edge, al(P.stoneEdge, 0.85f));
    const float courseY = baseY - fh * 0.48f;
    DrawLineEx({front.x + w * 0.1f, courseY}, {front.x + w * 0.9f, courseY}, edge * 0.8f, al(P.stoneEdge, 0.35f));
    if (size >= kMedium) {
        DrawLineEx({g.x + w * 0.12f, courseY}, {g.x + w * 0.12f, baseY - 2.0f}, edge * 0.8f, al(P.stoneEdge, 0.3f));
        DrawLineEx({g.x - w * 0.2f, courseY}, {g.x - w * 0.2f, baseY - fh + 2.0f}, edge * 0.8f, al(P.stoneEdge, 0.3f));
    }
    DrawRectangleRounded(top, 0.45f, 8, P.stoneTop);
    DrawRectangleRounded({top.x + w * 0.12f, top.y + td * 0.14f, w * 0.5f, td * 0.32f}, 1.0f, 6, al(P.stoneShine, 0.55f));
    DrawRectangleRoundedLinesEx(top, 0.45f, 8, edge, al(P.stoneEdge, 0.85f));
    const unsigned h = static_cast<unsigned>(size) * 2654435761u;
    for (int k = 0; k < size + 1; ++k) {
        DrawCircleV({top.x + w * (0.2f + 0.6f * hf(h, k * 5)), top.y + td * (0.35f + 0.4f * hf(h, k * 5 + 3))},
                    std::max(1.0f, TS * 0.012f), al(P.stoneEdge, 0.35f));
    }
}

void drawCap(Vector2 g, float elev, float bob) {
    const float w = kBlockW[kSmall] * TS;
    const float fh = kBlockFront[kSmall] * TS;
    const float td = w * 0.5f;
    const float baseY = g.y + 0.14f * TS - elev - fh - td * 0.45f + bob;
    const Vector2 apex{g.x, baseY - TS * 0.3f};
    const Vector2 left{g.x - w * 0.36f, baseY};
    const Vector2 right{g.x + w * 0.36f, baseY};
    const Vector2 mid{g.x + w * 0.04f, baseY + 1.0f};
    tri(apex, left, mid, P.goldLight);
    tri(apex, mid, right, P.gold);
    DrawLineEx(apex, left, 1.5f, P.goldDark);
    DrawLineEx(apex, right, 1.5f, P.goldDark);
    DrawLineEx(left, right, 1.5f, P.goldDark);
}

void drawStack(Vector2 g, Stack st, double time, bool withCap) {
    if (st.empty()) {
        return;
    }
    float elev = 0.0f;
    bool first = true;
    for (int s = kLarge; s >= kSmall; --s) {
        if (!st.has(s)) {
            continue;
        }
        drawBlock(g, s, elev, 1.0f, first);
        first = false;
        elev += stepUp(s);
    }
    if (withCap && st.isPyramid()) {
        drawCap(g, elev - stepUp(kSmall), 1.5f * static_cast<float>(std::sin(time * 2.2)));
    }
}

bool hedgeAt(const WorldDef& d, Vec2i p) { return !d.inBounds(p) || d.at(p) == Terrain::Hedge; }

bool landAt(const WorldDef& d, Vec2i p) {
    if (!d.inBounds(p)) {
        return false;
    }
    const Terrain t = d.at(p);
    return t != Terrain::Hedge && t != Terrain::Water;
}

bool fenceAt(const WorldDef& d, Vec2i p) {
    if (!d.inBounds(p) || d.at(p) != Terrain::Hedge) {
        return false;
    }
    for (int dy = -1; dy <= 1; ++dy) {
        for (int dx = -1; dx <= 1; ++dx) {
            if ((dx != 0 || dy != 0) && landAt(d, {p.x + dx, p.y + dy})) {
                return true;
            }
        }
    }
    return false;
}

bool waterAt(const WorldDef& d, Vec2i p) {
    if (!d.inBounds(p)) {
        return true;
    }
    const Terrain t = d.at(p);
    return t == Terrain::Water || (t == Terrain::Hedge && !fenceAt(d, p));
}

constexpr float kRailLow = 0.16f;
constexpr float kRailHigh = 0.34f;
constexpr float kPostH = 0.46f;

Vector2 fenceFoot(Vec2i p) {
    const Vector2 c = center(p);
    return {c.x, c.y + TH * 0.12f};
}

void rail(Vector2 a, Vector2 b) {
    for (const float hgt : {kRailLow, kRailHigh}) {
        const Vector2 ra{a.x, a.y - hgt * TS};
        const Vector2 rb{b.x, b.y - hgt * TS};
        DrawLineEx({ra.x, ra.y + 2.0f}, {rb.x, rb.y + 2.0f}, 3.2f, al(BLACK, 0.12f));
        DrawLineEx(ra, rb, 3.4f, P.wood);
        DrawLineEx({ra.x, ra.y - 1.0f}, {rb.x, rb.y - 1.0f}, 1.2f, sh(P.wood, 1.3f));
    }
}

void drawFence(const WorldDef& d, Vec2i p) {
    const Vector2 f = fenceFoot(p);
    const auto joins = [&](Vec2i q) { return fenceAt(d, q) || (d.inBounds(q) && d.at(q) == Terrain::Gate); };
    const Vec2i right{p.x + 1, p.y};
    const Vec2i down{p.x, p.y + 1};
    if (joins(right)) {
        const bool gate = d.at(right) == Terrain::Gate;
        const Vector2 to{gate ? f.x + TS * 0.45f : f.x + TS, f.y};
        ellipse({(f.x + to.x) * 0.5f, f.y + 3.0f}, (to.x - f.x) * 0.5f, 3.0f, al(BLACK, 0.08f));
        for (int k = 1; k <= 3; ++k) {
            const float x = f.x + (to.x - f.x) * static_cast<float>(k) / 4.0f;
            if (gate && k == 3) {
                continue;
            }
            DrawLineEx({x, f.y}, {x, f.y - kRailHigh * TS - 4.0f}, 2.2f, sh(P.wood, 0.9f));
        }
        rail(f, to);
    }
    if (joins(down)) {
        const bool gate = d.at(down) == Terrain::Gate;
        const Vector2 to{f.x, gate ? f.y + TH * 0.35f : f.y + TH};
        for (int k = 1; k <= 2; ++k) {
            const float y = f.y + (to.y - f.y) * static_cast<float>(k) / 3.0f;
            DrawLineEx({f.x, y}, {f.x, y - kRailHigh * TS - 4.0f}, 2.2f, sh(P.wood, 0.9f));
        }
        rail(f, to);
    }
    const float w = 0.14f * TS;
    const float h = kPostH * TS;
    ellipse({f.x + 3.0f, f.y + 1.0f}, w * 0.9f, w * 0.3f, al(BLACK, 0.18f));
    DrawRectangleRounded({f.x - w * 0.5f, f.y - h, w, h}, 0.3f, 4, P.stoneFront);
    DrawRectangleRec({f.x + w * 0.1f, f.y - h + 2.0f, w * 0.4f, h - 3.0f}, al(P.stoneEdge, 0.25f));
    DrawRectangleRounded({f.x - w * 0.7f, f.y - h - 5.0f, w * 1.4f, 7.0f}, 0.5f, 4, P.stoneTop);
    DrawRectangleRoundedLinesEx({f.x - w * 0.5f, f.y - h, w, h}, 0.3f, 4, 1.1f, al(P.stoneEdge, 0.8f));
}

void drawWaterLife(const WorldDef& d, Vec2i p, double time) {
    const unsigned h = hashCell(p.x, p.y, 21u);
    const Vector2 c = center(p);
    bool shore = false;
    for (Dir dir : kDirs) {
        shore = shore || !waterAt(d, p + offset(dir));
    }
    if (shore && h % 3u == 0u) {
        const Vector2 base{c.x + (hf(h, 4) - 0.5f) * TS * 0.4f, c.y + (hf(h, 12) - 0.5f) * TH * 0.3f};
        const float sway = static_cast<float>(std::sin(time * 1.6 + p.x * 0.7 + p.y)) * 2.5f;
        for (int k = 0; k < 5; ++k) {
            const float ox = (static_cast<float>(k) - 2.0f) * 5.0f;
            const float hgt = TS * (0.45f + 0.2f * hf(h, k * 3 + 1));
            const Vector2 tip{base.x + ox * 1.6f + sway, base.y - hgt};
            DrawLineEx({base.x + ox, base.y}, tip, 2.0f, P.frond);
            DrawCircleV(tip, 4.5f, P.frondLight);
            DrawCircleV({tip.x - 1.5f, tip.y - 1.5f}, 2.0f, sh(P.frondLight, 1.2f));
        }
        ellipse({base.x, base.y + 1.0f}, 14.0f, 4.0f, al(P.frond, 0.6f));
    } else if (!shore && h % 5u == 1u) {
        const Vector2 pad{c.x + (hf(h, 6) - 0.5f) * TS * 0.5f, c.y + (hf(h, 14) - 0.5f) * TH * 0.4f};
        ellipse(pad, 11.0f, 6.0f, P.frond);
        tri(pad, {pad.x + 11.0f, pad.y - 2.0f}, {pad.x + 11.0f, pad.y + 2.0f}, P.water);
        if (h % 2u == 0u) {
            DrawCircleV({pad.x - 3.0f, pad.y - 2.0f}, 3.0f, Color{246, 196, 214, 255});
            DrawCircleV({pad.x - 3.0f, pad.y - 2.0f}, 1.4f, P.goldLight);
        }
    }
}

void drawPalm(Vec2i p, double time) {
    const Vector2 c = center(p);
    const unsigned h = hashCell(p.x, p.y, 7u);
    const float lean = (hf(h, 0) - 0.5f) * 0.5f * TS;
    ellipse({c.x + TS * 0.35f, c.y + TS * 0.22f}, TS * 0.55f, TS * 0.16f, al(BLACK, 0.12f));
    const Vector2 base{c.x, c.y + 0.18f * TS};
    const Vector2 top{c.x + lean, c.y - 1.35f * TS};
    constexpr int kSeg = 22;
    for (int i = 0; i < kSeg; ++i) {
        const float t = static_cast<float>(i) / (kSeg - 1);
        const float bend = std::sin(t * kPi) * 0.1f * TS;
        const Vector2 q{base.x + (top.x - base.x) * t * t + bend, base.y + (top.y - base.y) * t};
        const float r = (0.1f - 0.035f * t) * TS;
        DrawCircleV(q, r, (i / 3) % 2 ? P.trunkDark : P.trunk);
        DrawCircleV({q.x - r * 0.35f, q.y}, r * 0.35f, al(WHITE, 0.12f));
    }
    const float sway = static_cast<float>(std::sin(time * 1.3 + p.x)) * 0.05f;
    for (int k = 0; k < 8; ++k) {
        const float a = static_cast<float>(k) / 8.0f * 2.0f * kPi + 0.2f + sway;
        Vector2 prev = top;
        Vector2 dir{std::cos(a) * 1.1f, std::sin(a) * 0.45f - 0.45f};
        for (int s = 0; s < 6; ++s) {
            const Vector2 next{prev.x + dir.x * 0.15f * TS, prev.y + dir.y * 0.15f * TS};
            const float thick = (0.19f - 0.027f * static_cast<float>(s)) * TS;
            DrawLineEx(prev, next, thick, P.frond);
            DrawLineEx({prev.x, prev.y - 2.0f}, {next.x, next.y - 2.0f}, thick * 0.4f, P.frondLight);
            DrawCircleV(next, thick * 0.5f, P.frond);
            prev = next;
            dir.y += 0.3f;
        }
    }
    DrawCircleV({top.x - 5.0f, top.y + 6.0f}, TS * 0.06f, P.trunkDark);
    DrawCircleV({top.x + 5.0f, top.y + 7.0f}, TS * 0.06f, P.trunkDark);
}

void drawCactus(Vec2i p) {
    const Vector2 c = center(p);
    ellipse({c.x + 6.0f, c.y + TS * 0.2f}, TS * 0.3f, TS * 0.1f, al(BLACK, 0.14f));
    const float w = 0.24f * TS;
    const Rectangle trunk{c.x - w * 0.5f, c.y - 0.72f * TS, w, 0.92f * TS};
    const Rectangle armL{c.x - 0.36f * TS, c.y - 0.5f * TS, 0.15f * TS, 0.34f * TS};
    const Rectangle armLh{c.x - 0.36f * TS, c.y - 0.26f * TS, 0.3f * TS, 0.12f * TS};
    const Rectangle armR{c.x + 0.21f * TS, c.y - 0.62f * TS, 0.15f * TS, 0.36f * TS};
    const Rectangle armRh{c.x + 0.06f * TS, c.y - 0.36f * TS, 0.3f * TS, 0.12f * TS};
    for (const Rectangle& r : {armLh, armRh, armL, armR, trunk}) {
        DrawRectangleRounded({r.x, r.y + 2.0f, r.width, r.height}, 1.0f, 8, P.cactusDark);
        DrawRectangleRounded(r, 1.0f, 8, P.cactus);
    }
    DrawLineEx({c.x - w * 0.18f, trunk.y + 8.0f}, {c.x - w * 0.18f, trunk.y + trunk.height - 6.0f}, 2.0f, P.cactusLight);
    DrawLineEx({c.x + w * 0.2f, trunk.y + 10.0f}, {c.x + w * 0.2f, trunk.y + trunk.height - 6.0f}, 1.5f, al(P.cactusDark, 0.7f));
    DrawCircleV({c.x + 2.0f, trunk.y + 2.0f}, 3.0f, Color{240, 120, 140, 255});
}

void drawRock(Vec2i p) {
    const Vector2 c = center(p);
    const unsigned h = hashCell(p.x, p.y, 3u);
    ellipse({c.x + 4.0f, c.y + TS * 0.2f}, TS * 0.4f, TS * 0.12f, al(BLACK, 0.15f));
    const Vector2 a{c.x - 0.12f * TS, c.y - 0.08f * TS};
    const Vector2 b{c.x + 0.14f * TS, c.y - 0.02f * TS};
    const float ra = (0.26f + 0.06f * hf(h, 0)) * TS;
    const float rb = (0.2f + 0.05f * hf(h, 8)) * TS;
    DrawCircleV({a.x, a.y + 4.0f}, ra, P.rockDark);
    DrawCircleV({b.x, b.y + 4.0f}, rb, P.rockDark);
    DrawCircleV(a, ra * 0.95f, P.rock);
    DrawCircleV(b, rb * 0.95f, P.rock);
    DrawCircleV({a.x - ra * 0.3f, a.y - ra * 0.35f}, ra * 0.35f, P.rockLight);
    DrawCircleV({b.x - rb * 0.25f, b.y - rb * 0.35f}, rb * 0.25f, P.rockLight);
}

void drawUrn(Vec2i p) {
    const Vector2 c = center(p);
    ellipse({c.x + 3.0f, c.y + TS * 0.2f}, TS * 0.3f, TS * 0.1f, al(BLACK, 0.16f));
    const Vector2 body{c.x, c.y - 0.1f * TS};
    const float r = 0.26f * TS;
    DrawCircleV({body.x, body.y + 2.0f}, r, P.urnDark);
    DrawCircleV(body, r * 0.95f, P.urn);
    DrawRectangle(static_cast<int>(body.x - r * 0.9f), static_cast<int>(body.y - 3.0f), static_cast<int>(r * 1.8f), 6, P.urnBand);
    DrawRectangleRounded({body.x - r * 0.4f, body.y - r * 1.55f, r * 0.8f, r * 0.8f}, 0.3f, 6, P.urn);
    ellipse({body.x, body.y - r * 1.5f}, r * 0.55f, r * 0.18f, P.urnDark);
    ellipse({body.x, body.y - r * 1.55f}, r * 0.4f, r * 0.1f, sh(P.urnDark, 0.6f));
    DrawCircleV({body.x - r * 0.4f, body.y - r * 0.4f}, r * 0.22f, al(WHITE, 0.25f));
}

void drawPylon(Vector2 base, float w, float h, bool gilded, double time) {
    const float tw = w * 0.74f;
    const Vector2 bl{base.x - w * 0.5f, base.y};
    const Vector2 br{base.x + w * 0.5f, base.y};
    const Vector2 tl{base.x - tw * 0.5f, base.y - h};
    const Vector2 tr{base.x + tw * 0.5f, base.y - h};
    ellipse({base.x + 4.0f, base.y + 2.0f}, w * 0.7f, w * 0.18f, al(BLACK, 0.16f));
    tri(tl, bl, br, P.stoneFront);
    tri(tl, br, tr, P.stoneFront);
    tri({tr.x, tr.y}, {br.x, br.y}, {br.x - w * 0.12f, br.y}, sh(P.stoneFront, 0.85f));
    DrawRectangleRounded({tl.x - 4.0f, tl.y - 7.0f, tw + 8.0f, 9.0f}, 0.4f, 4, P.stoneTop);
    DrawRectangleRoundedLinesEx({tl.x - 4.0f, tl.y - 7.0f, tw + 8.0f, 9.0f}, 0.4f, 4, 1.2f, al(P.stoneEdge, 0.8f));
    DrawLineEx(tl, bl, 1.4f, al(P.stoneEdge, 0.8f));
    DrawLineEx(tr, br, 1.4f, al(P.stoneEdge, 0.8f));
    for (int k = 0; k < 3; ++k) {
        const float y = base.y - h * (0.3f + 0.2f * static_cast<float>(k));
        DrawRectangleRec({base.x - 3.0f, y, 6.0f, 2.5f}, al(P.stoneEdge, 0.55f));
        DrawCircleV({base.x + (k % 2 ? 5.0f : -5.0f), y + 7.0f}, 1.8f, al(P.stoneEdge, 0.5f));
    }
    if (gilded) {
        const float glow = 0.6f + 0.4f * static_cast<float>(std::sin(time * 2.5 + base.x));
        DrawCircleV({base.x, tl.y - 12.0f}, 6.0f, al(P.goldLight, 0.35f * glow));
        DrawCircleV({base.x, tl.y - 12.0f}, 3.8f, P.gold);
    }
}

void drawGate(const WorldDef& d, const WorldState& s, Vec2i p, double time) {
    const Vector2 c = center(p);
    const bool across = hedgeAt(d, {p.x - 1, p.y}) || blocksPlayer(d.at({p.x - 1, p.y}));
    int room = 0;
    for (Dir dir : kDirs) {
        room = std::max(room, d.roomAt(p + offset(dir)));
    }
    const bool solved = room > 0 && s.solved[static_cast<std::size_t>(room)] != 0;
    const int lock = d.lockAt(p);
    const bool locked = lock > s.solvedCount();
    const float h = 0.95f * TS;
    const float w = 0.3f * TS;
    const float groundY = c.y + TH * 0.5f;
    if (across) {
        const Vector2 left{c.x - 0.52f * TS, groundY};
        const Vector2 right{c.x + 0.52f * TS, groundY};
        if (locked) {
            const Rectangle slab{c.x - 0.4f * TS, groundY - h * 0.88f, 0.8f * TS, h * 0.88f};
            DrawRectangleRec(slab, sh(P.stoneFront, 0.92f));
            DrawRectangleLinesEx(slab, 1.5f, al(P.stoneEdge, 0.9f));
            const Vector2 ic{c.x, slab.y + slab.height * 0.42f};
            tri({ic.x, ic.y - 14.0f}, {ic.x - 11.0f, ic.y + 4.0f}, {ic.x + 11.0f, ic.y + 4.0f}, P.goldDark);
            const std::string n = std::to_string(lock);
            DrawText(n.c_str(), static_cast<int>(c.x - 5.0f), static_cast<int>(ic.y + 9.0f), 18, P.goldDark);
        }
        drawPylon(left, w, h, solved, time);
        drawPylon(right, w, h, solved, time);
        const Rectangle lintel{left.x - w * 0.45f, groundY - h - 16.0f, right.x - left.x + w * 0.9f, 14.0f};
        DrawRectangleRounded({lintel.x, lintel.y - 5.0f, lintel.width, 7.0f}, 0.4f, 4, P.stoneTop);
        DrawRectangleRec(lintel, P.stoneFront);
        DrawRectangleLinesEx(lintel, 1.3f, al(P.stoneEdge, 0.85f));
        const Vector2 disk{c.x, lintel.y + lintel.height * 0.5f};
        const Color diskColor = solved ? P.gold : sh(P.stoneFront, 0.8f);
        DrawLineEx({disk.x - 16.0f, disk.y - 1.0f}, {disk.x - 5.0f, disk.y + 2.0f}, 3.0f, diskColor);
        DrawLineEx({disk.x + 16.0f, disk.y - 1.0f}, {disk.x + 5.0f, disk.y + 2.0f}, 3.0f, diskColor);
        DrawCircleV(disk, 5.0f, diskColor);
        return;
    }
    const Vector2 back{c.x, c.y - TH * 0.42f};
    const Vector2 front{c.x, groundY + 2.0f};
    drawPylon(back, w * 0.9f, h, solved, time);
    if (locked) {
        const Rectangle slab{c.x - 0.1f * TS, back.y - h * 0.8f, 0.2f * TS, front.y - back.y + h * 0.8f};
        DrawRectangleRec(slab, sh(P.stoneFront, 0.9f));
        DrawRectangleLinesEx(slab, 1.4f, al(P.stoneEdge, 0.9f));
    }
    const Rectangle beam{c.x - w * 0.42f, back.y - h - 10.0f, w * 0.84f, front.y - back.y + 12.0f};
    DrawRectangleRec(beam, P.stoneTop);
    DrawRectangleLinesEx(beam, 1.3f, al(P.stoneEdge, 0.85f));
    drawPylon(front, w * 0.9f, h, solved, time);
    if (locked) {
        const Rectangle sign{c.x - 0.2f * TS, front.y - h * 0.62f, 0.4f * TS, 0.34f * TS};
        DrawRectangleRec(sign, sh(P.stoneFront, 0.95f));
        DrawRectangleLinesEx(sign, 1.3f, al(P.stoneEdge, 0.9f));
        const Vector2 ic{sign.x + sign.width * 0.32f, sign.y + sign.height * 0.72f};
        tri({ic.x, ic.y - 11.0f}, {ic.x - 7.0f, ic.y}, {ic.x + 7.0f, ic.y}, P.goldDark);
        const std::string n = std::to_string(lock);
        DrawText(n.c_str(), static_cast<int>(sign.x + sign.width * 0.58f), static_cast<int>(sign.y + 3.0f), 17, P.goldDark);
    }
}

void drawAltar(Vec2i p, bool lit, double time) {
    const Vector2 c = center(p);
    const float r = 0.4f * TS;
    const Rectangle slab{c.x - r, c.y - r * 0.7f, r * 2.0f, r * 1.45f};
    DrawRectangleRounded({slab.x, slab.y + 4.0f, slab.width, slab.height}, 0.3f, 6, P.stoneEdge);
    DrawRectangleRounded(slab, 0.3f, 6, P.stoneTop);
    const float glow = lit ? 0.5f + 0.5f * static_cast<float>(std::sin(time * 3.0)) : 0.0f;
    const Color line = lit ? Color{255, static_cast<unsigned char>(200 + 40 * glow), 90, 255} : P.goldDark;
    const Vector2 top{c.x, c.y - r * 0.6f};
    const Vector2 right{c.x + r * 0.7f, c.y + 0.05f * TS};
    const Vector2 bottom{c.x, c.y + r * 0.7f};
    const Vector2 left{c.x - r * 0.7f, c.y + 0.05f * TS};
    DrawLineEx(top, right, 2.5f, line);
    DrawLineEx(right, bottom, 2.5f, line);
    DrawLineEx(bottom, left, 2.5f, line);
    DrawLineEx(left, top, 2.5f, line);
    DrawCircleV({c.x, c.y + 0.02f * TS}, r * 0.16f, line);
}

void drawPlayer(Vector2 g, Dir facing, float walk, float squash, double time) {
    ellipse({g.x, g.y + 0.2f * TS}, 0.26f * TS, 0.08f * TS, al(BLACK, 0.22f));
    const float fx = facing == Dir::Left ? -1.0f : facing == Dir::Right ? 1.0f : 0.0f;
    const float swing = std::sin(walk * 2.0f * kPi) * 0.07f * TS;
    const float bob = std::abs(std::sin(walk * 2.0f * kPi)) * -0.03f * TS;
    const float legT = 0.075f * TS;
    const float legSpread = fx == 0.0f ? 0.1f * TS : 0.06f * TS;
    DrawLineEx({g.x - legSpread, g.y - 0.1f * TS + bob}, {g.x - legSpread + swing, g.y + 0.18f * TS}, legT, P.ink);
    DrawLineEx({g.x + legSpread, g.y - 0.1f * TS + bob}, {g.x + legSpread - swing, g.y + 0.18f * TS}, legT, P.ink);
    DrawCircleV({g.x - legSpread + swing, g.y + 0.18f * TS}, legT * 0.55f, P.ink);
    DrawCircleV({g.x + legSpread - swing, g.y + 0.18f * TS}, legT * 0.55f, P.ink);
    const Vector2 body{g.x - fx * 0.03f * TS, g.y - 0.2f * TS + bob};
    const float rx = 0.25f * TS * (1.0f + squash * 0.25f);
    const float ry = 0.21f * TS * (1.0f - squash * 0.25f);
    if (fx != 0.0f) {
        DrawLineEx({body.x - fx * rx * 0.8f, body.y}, {body.x - fx * rx * 1.25f, body.y - 0.18f * TS}, 5.0f, P.ink);
        DrawCircleV({body.x - fx * rx * 1.25f, body.y - 0.2f * TS}, 4.0f, P.ink);
    }
    ellipse(body, rx, ry, P.ink);
    ellipse({body.x - rx * 0.3f, body.y - ry * 0.45f}, rx * 0.35f, ry * 0.2f, al(P.inkLight, 0.8f));
    const Vector2 head{body.x + fx * 0.2f * TS, body.y - 0.24f * TS + (facing == Dir::Up ? -0.02f * TS : 0.0f)};
    const float hr = 0.15f * TS;
    tri({head.x - hr * 0.85f, head.y - hr * 0.2f}, {head.x - hr * 0.55f, head.y - hr * 1.65f}, {head.x - hr * 0.05f, head.y - hr * 0.6f}, P.ink);
    tri({head.x + hr * 0.85f, head.y - hr * 0.2f}, {head.x + hr * 0.55f, head.y - hr * 1.65f}, {head.x + hr * 0.05f, head.y - hr * 0.6f}, P.ink);
    DrawCircleV(head, hr, P.ink);
    if (facing == Dir::Up) {
        return;
    }
    const bool blink = std::fmod(time + 0.7, 3.7) < 0.12;
    const float er = std::max(1.5f, hr * 0.2f);
    const auto eye = [&](Vector2 e) {
        if (blink) {
            DrawLineEx({e.x - er, e.y}, {e.x + er, e.y}, 1.6f, P.eye);
        } else {
            DrawCircleV(e, er, P.eye);
        }
    };
    if (fx != 0.0f) {
        eye({head.x + fx * hr * 0.45f, head.y + hr * 0.05f});
    } else {
        eye({head.x - hr * 0.38f, head.y + hr * 0.1f});
        eye({head.x + hr * 0.38f, head.y + hr * 0.1f});
    }
}

}

Renderer::Renderer() {
    static constexpr std::array<const char*, 4> paths{
        "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/truetype/liberation/LiberationSans-Bold.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf"};
    for (const char* path : paths) {
        if (FileExists(path)) {
            font_ = LoadFontEx(path, 96, nullptr, 0);
            if (font_.texture.id != 0) {
                GenTextureMipmaps(&font_.texture);
                SetTextureFilter(font_.texture, TEXTURE_FILTER_TRILINEAR);
                ownsFont_ = true;
                return;
            }
        }
    }
    font_ = GetFontDefault();
}

Renderer::~Renderer() {
    if (ownsFont_) {
        UnloadFont(font_);
    }
}

float Renderer::random01() {
    rng_ ^= rng_ << 13;
    rng_ ^= rng_ >> 17;
    rng_ ^= rng_ << 5;
    return static_cast<float>(rng_ & 0xFFFFFFu) / static_cast<float>(0xFFFFFF);
}

void Renderer::spawnPuff(Vector2 at, Color color, int count, float speed, float lift) {
    for (int i = 0; i < count; ++i) {
        const float a = random01() * 2.0f * kPi;
        const float v = speed * (0.4f + random01() * 0.6f);
        Particle p;
        p.pos = {at.x + std::cos(a) * 6.0f, at.y + std::sin(a) * 4.0f};
        p.vel = {std::cos(a) * v, std::sin(a) * v * 0.5f};
        p.z = 2.0f;
        p.vz = lift * (0.5f + random01());
        p.maxLife = p.life = 0.35f + random01() * 0.35f;
        p.size = 3.0f + random01() * 4.0f;
        p.color = color;
        particles_.push_back(p);
    }
}

void Renderer::spawnConfetti(Vector2 at) {
    constexpr std::array<Color, 4> colors{P.gold, P.goldLight, Color{226, 110, 80, 255}, Color{96, 170, 190, 255}};
    for (int i = 0; i < 70; ++i) {
        const float a = random01() * 2.0f * kPi;
        const float v = 60.0f + random01() * 160.0f;
        Particle p;
        p.pos = at;
        p.vel = {std::cos(a) * v, std::sin(a) * v * 0.5f};
        p.z = 40.0f;
        p.vz = 250.0f + random01() * 300.0f;
        p.maxLife = p.life = 1.2f + random01() * 0.8f;
        p.size = 2.5f + random01() * 3.0f;
        p.color = colors[static_cast<std::size_t>(i) % colors.size()];
        particles_.push_back(p);
    }
}

void Renderer::desiredCamera(const Game& game, Vector2& target, float& zoom) const {
    const float sw = static_cast<float>(GetScreenWidth());
    const float shh = static_cast<float>(GetScreenHeight());
    const int room = game.currentRoom();
    if (room > 0) {
        const RoomInfo& info = game.def().rooms[static_cast<std::size_t>(room)];
        const float x0 = static_cast<float>(info.min.x - 1) * TS;
        const float y0 = static_cast<float>(info.min.y - 1) * TH - TS;
        const float x1 = static_cast<float>(info.max.x + 2) * TS;
        const float y1 = static_cast<float>(info.max.y + 2) * TH;
        target = {(x0 + x1) * 0.5f, (y0 + y1) * 0.5f - TS * 0.2f};
        zoom = std::clamp(std::min(sw / ((x1 - x0) * 1.25f), shh / ((y1 - y0) * 1.45f)), 0.4f, 2.4f);
    } else {
        target = center(game.state().player);
        zoom = std::clamp(shh / (11.0f * TH), 0.4f, 2.0f);
    }
    const Vector2 p = target;
    const float halfW = sw * 0.5f / zoom;
    const float halfH = shh * 0.5f / zoom;
    const float worldW = static_cast<float>(game.def().width) * TS;
    const float worldH = static_cast<float>(game.def().height) * TH;
    target.x = worldW > halfW * 2.0f ? std::clamp(p.x, halfW, worldW - halfW) : worldW * 0.5f;
    target.y = worldH > halfH * 2.0f ? std::clamp(p.y, halfH, worldH - halfH) : worldH * 0.5f;
}

Camera2D Renderer::camera() const {
    Camera2D cam{};
    cam.offset = {static_cast<float>(GetScreenWidth()) * 0.5f, static_cast<float>(GetScreenHeight()) * 0.5f};
    cam.target = camTarget_;
    cam.zoom = camZoom_;
    return cam;
}

void Renderer::snap(const Game& game) {
    animT_ = 1.0f;
    bumpT_ = 1.0f;
    desiredCamera(game, camTarget_, camZoom_);
    lastRoom_ = game.currentRoom();
}

void Renderer::onMove(const Game&, const MoveEvent& event) {
    if (!event.moved) {
        return;
    }
    anim_ = event;
    animT_ = 0.0f;
    if (event.pushed) {
        const Vector2 at = center(event.blockTo);
        if (event.clearedSand) {
            spawnPuff({at.x, at.y + 0.1f * TS}, P.sand, 14, 90.0f, 120.0f);
        }
        if (event.stacked) {
            spawnPuff({at.x, at.y + 0.1f * TS}, P.stoneTop, 6, 50.0f, 90.0f);
        }
        if (event.solvedRoom > 0) {
            spawnConfetti({at.x, at.y - 0.5f * TS});
        }
    }
}

void Renderer::onBlocked(Dir dir) {
    bumpT_ = 0.0f;
    bumpDir_ = dir;
}

void Renderer::banner(std::string title, std::string subtitle, float seconds) {
    bannerTitle_ = std::move(title);
    bannerSub_ = std::move(subtitle);
    bannerTime_ = 0.0f;
    bannerDuration_ = seconds;
}

void Renderer::update(const Game& game, float dt) {
    time_ += dt;
    if (animT_ < 1.0f) {
        animT_ = std::min(1.0f, animT_ + dt / kAnimTime);
        walkPhase_ += dt / (kAnimTime * 2.0f);
    }
    bumpT_ = std::min(1.0f, bumpT_ + dt / kBumpTime);
    Vector2 target{};
    float zoom = 1.0f;
    desiredCamera(game, target, zoom);
    const float k = 1.0f - std::exp(-dt * 6.0f);
    camTarget_ = lerpV(camTarget_, target, k);
    camZoom_ += (zoom - camZoom_) * k;
    for (auto& p : particles_) {
        p.life -= dt;
        p.pos.x += p.vel.x * dt;
        p.pos.y += p.vel.y * dt;
        p.vel.x *= 1.0f - std::min(1.0f, dt * 2.5f);
        p.vel.y *= 1.0f - std::min(1.0f, dt * 2.5f);
        p.z += p.vz * dt;
        p.vz -= 700.0f * dt;
        if (p.z < 0.0f) {
            p.z = 0.0f;
            p.vz = 0.0f;
        }
    }
    std::erase_if(particles_, [](const Particle& p) { return p.life <= 0.0f; });
    if (bannerTime_ < bannerDuration_) {
        bannerTime_ += dt;
    }
    const int room = game.currentRoom();
    if (room != lastRoom_) {
        lastRoom_ = room;
        if (room > 0) {
            roomTitleId_ = room;
            roomTitleTime_ = 0.0f;
        }
    }
    roomTitleTime_ += dt;
}

void Renderer::drawGround(const Game& game, int x0, int y0, int x1, int y1) const {
    const WorldDef& d = game.def();
    const WorldState& s = game.state();
    DrawRectangleRec({static_cast<float>(x0) * TS, static_cast<float>(y0) * TH, static_cast<float>(x1 - x0 + 1) * TS,
                      static_cast<float>(y1 - y0 + 1) * TH},
                     P.sand);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Vec2i p{x, y};
            if (d.at(p) == Terrain::Hedge) {
                continue;
            }

        }
    }
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Vec2i p{x, y};
            if (waterAt(d, p)) {
                continue;
            }
            const unsigned h = hashCell(x, y, 11u);
            const Vector2 c = center(p);
            for (int k = 0; k < 2; ++k) {
                if (((h >> (k * 3)) & 3u) == 0u) {
                    continue;
                }
                const float ox = (hf(h, 4 + k * 8) - 0.5f) * TS * 0.5f;
                const float oy = (static_cast<float>(k) - 0.5f) * TH * 0.5f + (hf(h, 12 + k * 8) - 0.5f) * TH * 0.2f;
                const float len = TS * (0.2f + 0.15f * hf(h, 20 + k));
                Vector2 prev{c.x + ox - len, c.y + oy + 2.0f};
                for (int i = 1; i <= 6; ++i) {
                    const float t = static_cast<float>(i) / 6.0f;
                    const Vector2 next{c.x + ox - len + 2.0f * len * t, c.y + oy + 2.0f - std::sin(t * kPi) * 4.0f};
                    DrawLineEx(prev, next, 1.6f, al(P.sandDark, 0.55f));
                    DrawLineEx({prev.x, prev.y - 1.6f}, {next.x, next.y - 1.6f}, 1.2f, al(P.sandLight, 0.9f));
                    prev = next;
                }
            }
            if (h % 7u == 0u) {
                DrawCircleV({c.x + (hf(h, 9) - 0.5f) * TS * 0.7f, c.y + (hf(h, 17) - 0.5f) * TH * 0.7f}, 2.0f, P.sandSpeck);
            }
        }
    }
    const auto blobPass = [&](float radius, Color color, auto&& pred) {
        const float rx = radius * TS;
        const float ry = radius * TH;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const Vec2i p{x, y};
                if (!pred(p)) {
                    continue;
                }
                const Vector2 c = center(p);
                ellipse(c, rx, ry, color);
                const bool right = pred(Vec2i{x + 1, y});
                const bool down = pred(Vec2i{x, y + 1});
                if (right) {
                    DrawRectangleRec({c.x, c.y - ry, TS, ry * 2.0f}, color);
                }
                if (down) {
                    DrawRectangleRec({c.x - rx, c.y, rx * 2.0f, TH}, color);
                }
                if (right && down && pred(Vec2i{x + 1, y + 1})) {
                    DrawRectangleRec({c.x, c.y, TS, TH}, color);
                }
            }
        }
    };
    const auto bare = [&](Vec2i p) { return isBare(d, s, p); };
    const auto water = [&](Vec2i p) { return d.inBounds(p) && waterAt(d, p); };
    blobPass(0.54f, al(P.sandRim, 0.75f), bare);
    blobPass(0.47f, P.bareDark, bare);
    blobPass(0.44f, P.bare, bare);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Vec2i p{x, y};
            if (!bare(p)) {
                continue;
            }
            const float cx0 = static_cast<float>(x) * TS;
            const float cy0 = static_cast<float>(y) * TH;
            const unsigned h = hashCell(x, y, 5u);
            const bool r = bare({x + 1, y});
            const bool dn = bare({x, y + 1});
            const bool up = bare({x, y - 1});
            const bool lf = bare({x - 1, y});
            if (r && dn && up && lf) {
                const float tint = hf(h, 2);
                DrawRectangleRec({cx0 + 2.0f, cy0 + 2.0f, TS - 4.0f, TH - 4.0f}, al(tint > 0.5f ? P.paveLight : P.bareDark, 0.25f * std::abs(tint - 0.5f) * 2.0f));
            }
            if (r) {
                const bool stagger = (y % 2) == 0;
                const float gx = cx0 + TS + (stagger ? -TS * 0.5f : 0.0f);
                DrawLineEx({gx, cy0 + 3.0f}, {gx, cy0 + TH - 3.0f}, 1.6f, al(P.bareDark, 0.9f));
            }
            if (dn) {
                DrawLineEx({cx0 + (lf ? 0.0f : TS * 0.3f), cy0 + TH}, {cx0 + (r ? TS : TS * 0.7f), cy0 + TH}, 1.8f, al(P.bareDark, 0.9f));
            }
            if (up) {
                DrawLineEx({cx0 + (lf ? 0.0f : TS * 0.3f), cy0 + 2.0f}, {cx0 + (r ? TS : TS * 0.7f), cy0 + 2.0f}, 1.0f, al(P.paveLight, 0.8f));
            }
            if (h % 6u == 0u) {
                const Vector2 c = center(p);
                const Vector2 a{c.x + (hf(h, 3) - 0.5f) * TS * 0.4f, c.y + (hf(h, 11) - 0.5f) * TH * 0.4f};
                DrawLineEx(a, {a.x + 7.0f, a.y + 3.0f}, 1.0f, al(P.bareDark, 0.7f));
                DrawLineEx({a.x + 7.0f, a.y + 3.0f}, {a.x + 10.0f, a.y + 1.0f}, 1.0f, al(P.bareDark, 0.7f));
            }
        }
    }
    blobPass(0.52f, P.sandRim, water);
    blobPass(0.44f, P.water, water);
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Vec2i p{x, y};
            bool deep = water(p);
            for (int dy = -1; dy <= 1 && deep; ++dy) {
                for (int dx = -1; dx <= 1 && deep; ++dx) {
                    deep = water({x + dx, y + dy});
                }
            }
            if (deep) {
                const Vector2 c = center(p);
                ellipse(c, TS * 0.62f, TH * 0.62f, al(P.waterDeep, 0.55f));
            }
        }
    }
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Vec2i p{x, y};
            const Terrain t = d.at(p);
            const Vector2 c = center(p);
            if (water(p) && hashCell(x, y, 13u) % 3u == 0u) {
                const float ph = static_cast<float>(std::fmod(time_ * 0.6 + hf(hashCell(x, y, 9u), 0), 1.0));
                const float rr = (0.1f + ph * 0.25f) * TS;
                DrawEllipseLines(static_cast<int>(c.x), static_cast<int>(c.y), rr, rr * 0.45f, al(P.waterLight, 1.0f - ph));
            } else if (t == Terrain::Rock || t == Terrain::Urn) {
                const Rectangle plinth{c.x - 0.4f * TS, c.y - 0.34f * TH, 0.8f * TS, 0.72f * TH};
                DrawRectangleRounded({plinth.x, plinth.y + 4.0f, plinth.width, plinth.height}, 0.25f, 6, P.stoneEdge);
                DrawRectangleRounded(plinth, 0.25f, 6, P.stoneTop);
                DrawRectangleRoundedLinesEx(plinth, 0.25f, 6, 1.2f, al(P.stoneEdge, 0.8f));
            } else if (t == Terrain::Altar) {
                const int room = d.roomAt(p);
                drawAltar(p, room > 0 && s.solved[static_cast<std::size_t>(room)] != 0, time_);
            }
        }
    }
    for (const auto& pt : particles_) {
        if (pt.color.r != P.gold.r) {
            continue;
        }
        ellipse({pt.pos.x, pt.pos.y}, pt.size, pt.size * 0.4f, al(BLACK, 0.1f * pt.life / pt.maxLife));
    }
}

void Renderer::drawObjects(const Game& game, int x0, int y0, int x1, int y1) const {
    const WorldDef& d = game.def();
    const WorldState& s = game.state();
    const float t = ease(animT_);
    const bool anim = animT_ < 1.0f && anim_.moved;
    Vector2 playerPos = center(s.player);
    float squash = 0.0f;
    int playerRow = s.player.y;
    if (anim) {
        playerPos = lerpV(center(anim_.playerFrom), center(anim_.playerTo), t);
        playerRow = std::max(anim_.playerFrom.y, anim_.playerTo.y);
        if (anim_.pushed) {
            squash = std::sin(animT_ * kPi) * 0.6f;
            if (anim_.playerFrom == anim_.playerTo) {
                const Vec2i o = anim_.blockTo - anim_.blockFrom;
                const float nudge = std::sin(animT_ * kPi) * 0.18f * TS;
                playerPos.x += static_cast<float>(o.x) * nudge;
                playerPos.y += static_cast<float>(o.y) * nudge;
            }
        }
    }
    if (bumpT_ < 1.0f) {
        const Vec2i o = offset(bumpDir_);
        const float b = std::sin(bumpT_ * kPi) * 0.07f * TS;
        playerPos.x += static_cast<float>(o.x) * b;
        playerPos.y += static_cast<float>(o.y) * b;
        squash = std::max(squash, std::sin(bumpT_ * kPi) * 0.4f);
    }
    const bool movingBlock = anim && anim_.pushed;
    const int blockRow = movingBlock ? std::max(anim_.blockFrom.y, anim_.blockTo.y) : -1;
    const auto drawMovingBlock = [&]() {
        const Vector2 g = lerpV(center(anim_.blockFrom), center(anim_.blockTo), t);
        const float e0 = elevationUnder(Stack{anim_.sourceMask}, anim_.sizeBefore);
        const float e1 = stackHeight(Stack{anim_.destMask});
        const float hop = anim_.stacked || e0 > 0.0f ? std::sin(animT_ * kPi) * 0.2f * TS : 0.0f;
        const int size = animT_ < 0.55f ? anim_.sizeBefore : anim_.sizeAfter;
        const float pop = anim_.sizeAfter != anim_.sizeBefore && animT_ >= 0.55f ? 1.0f + 0.12f * std::sin((animT_ - 0.55f) / 0.45f * kPi) : 1.0f;
        drawBlock(g, size, e0 + (e1 - e0) * t + hop, pop, e0 + (e1 - e0) * t < 1.0f);
    };
    const float walk = anim ? walkPhase_ : 0.0f;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            const Vec2i p{x, y};
            const Vector2 c = center(p);
            switch (d.at(p)) {
                case Terrain::Hedge:
                    if (fenceAt(d, p)) {
                        drawFence(d, p);
                    } else {
                        drawWaterLife(d, p, time_);
                    }
                    break;
                case Terrain::Water: drawWaterLife(d, p, time_); break;
                case Terrain::Palm: drawPalm(p, time_); break;
                case Terrain::Rock: drawRock(p); break;
                case Terrain::Urn: drawUrn(p); break;
                case Terrain::Cactus: drawCactus(p); break;
                default: break;
            }
            Stack st = s.stack(d.index(p));
            if (movingBlock && p == anim_.blockTo) {
                st = Stack{anim_.destMask};
            }
            const int room = d.roomAt(p);
            const bool capped = room > 0 && s.solved[static_cast<std::size_t>(room)] != 0;
            drawStack(c, st, time_ + x * 0.3, capped);
        }
        for (int x = x0; x <= x1; ++x) {
            if (d.at({x, y}) == Terrain::Gate) {
                drawGate(d, s, {x, y}, time_);
            }
        }
        if (y == playerRow && !(movingBlock && blockRow == y && anim_.blockTo.x < anim_.playerTo.x)) {
            drawPlayer(playerPos, s.facing, walk, squash, time_);
            if (movingBlock && blockRow == y) {
                drawMovingBlock();
            }
        } else {
            if (movingBlock && blockRow == y) {
                drawMovingBlock();
            }
            if (y == playerRow) {
                drawPlayer(playerPos, s.facing, walk, squash, time_);
            }
        }
    }
    for (const auto& pt : particles_) {
        const float a = std::clamp(pt.life / pt.maxLife, 0.0f, 1.0f);
        DrawCircleV({pt.pos.x, pt.pos.y - pt.z}, pt.size * (0.5f + 0.5f * a), al(pt.color, a));
    }
}

void Renderer::drawWorld(const Game& game) const {
    const Camera2D cam = camera();
    const Vector2 a = GetScreenToWorld2D({0.0f, 0.0f}, cam);
    const Vector2 b = GetScreenToWorld2D({static_cast<float>(GetScreenWidth()), static_cast<float>(GetScreenHeight())}, cam);
    const WorldDef& d = game.def();
    const int x0 = std::max(0, static_cast<int>(std::floor(a.x / TS)) - 1);
    const int y0 = std::max(0, static_cast<int>(std::floor(a.y / TH)) - 1);
    const int x1 = std::min(d.width - 1, static_cast<int>(std::floor(b.x / TS)) + 1);
    const int y1 = std::min(d.height - 1, static_cast<int>(std::floor(b.y / TH)) + 4);
    BeginMode2D(cam);
    drawGround(game, x0, y0, x1, y1);
    drawObjects(game, x0, y0, x1, y1);
    EndMode2D();
}

void Renderer::text(const std::string& s, Vector2 pos, float size, Color color, bool centered) const {
    const float spacing = size * 0.04f;
    if (centered) {
        const Vector2 m = MeasureTextEx(font_, s.c_str(), size, spacing);
        pos.x -= m.x * 0.5f;
    }
    DrawTextEx(font_, s.c_str(), pos, size, spacing, color);
}

void Renderer::drawHud(const Game& game, bool help, bool muted) const {
    const float sw = static_cast<float>(GetScreenWidth());
    const float shh = static_cast<float>(GetScreenHeight());
    const float u = std::clamp(shh / 800.0f, 0.7f, 1.6f);
    const std::string count = std::to_string(game.solvedCount()) + " / " + std::to_string(game.levelCount());
    const Vector2 m = MeasureTextEx(font_, count.c_str(), 30.0f * u, 1.2f * u);
    const Rectangle pill{sw - m.x - 78.0f * u, 18.0f * u, m.x + 60.0f * u, 46.0f * u};
    DrawRectangleRounded(pill, 1.0f, 10, al(P.ink, 0.55f));
    const Vector2 ic{pill.x + 24.0f * u, pill.y + 33.0f * u};
    tri({ic.x, ic.y - 22.0f * u}, {ic.x - 14.0f * u, ic.y}, {ic.x + 14.0f * u, ic.y}, P.gold);
    tri({ic.x, ic.y - 22.0f * u}, {ic.x, ic.y}, {ic.x + 14.0f * u, ic.y}, P.goldDark);
    text(count, {pill.x + 46.0f * u, pill.y + 8.0f * u}, 30.0f * u, P.text);
    if (muted) {
        text("muted", {pill.x, pill.y + pill.height + 6.0f * u}, 18.0f * u, al(P.textDark, 0.8f));
    }
    if (roomTitleId_ > 0 && roomTitleTime_ < 3.2f) {
        const float a = std::min({1.0f, roomTitleTime_ * 2.0f, (3.2f - roomTitleTime_) * 1.5f});
        const bool solved = game.state().solved[static_cast<std::size_t>(roomTitleId_)] != 0;
        const Vector2 nm = MeasureTextEx(font_, game.roomName(roomTitleId_).c_str(), 40.0f * u, 1.6f * u);
        const float pillH = (solved ? 86.0f : 58.0f) * u;
        DrawRectangleRounded({sw * 0.5f - nm.x * 0.5f - 30.0f * u, 18.0f * u, nm.x + 60.0f * u, pillH}, 0.6f, 10, al(P.ink, 0.55f * a));
        text(game.roomName(roomTitleId_), {sw * 0.5f, 26.0f * u}, 40.0f * u, al(P.text, a), true);
        if (solved) {
            text(game.pyramidName(roomTitleId_), {sw * 0.5f, 72.0f * u}, 22.0f * u, al(P.goldLight, a), true);
        }
    }
    if (bannerTime_ < bannerDuration_) {
        const float a = std::min({1.0f, bannerTime_ * 3.0f, (bannerDuration_ - bannerTime_) * 1.5f});
        const float slide = (1.0f - std::min(1.0f, bannerTime_ * 3.0f)) * 30.0f * u;
        const Vector2 tm = MeasureTextEx(font_, bannerTitle_.c_str(), 38.0f * u, 1.5f * u);
        const float bw = std::max(tm.x, 360.0f * u) + 80.0f * u;
        const Rectangle r{(sw - bw) * 0.5f, shh * 0.72f + slide, bw, 100.0f * u};
        DrawRectangleRounded(r, 0.35f, 10, al(P.ink, 0.8f * a));
        DrawRectangleRoundedLinesEx(r, 0.35f, 10, 2.0f * u, al(P.gold, a));
        text(bannerTitle_, {sw * 0.5f, r.y + 16.0f * u}, 38.0f * u, al(P.goldLight, a), true);
        text(bannerSub_, {sw * 0.5f, r.y + 62.0f * u}, 22.0f * u, al(P.text, a), true);
    }
    if (help) {
        const Rectangle r{sw * 0.5f - 250.0f * u, shh * 0.5f - 170.0f * u, 500.0f * u, 340.0f * u};
        DrawRectangleRounded(r, 0.12f, 10, al(P.ink, 0.88f));
        text("How to play", {sw * 0.5f, r.y + 20.0f * u}, 34.0f * u, P.goldLight, true);
        static constexpr std::array<const char*, 8> lines{
            "Arrows / WASD    walk and push",
            "Z / Backspace    undo",
            "R                reset this garden",
            "M                mute",
            "H                close this help",
            "Rolling a block over sand makes it bigger.",
            "Stack large, medium, small to raise a pyramid.",
            "Every block in a garden must end in a pyramid."};
        for (std::size_t i = 0; i < lines.size(); ++i) {
            text(lines[i], {r.x + 34.0f * u, r.y + 76.0f * u + static_cast<float>(i) * 30.0f * u}, 20.0f * u,
                 i < 5 ? P.text : P.goldLight);
        }
    } else {
        DrawRectangleRounded({12.0f * u, shh - 42.0f * u, 92.0f * u, 30.0f * u}, 1.0f, 8, al(P.ink, 0.4f));
        text("H  help", {26.0f * u, shh - 36.0f * u}, 18.0f * u, al(P.text, 0.85f));
    }
}

void Renderer::drawTitle() const {
    const float sw = static_cast<float>(GetScreenWidth());
    const float shh = static_cast<float>(GetScreenHeight());
    const float u = std::clamp(shh / 800.0f, 0.7f, 1.6f);
    DrawRectangleGradientV(0, 0, static_cast<int>(sw), static_cast<int>(shh), al(Color{60, 36, 20, 255}, 0.55f),
                           al(Color{60, 36, 20, 255}, 0.15f));
    const Vector2 c{sw * 0.5f, shh * 0.3f};
    const float s = 70.0f * u;
    tri({c.x, c.y - s * 1.1f}, {c.x - s, c.y}, {c.x + s, c.y}, P.gold);
    tri({c.x, c.y - s * 1.1f}, {c.x, c.y}, {c.x + s, c.y}, P.goldDark);
    text("PYRAMID MAKER", {sw * 0.5f + 4.0f * u, c.y + 30.0f * u + 5.0f * u}, 76.0f * u, al(BLACK, 0.35f), true);
    text("PYRAMID MAKER", {sw * 0.5f, c.y + 30.0f * u}, 76.0f * u, P.text, true);
    text("a puzzle about sand, stone and patience", {sw * 0.5f, c.y + 118.0f * u}, 26.0f * u, P.goldLight, true);
    const float blink = 0.55f + 0.45f * static_cast<float>(std::sin(time_ * 3.0));
    text("press any key", {sw * 0.5f, shh * 0.8f}, 28.0f * u, al(P.text, blink), true);
}

void Renderer::draw(const Game& game, bool title, bool help, bool muted) const {
    ClearBackground(P.voidColor);
    drawWorld(game);
    if (title) {
        drawTitle();
    } else {
        drawHud(game, help, muted);
    }
}

}
