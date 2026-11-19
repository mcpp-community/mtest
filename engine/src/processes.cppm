// The processes of a program's unit as the system shows them (0.10.0): what a timeout records
// before it asks the program to end, and again after the program's grace (docs/commands.md
// "Running").
export module mcpplibs.mtest.engine.processes;

import std;
export import mcpplibs.mtest.engine.model;

export namespace mcpplibs::mtest::engine {

struct processes_seen {
    std::string seen;  // "tree" or "program", as timeout_info's
    std::vector<process_info> processes;
};

// The program the system identifies by `program`, every process it started, and every other process
// of its unit, the program's subtree first: on Linux from /proc, each with its state, what it waits
// on, what the request to end does to it, and its threads. Where the system lists no other process
// to the engine (macOS, Windows, a /proc it cannot read), the program alone, as mcpplibs.os
// describes it; nothing once the program has ended.
processes_seen processes_of(std::uint64_t program);

}  // namespace mcpplibs::mtest::engine
