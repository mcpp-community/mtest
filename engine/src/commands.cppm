// Every command of the engine, once: the CLI (main.cpp), the JSON-RPC service and the MCP tools
// (service.cppm) call the same function with the same parameters and print or send the same data
// (mtest design §6.1: one capability, three entries). docs/commands.md describes each; `catalog()`
// describes them to a machine.
export module mcpplibs.mtest.engine.commands;

import std;
export import mcpplibs.mtest.engine.model;

export namespace mcpplibs::mtest::engine {

struct answer {
    int status = 0;    // the process's exit status: 0 done, 1 a test or a gate failed, 2 a usage
                       // error, 3 the machine's
    json data;         // the result, with its `conclusion`
    std::string text;  // for a person
};

// Runs `command` with `params` in the project of `cwd`. `events` receives a run's events as they
// happen.
answer dispatch(const std::filesystem::path& cwd, std::string_view command, const json& params,
                const std::function<void(const json&)>& events = {});

// The commands, each with a description and the JSON Schema of its parameters (MCP's `tools/list`).
json catalog();

}  // namespace mcpplibs::mtest::engine
