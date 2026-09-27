#include "Game.hpp"

#include <array>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <utility>

namespace pm {

namespace {

constexpr std::size_t kHistoryLimit = 20000;
constexpr std::size_t kHistoryTrim = 2000;
constexpr const char* kSaveMagic = "PYRAMIDMAKER";
constexpr int kSaveVersion = 1;

std::string makePyramidName(int room) {
    static constexpr std::array<const char*, 12> rulers{
        "Khufu", "Hatshepsut", "Djoser", "Sneferu", "Nefertari", "Menkaure",
        "Ahmose", "Tiye", "Khafre", "Merneith", "Userkaf", "Sobekneferu"};
    static constexpr std::array<const char*, 10> epithets{
        "the Patient", "the Careful", "of Many Steps", "the Unhurried", "the Sandy",
        "the Stubborn", "the Well-Rolled", "of the Quiet Dunes", "the Tall", "the Golden"};
    const auto r = static_cast<std::size_t>(room);
    return std::string("The Pyramid of ") + rulers[(r * 7u + 3u) % rulers.size()] + " " +
           epithets[(r * 5u + 1u) % epithets.size()];
}

}

Game::Game(World world) : world_(std::move(world)), current_(world_.initial) {
    pyramidNames_.reserve(world_.def.rooms.size());
    for (std::size_t r = 0; r < world_.def.rooms.size(); ++r) {
        pyramidNames_.push_back(makePyramidName(static_cast<int>(r)));
    }
}

const std::string& Game::roomName(int room) const {
    return world_.def.rooms[static_cast<std::size_t>(room)].name;
}

const std::string& Game::pyramidName(int room) const { return pyramidNames_[static_cast<std::size_t>(room)]; }

void Game::remember() {
    if (history_.size() >= kHistoryLimit) {
        history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(kHistoryTrim));
    }
    history_.push_back(current_);
}

MoveEvent Game::move(Dir dir) {
    MoveEvent ev;
    auto next = step(world_.def, current_, dir, &ev);
    if (!next) {
        current_.facing = dir;
        return ev;
    }
    remember();
    current_ = std::move(*next);
    if (ev.solvedRoom > 0) {
        history_.clear();
    }
    ++revision_;
    return ev;
}

bool Game::undo() {
    if (history_.empty()) {
        return false;
    }
    current_ = std::move(history_.back());
    history_.pop_back();
    ++revision_;
    return true;
}

bool Game::resetRoom() {
    const int room = currentRoom();
    if (room <= 0 || current_.solved[static_cast<std::size_t>(room)] != 0) {
        return false;
    }
    WorldState next = current_;
    for (std::size_t i = 0; i < world_.def.room.size(); ++i) {
        if (world_.def.room[i] == room) {
            next.cells[i] = world_.initial.cells[i];
        }
    }
    next.player = current_.entry;
    next.facing = Dir::Up;
    if (next == current_) {
        return false;
    }
    remember();
    current_ = std::move(next);
    ++revision_;
    return true;
}

void Game::newGame() {
    current_ = world_.initial;
    history_.clear();
    ++revision_;
}

bool Game::save(const std::string& path) const {
    std::error_code ec;
    const auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
    }
    std::ofstream out(path, std::ios::trunc);
    if (!out) {
        return false;
    }
    const auto& d = world_.def;
    out << kSaveMagic << ' ' << kSaveVersion << '\n';
    out << d.width << ' ' << d.height << '\n';
    out << current_.player.x << ' ' << current_.player.y << ' ' << current_.entry.x << ' ' << current_.entry.y << ' '
        << static_cast<int>(current_.facing) << ' ' << current_.moves << '\n';
    for (auto s : current_.solved) {
        out << (s != 0 ? '1' : '0');
    }
    out << '\n';
    constexpr const char* hex = "0123456789abcdef";
    for (int y = 0; y < d.height; ++y) {
        for (int x = 0; x < d.width; ++x) {
            out << hex[current_.cells[d.index({x, y})] & 15u];
        }
        out << '\n';
    }
    return static_cast<bool>(out);
}

bool Game::load(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::string magic;
    int version = 0;
    int w = 0;
    int h = 0;
    in >> magic >> version >> w >> h;
    const auto& d = world_.def;
    if (magic != kSaveMagic || version != kSaveVersion || w != d.width || h != d.height) {
        return false;
    }
    WorldState s = world_.initial;
    int facing = 0;
    in >> s.player.x >> s.player.y >> s.entry.x >> s.entry.y >> facing >> s.moves;
    std::string solved;
    in >> solved;
    if (!in || solved.size() != s.solved.size() || facing < 0 || facing > 3 || !d.inBounds(s.player)) {
        return false;
    }
    s.facing = static_cast<Dir>(facing);
    for (std::size_t i = 0; i < solved.size(); ++i) {
        s.solved[i] = solved[i] == '1' ? 1 : 0;
    }
    for (int y = 0; y < h; ++y) {
        std::string row;
        in >> row;
        if (!in || static_cast<int>(row.size()) != w) {
            return false;
        }
        for (int x = 0; x < w; ++x) {
            const char c = row[static_cast<std::size_t>(x)];
            const int v = c >= 'a' ? c - 'a' + 10 : c - '0';
            if (v < 0 || v > 15) {
                return false;
            }
            s.cells[d.index({x, y})] = static_cast<std::uint8_t>(v);
        }
    }
    current_ = std::move(s);
    history_.clear();
    ++revision_;
    return true;
}

}
