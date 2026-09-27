#pragma once

#include "World.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace pm {

class Game {
public:
    explicit Game(World world);

    MoveEvent move(Dir dir);
    bool undo();
    bool resetRoom();
    void newGame();
    bool save(const std::string& path) const;
    bool load(const std::string& path);

    [[nodiscard]] const WorldDef& def() const { return world_.def; }
    [[nodiscard]] const WorldState& state() const { return current_; }
    [[nodiscard]] int currentRoom() const { return world_.def.roomAt(current_.player); }
    [[nodiscard]] int solvedCount() const { return current_.solvedCount(); }
    [[nodiscard]] int levelCount() const { return world_.def.levelCount(); }
    [[nodiscard]] const std::string& roomName(int room) const;
    [[nodiscard]] const std::string& pyramidName(int room) const;
    [[nodiscard]] bool canUndo() const { return !history_.empty(); }
    [[nodiscard]] std::uint64_t revision() const { return revision_; }

private:
    World world_;
    WorldState current_;
    std::vector<WorldState> history_;
    std::vector<std::string> pyramidNames_;
    std::uint64_t revision_{};

    void remember();
};

}
