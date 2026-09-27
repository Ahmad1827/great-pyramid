#pragma once

#include "Core.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace pm {

enum class Terrain : std::uint8_t { Ground, Hedge, Palm, Rock, Urn, Cactus, Water, Gate, Altar };

[[nodiscard]] constexpr bool blocksPlayer(Terrain t) {
    return t == Terrain::Hedge || t == Terrain::Palm || t == Terrain::Rock || t == Terrain::Urn ||
           t == Terrain::Cactus || t == Terrain::Water;
}

struct RoomInfo {
    int id{};
    std::string name;
    Vec2i min{};
    Vec2i max{};
    int blocks{};
    int altars{};
};

struct WorldDef {
    int width{};
    int height{};
    std::vector<Terrain> terrain;
    std::vector<std::uint8_t> lock;
    std::vector<int> room;
    std::vector<RoomInfo> rooms;

    [[nodiscard]] bool inBounds(Vec2i p) const { return p.x >= 0 && p.y >= 0 && p.x < width && p.y < height; }
    [[nodiscard]] std::size_t index(Vec2i p) const { return static_cast<std::size_t>(p.y * width + p.x); }
    [[nodiscard]] Terrain at(Vec2i p) const { return inBounds(p) ? terrain[index(p)] : Terrain::Hedge; }
    [[nodiscard]] int roomAt(Vec2i p) const { return inBounds(p) ? room[index(p)] : 0; }
    [[nodiscard]] int lockAt(Vec2i p) const { return inBounds(p) ? lock[index(p)] : 0; }
    [[nodiscard]] int levelCount() const { return static_cast<int>(rooms.size()) - 1; }
};

struct WorldState {
    std::vector<std::uint8_t> cells;
    std::vector<std::uint8_t> solved;
    Vec2i player{};
    Vec2i entry{};
    Dir facing{Dir::Down};
    int moves{};

    static constexpr std::uint8_t kSandBit = 8u;

    [[nodiscard]] bool sand(std::size_t i) const { return (cells[i] & kSandBit) != 0; }
    [[nodiscard]] Stack stack(std::size_t i) const { return Stack{static_cast<std::uint8_t>(cells[i] & 7u)}; }
    void setSand(std::size_t i, bool v) {
        cells[i] = static_cast<std::uint8_t>(v ? (cells[i] | kSandBit) : (cells[i] & ~kSandBit));
    }
    void setStack(std::size_t i, Stack s) { cells[i] = static_cast<std::uint8_t>((cells[i] & kSandBit) | s.mask()); }
    [[nodiscard]] int solvedCount() const {
        int n = 0;
        for (std::size_t i = 1; i < solved.size(); ++i) {
            n += solved[i] != 0;
        }
        return n;
    }
    friend bool operator==(const WorldState&, const WorldState&) = default;
};

struct MoveEvent {
    bool moved{};
    Vec2i playerFrom{};
    Vec2i playerTo{};
    bool pushed{};
    Vec2i blockFrom{};
    Vec2i blockTo{};
    int sizeBefore{};
    int sizeAfter{};
    std::uint8_t sourceMask{};
    std::uint8_t destMask{};
    bool clearedSand{};
    bool stacked{};
    bool enteredRoom{};
    int solvedRoom{-1};
};

struct World {
    WorldDef def;
    WorldState initial;
};

[[nodiscard]] World parseWorld(const std::vector<std::string>& rows, const std::vector<std::string>& names);
[[nodiscard]] bool roomComplete(const WorldDef& def, const WorldState& state, int room);
[[nodiscard]] std::optional<WorldState> step(const WorldDef& def, const WorldState& state, Dir dir, MoveEvent* event);
[[nodiscard]] const std::vector<std::string>& worldRows();
[[nodiscard]] const std::vector<std::string>& gardenNames();

}
