#pragma once

#include "World.hpp"

#include <cstddef>
#include <cstdint>
#include <algorithm>
#include <cstring>
#include <optional>
#include <queue>
#include <utility>
#include <string>
#include <vector>

namespace pm::tools {

struct SolveResult {
    bool solved{};
    bool exhausted{};
    std::size_t explored{};
    int pushes{};
    std::string path;
};

inline std::optional<Vec2i> findGate(const WorldDef& def, int room) {
    for (int y = 0; y < def.height; ++y) {
        for (int x = 0; x < def.width; ++x) {
            const Vec2i p{x, y};
            if (def.at(p) != Terrain::Gate) {
                continue;
            }
            for (Dir d : kDirs) {
                if (def.roomAt(p + offset(d)) == room) {
                    return p;
                }
            }
        }
    }
    return std::nullopt;
}

class RoomSolver {
public:
    RoomSolver(const WorldDef& def, const WorldState& start, int room) : def_(def), base_(start), room_(room) {
        for (std::size_t i = 0; i < def.room.size(); ++i) {
            if (def.room[i] == room) {
                cells_.push_back(i);
            }
        }
        gate_ = findGate(def, room);
        stride_ = (cells_.size() + 1) / 2 + 2;
    }

    SolveResult solve(std::size_t limit, bool greedy = false) {
        SolveResult result;
        if (!gate_) {
            return result;
        }
        WorldState s = base_;
        s.player = *gate_;
        canonical(s);
        insert(s, UINT32_MAX, 0, 0);
        depth_.push_back(0);
        using Entry = std::pair<int, std::uint32_t>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<>> open;
        open.push({0, 0});
        std::size_t fifo = 0;
        while (greedy ? !open.empty() : fifo < count_) {
            std::size_t head = fifo++;
            if (greedy) {
                head = open.top().second;
                open.pop();
            }
            if (count_ > limit) {
                result.explored = count_;
                return result;
            }
            WorldState cur = load(head);
            const auto reach = reachable(cur);
            for (std::size_t ri = 0; ri < reach.size(); ++ri) {
                if (!reach[ri]) {
                    continue;
                }
                const Vec2i p{static_cast<int>(ri % static_cast<std::size_t>(def_.width)),
                              static_cast<int>(ri / static_cast<std::size_t>(def_.width))};
                for (int d = 0; d < 4; ++d) {
                    const Dir dir = kDirs[static_cast<std::size_t>(d)];
                    const Vec2i t = p + offset(dir);
                    if (!def_.inBounds(t) || cur.stack(def_.index(t)).empty()) {
                        continue;
                    }
                    WorldState from = cur;
                    from.player = p;
                    auto next = step(def_, from, dir, nullptr);
                    if (!next) {
                        continue;
                    }
                    const bool done = next->solved[static_cast<std::size_t>(room_)] != 0;
                    canonical(*next);
                    if (!insert(*next, static_cast<std::uint32_t>(head), static_cast<std::uint32_t>(ri), static_cast<std::uint8_t>(d))) {
                        continue;
                    }
                    depth_.push_back(depth_[head] + 1);
                    if (greedy) {
                        open.push({depth_.back() + 4 * heuristic(*next), static_cast<std::uint32_t>(count_ - 1)});
                    }
                    if (done) {
                        result.solved = true;
                        result.explored = count_;
                        buildPath(count_ - 1, result);
                        return result;
                    }
                }
            }
        }
        result.exhausted = true;
        result.explored = count_;
        return result;
    }

private:
    const WorldDef& def_;
    WorldState base_;
    int room_;
    std::vector<std::size_t> cells_;
    std::optional<Vec2i> gate_;
    std::size_t stride_{};
    std::vector<std::uint8_t> arena_;
    std::vector<std::uint32_t> parent_;
    std::vector<std::uint32_t> pushFrom_;
    std::vector<std::uint8_t> pushDir_;
    std::vector<std::uint32_t> table_;
    std::size_t count_{};
    std::vector<int> depth_;

    int heuristic(const WorldState& s) const {
        int stacks = 0;
        int sizes = 0;
        int blocks = 0;
        for (std::size_t i : cells_) {
            const Stack st = s.stack(i);
            if (st.empty()) {
                continue;
            }
            ++stacks;
            blocks += st.count();
            sizes += (st.has(1) ? 1 : 0) + (st.has(2) ? 2 : 0) + (st.has(3) ? 3 : 0);
        }
        const int pyramids = blocks / 3;
        return (stacks - pyramids) + std::max(0, pyramids * 6 - sizes);
    }

    bool walkable(const WorldState& s, Vec2i p) const {
        if (!def_.inBounds(p)) {
            return false;
        }
        if (def_.roomAt(p) != room_ && p != *gate_) {
            return false;
        }
        const std::size_t i = def_.index(p);
        return !blocksPlayer(def_.terrain[i]) && s.stack(i).empty();
    }

    std::vector<char> reachable(const WorldState& s) const {
        std::vector<char> seen(def_.terrain.size(), 0);
        std::vector<Vec2i> q{s.player};
        seen[def_.index(s.player)] = 1;
        for (std::size_t h = 0; h < q.size(); ++h) {
            for (Dir d : kDirs) {
                const Vec2i n = q[h] + offset(d);
                if (walkable(s, n) && !seen[def_.index(n)]) {
                    seen[def_.index(n)] = 1;
                    q.push_back(n);
                }
            }
        }
        return seen;
    }

    void canonical(WorldState& s) const {
        const auto r = reachable(s);
        for (std::size_t i = 0; i < r.size(); ++i) {
            if (r[i]) {
                s.player = {static_cast<int>(i % static_cast<std::size_t>(def_.width)),
                            static_cast<int>(i / static_cast<std::size_t>(def_.width))};
                return;
            }
        }
    }

