#pragma once

#include <array>
#include <cstdint>

namespace pm {

enum class Dir : std::uint8_t { Up, Down, Left, Right };

struct Vec2i {
    int x{};
    int y{};
    friend constexpr bool operator==(const Vec2i&, const Vec2i&) = default;
};

constexpr Vec2i operator+(Vec2i a, Vec2i b) { return {a.x + b.x, a.y + b.y}; }
constexpr Vec2i operator-(Vec2i a, Vec2i b) { return {a.x - b.x, a.y - b.y}; }

constexpr Vec2i offset(Dir d) {
    switch (d) {
        case Dir::Up: return {0, -1};
        case Dir::Down: return {0, 1};
        case Dir::Left: return {-1, 0};
        case Dir::Right: return {1, 0};
    }
    return {};
}

inline constexpr std::array<Dir, 4> kDirs{Dir::Up, Dir::Down, Dir::Left, Dir::Right};

inline constexpr int kSmall = 1;
inline constexpr int kMedium = 2;
inline constexpr int kLarge = 3;

class Stack {
public:
    constexpr Stack() = default;
    constexpr explicit Stack(std::uint8_t mask) : mask_(static_cast<std::uint8_t>(mask & 7u)) {}

    [[nodiscard]] constexpr std::uint8_t mask() const { return mask_; }
    [[nodiscard]] constexpr bool empty() const { return mask_ == 0; }
    [[nodiscard]] constexpr bool has(int size) const { return (mask_ >> (size - 1)) & 1u; }
    [[nodiscard]] constexpr int count() const { return has(1) + has(2) + has(3); }
    [[nodiscard]] constexpr int top() const {
        for (int s = kSmall; s <= kLarge; ++s) {
            if (has(s)) {
                return s;
            }
        }
        return 0;
    }
    [[nodiscard]] constexpr int bottom() const {
        for (int s = kLarge; s >= kSmall; --s) {
            if (has(s)) {
                return s;
            }
        }
        return 0;
    }
    [[nodiscard]] constexpr bool accepts(int size) const { return empty() || size < top(); }
    [[nodiscard]] constexpr bool isPyramid() const { return mask_ == 7u; }
    constexpr void push(int size) { mask_ = static_cast<std::uint8_t>(mask_ | (1u << (size - 1))); }
    constexpr int pop() {
        const int s = top();
        if (s > 0) {
            mask_ = static_cast<std::uint8_t>(mask_ & ~(1u << (s - 1)));
        }
        return s;
    }
    friend constexpr bool operator==(const Stack&, const Stack&) = default;

private:
    std::uint8_t mask_{};
};

}
