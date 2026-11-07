// `mtest serve`: the engine as a service on its standard streams, one JSON message per line
// (docs/service.md). It speaks JSON-RPC 2.0, whose methods are the commands (commands.cppm) and
// whose `mtest/event` notifications are a run's events as they happen, and the Model Context
// Protocol's tools (`initialize`, `tools/list`, `tools/call`), whose catalog is the same commands
// with their JSON Schemas. Each request runs on a thread of its own, so `status` and `cancel` are
// answered while a `run` runs.
export module mcpplibs.mtest.engine.service;

import std;

export namespace mcpplibs::mtest::engine {

int serve(const std::filesystem::path& cwd);

}  // namespace mcpplibs::mtest::engine