    void encode(const WorldState& s, std::uint8_t* out) const {
        std::memset(out, 0, stride_);
        for (std::size_t k = 0; k < cells_.size(); ++k) {
            const std::uint8_t v = s.cells[cells_[k]] & 15u;
            out[k / 2] = static_cast<std::uint8_t>(out[k / 2] | (k % 2 ? v << 4 : v));
        }
        const auto pi = static_cast<std::uint16_t>(def_.index(s.player));
        out[stride_ - 2] = static_cast<std::uint8_t>(pi & 0xFFu);
        out[stride_ - 1] = static_cast<std::uint8_t>(pi >> 8);
    }

    WorldState load(std::size_t node) const {
        WorldState s = base_;
        const std::uint8_t* in = arena_.data() + node * stride_;
        for (std::size_t k = 0; k < cells_.size(); ++k) {
            const std::uint8_t v = k % 2 ? static_cast<std::uint8_t>(in[k / 2] >> 4) : static_cast<std::uint8_t>(in[k / 2] & 15u);
            s.cells[cells_[k]] = v;
        }
        const std::size_t pi = static_cast<std::size_t>(in[stride_ - 2]) | (static_cast<std::size_t>(in[stride_ - 1]) << 8);
        s.player = {static_cast<int>(pi % static_cast<std::size_t>(def_.width)),
                    static_cast<int>(pi / static_cast<std::size_t>(def_.width))};
        return s;
    }

    static std::uint64_t hashBytes(const std::uint8_t* p, std::size_t n) {
        std::uint64_t h = 1469598103934665603ull;
        for (std::size_t i = 0; i < n; ++i) {
            h = (h ^ p[i]) * 1099511628211ull;
        }
        return h;
    }

    void grow() {
        std::vector<std::uint32_t> old = std::move(table_);
        table_.assign(old.empty() ? (1u << 16) : old.size() * 2, UINT32_MAX);
        const std::size_t mask = table_.size() - 1;
        for (std::uint32_t idx : old) {
            if (idx == UINT32_MAX) {
                continue;
            }
            std::size_t slot = hashBytes(arena_.data() + static_cast<std::size_t>(idx) * stride_, stride_) & mask;
            while (table_[slot] != UINT32_MAX) {
                slot = (slot + 1) & mask;
            }
            table_[slot] = idx;
        }
    }

    bool insert(const WorldState& s, std::uint32_t parent, std::uint32_t from, std::uint8_t dir) {
        if ((count_ + 1) * 2 > table_.size()) {
            grow();
        }
        const std::size_t offsetBytes = count_ * stride_;
        arena_.resize(offsetBytes + stride_);
        encode(s, arena_.data() + offsetBytes);
        const std::size_t mask = table_.size() - 1;
        std::size_t slot = hashBytes(arena_.data() + offsetBytes, stride_) & mask;
        while (table_[slot] != UINT32_MAX) {
            if (std::memcmp(arena_.data() + static_cast<std::size_t>(table_[slot]) * stride_, arena_.data() + offsetBytes, stride_) == 0) {
                arena_.resize(offsetBytes);
                return false;
            }
            slot = (slot + 1) & mask;
        }
        table_[slot] = static_cast<std::uint32_t>(count_);
        parent_.push_back(parent);
        pushFrom_.push_back(from);
        pushDir_.push_back(dir);
        ++count_;
        return true;
    }

    std::string walk(const WorldState& s, Vec2i from, Vec2i to) const {
        std::vector<int> prev(def_.terrain.size(), -1);
        std::vector<char> how(def_.terrain.size(), 0);
        std::vector<Vec2i> q{from};
        prev[def_.index(from)] = static_cast<int>(def_.index(from));
        constexpr char names[4] = {'U', 'D', 'L', 'R'};
        for (std::size_t h = 0; h < q.size() && prev[def_.index(to)] < 0; ++h) {
            for (int d = 0; d < 4; ++d) {
                const Vec2i n = q[h] + offset(kDirs[static_cast<std::size_t>(d)]);
                if (walkable(s, n) && prev[def_.index(n)] < 0) {
                    prev[def_.index(n)] = static_cast<int>(def_.index(q[h]));
                    how[def_.index(n)] = names[d];
                    q.push_back(n);
                }
            }
        }
        std::string out;
        for (std::size_t c = def_.index(to); c != def_.index(from); c = static_cast<std::size_t>(prev[c])) {
            out.push_back(how[c]);
        }
        return {out.rbegin(), out.rend()};
    }

    void buildPath(std::size_t node, SolveResult& result) const {
        std::vector<std::size_t> chain;
        for (std::size_t c = node; c != 0; c = parent_[c]) {
            chain.push_back(c);
        }
        constexpr char names[4] = {'U', 'D', 'L', 'R'};
        WorldState s = base_;
        s.player = *gate_;
        std::string path;
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            const std::size_t pf = pushFrom_[*it];
            const Vec2i p{static_cast<int>(pf % static_cast<std::size_t>(def_.width)),
                          static_cast<int>(pf / static_cast<std::size_t>(def_.width))};
            path += walk(s, s.player, p);
            s.player = p;
            const Dir dir = kDirs[pushDir_[*it]];
            path.push_back(names[pushDir_[*it]]);
            s = *step(def_, s, dir, nullptr);
        }
        result.pushes = static_cast<int>(chain.size());
        result.path = std::move(path);
    }
};

inline SolveResult solveRoom(const WorldDef& def, const WorldState& start, int room, std::size_t limit, bool greedy = false) {
    RoomSolver solver(def, start, room);
    return solver.solve(limit, greedy);
}

}
