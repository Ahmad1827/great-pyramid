#include "RoomSolver.hpp"
#include "World.hpp"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
    const std::size_t limit = argc > 1 ? std::stoul(argv[1]) : 6000000;
    const bool greedy = argc > 2 && std::string(argv[2]) == "greedy";
    const pm::World world = pm::parseWorld(pm::worldRows(), pm::gardenNames());
    int failures = 0;
    for (int r = 1; r <= world.def.levelCount(); ++r) {
        const auto& info = world.def.rooms[static_cast<std::size_t>(r)];
        const auto result = pm::tools::solveRoom(world.def, world.initial, r, limit, greedy);
        std::cout << r << ". " << info.name << " (" << info.blocks << " blocks): ";
        if (result.solved) {
            std::cout << "solved in " << result.pushes << " pushes, " << result.path.size() << " moves\n  " << result.path << "\n";
        } else if (result.exhausted) {
            std::cout << "UNSOLVABLE\n";
            ++failures;
        } else {
            std::cout << "search limit reached after " << result.explored << " states\n";
            ++failures;
        }
        std::cout.flush();
    }
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
