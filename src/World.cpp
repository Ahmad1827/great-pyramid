#include "World.hpp"

#include <algorithm>
#include <stdexcept>

namespace pm {

namespace {

struct Glyph {
    Terrain terrain{Terrain::Hedge};
    bool sand{};
    int block{};
    int lock{};
    bool player{};
};

Glyph decode(char c) {
    switch (c) {
        case '.': return {Terrain::Ground, true};
        case ',': return {Terrain::Ground, false};
        case 'A': return {Terrain::Altar, false};
        case '=': return {Terrain::Gate, false};
        case 'T': return {Terrain::Palm};
        case 'R': return {Terrain::Rock};
        case 'U': return {Terrain::Urn};
        case 'C': return {Terrain::Cactus};
        case '~': return {Terrain::Water};
        case 'P': return {Terrain::Ground, true, 0, 0, true};
        case 's': return {Terrain::Ground, false, kSmall};
        case 'm': return {Terrain::Ground, false, kMedium};
        case 'l': return {Terrain::Ground, false, kLarge};
        default: break;
    }
    if (c >= '1' && c <= '9') {
        return {Terrain::Gate, false, 0, c - '0'};
    }
    return {Terrain::Hedge};
}

bool floodable(Terrain t) { return !blocksPlayer(t) && t != Terrain::Gate; }

}

World parseWorld(const std::vector<std::string>& rows, const std::vector<std::string>& names) {
    World w;
    WorldDef& d = w.def;
    WorldState& s = w.initial;
    d.height = static_cast<int>(rows.size());
    for (const auto& r : rows) {
        d.width = std::max(d.width, static_cast<int>(r.size()));
    }
    if (d.width == 0 || d.height == 0) {
        throw std::invalid_argument("world map is empty");
    }
    const auto n = static_cast<std::size_t>(d.width * d.height);
    d.terrain.assign(n, Terrain::Hedge);
    d.lock.assign(n, 0);
    d.room.assign(n, 0);
    s.cells.assign(n, 0);
    bool hasPlayer = false;
    for (int y = 0; y < d.height; ++y) {
        const auto& row = rows[static_cast<std::size_t>(y)];
        for (int x = 0; x < static_cast<int>(row.size()); ++x) {
            const Vec2i p{x, y};
            const std::size_t i = d.index(p);
            const Glyph g = decode(row[static_cast<std::size_t>(x)]);
            d.terrain[i] = g.terrain;
            d.lock[i] = static_cast<std::uint8_t>(g.lock);
            s.setSand(i, g.sand);
            if (g.block > 0) {
                Stack st;
                st.push(g.block);
                s.setStack(i, st);
            }
            if (g.player) {
                s.player = p;
                hasPlayer = true;
            }
        }
    }
    if (!hasPlayer) {
        throw std::invalid_argument("world map has no player start");
    }
    s.entry = s.player;
    d.rooms.push_back(RoomInfo{0, "The Sands", {0, 0}, {d.width - 1, d.height - 1}, 0, 0});
    std::vector<int> comp(n, -1);
    std::vector<Vec2i> queue;
    for (int y = 0; y < d.height; ++y) {
        for (int x = 0; x < d.width; ++x) {
            const Vec2i start{x, y};
            const std::size_t si = d.index(start);
            if (comp[si] >= 0 || !floodable(d.terrain[si])) {
                continue;
            }
            queue.clear();
            queue.push_back(start);
            comp[si] = 1;
            bool hasBlock = false;
            for (std::size_t q = 0; q < queue.size(); ++q) {
                const Vec2i p = queue[q];
                hasBlock = hasBlock || !s.stack(d.index(p)).empty();
                for (Dir dir : kDirs) {
                    const Vec2i nb = p + offset(dir);
                    if (!d.inBounds(nb)) {
                        continue;
                    }
                    const std::size_t ni = d.index(nb);
                    if (comp[ni] < 0 && floodable(d.terrain[ni])) {
                        comp[ni] = 1;
                        queue.push_back(nb);
                    }
                }
            }
            if (!hasBlock) {
                continue;
            }
            RoomInfo info;
            info.id = static_cast<int>(d.rooms.size());
            const auto nameIndex = static_cast<std::size_t>(info.id - 1);
            info.name = nameIndex < names.size() ? names[nameIndex] : "Garden " + std::to_string(info.id);
            info.min = queue.front();
            info.max = queue.front();
            for (const Vec2i p : queue) {
                const std::size_t i = d.index(p);
                d.room[i] = info.id;
                info.min = {std::min(info.min.x, p.x), std::min(info.min.y, p.y)};
                info.max = {std::max(info.max.x, p.x), std::max(info.max.y, p.y)};
                info.blocks += s.stack(i).count();
                info.altars += d.terrain[i] == Terrain::Altar;
            }
            d.rooms.push_back(info);
        }
    }
    s.solved.assign(d.rooms.size(), 0);
    return w;
}

bool roomComplete(const WorldDef& def, const WorldState& state, int room) {
    if (room <= 0 || room >= static_cast<int>(def.rooms.size())) {
        return false;
    }
    const RoomInfo& info = def.rooms[static_cast<std::size_t>(room)];
    int pyramids = 0;
    for (int y = info.min.y; y <= info.max.y; ++y) {
        for (int x = info.min.x; x <= info.max.x; ++x) {
            const std::size_t i = def.index({x, y});
            if (def.room[i] != room) {
                continue;
            }
            const Stack st = state.stack(i);
            if (def.terrain[i] == Terrain::Altar && !st.isPyramid()) {
                return false;
            }
            if (st.empty()) {
                continue;
            }
            if (!st.isPyramid()) {
                return false;
            }
            ++pyramids;
        }
    }
    return pyramids > 0;
}

std::optional<WorldState> step(const WorldDef& def, const WorldState& state, Dir dir, MoveEvent* event) {
    const Vec2i o = offset(dir);
    const Vec2i from = state.player;
    const Vec2i target = from + o;
    if (!def.inBounds(target)) {
        return std::nullopt;
    }
    const Terrain tt = def.at(target);
    if (blocksPlayer(tt) || (tt == Terrain::Gate && def.lockAt(target) > state.solvedCount())) {
        return std::nullopt;
    }
    WorldState next = state;
    next.facing = dir;
    MoveEvent ev;
    ev.playerFrom = from;
    const std::size_t ti = def.index(target);
    Stack source = state.stack(ti);
    const int room = def.roomAt(target);
    if (!source.empty()) {
        if (room > 0 && state.solved[static_cast<std::size_t>(room)] != 0) {
            return std::nullopt;
        }
        const Vec2i dest = target + o;
        if (!def.inBounds(dest)) {
            return std::nullopt;
        }
        const Terrain dt = def.at(dest);
        if (blocksPlayer(dt) || dt == Terrain::Gate || def.roomAt(dest) != room) {
            return std::nullopt;
        }
        const std::size_t di = def.index(dest);
        Stack destination = state.stack(di);
        const int size = source.top();
        ev.sourceMask = source.mask();
        ev.destMask = destination.mask();
        int finalSize = size;
        if (destination.empty()) {
            if (state.sand(di)) {
                finalSize = std::min(kLarge, size + 1);
                next.setSand(di, false);
                ev.clearedSand = true;
            }
        } else {
            if (!destination.accepts(size)) {
                return std::nullopt;
            }
            ev.stacked = true;
        }
        source.pop();
        destination.push(finalSize);
        next.setStack(ti, source);
        next.setStack(di, destination);
        ev.pushed = true;
        ev.blockFrom = target;
        ev.blockTo = dest;
        ev.sizeBefore = size;
        ev.sizeAfter = finalSize;
        if (source.empty()) {
            next.player = target;
        }
    } else {
        next.player = target;
    }
    if (next.player != from && def.roomAt(next.player) > 0 && def.roomAt(next.player) != def.roomAt(from)) {
        next.entry = from;
        ev.enteredRoom = true;
    }
    ++next.moves;
    if (room > 0 && next.solved[static_cast<std::size_t>(room)] == 0 && roomComplete(def, next, room)) {
        next.solved[static_cast<std::size_t>(room)] = 1;
        ev.solvedRoom = room;
    }
    ev.moved = true;
    ev.playerTo = next.player;
    if (event != nullptr) {
        *event = ev;
    }
    return next;
}

}
