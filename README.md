# mtest

A test system for the agent era: one protocol between test programs and the tools that run them, a
C++ test library with no macro and no header, and an engine that runs, stores and explains tests of any
language — for people, for CI and for agents alike.

English | [简体中文](README.zh-CN.md)

## Parts

Each part is used alone; together they share one data model.

| Part | What it gives | Where |
|---|---|---|
| Protocol | how a test program lists, selects and reports its cases | [spec/protocol.md](spec/protocol.md) |
| C++ library | cases, expectations, fixtures; `bench`, `cts`, `e2e` modules | [lib/](lib/) — `mcpplibs.mtest` |
| Engine | run, plan, explain, history, analyze, graph, gate, trace, bisect, ci | [engine/](engine/) — the `mtest` program |
| Service | the engine's commands as JSON-RPC and MCP tools | `mtest serve` — [docs/service.md](docs/service.md) |
| Adapters | GoogleTest, Bash, Python | [adapters/](adapters/) — [docs/adapters.md](docs/adapters.md) |
| C ABI | the protocol for any language with a C FFI | [capi/](capi/) — `include/mtest.h` |
| Conformance | one suite that checks every implementation of the protocol | [conformance/](conformance/) |

## A test, with the library

```cpp
import mcpplibs.mtest;
using namespace mcpplibs::mtest;

int main() {
    "adds"_test = [] { expect(eq(1 + 1, 2)); };
    "reads a file"_test[tag{"fs"}] = [] {
        auto dir = temp_dir();
        expect(std::filesystem::is_directory(dir));
    };
    return report();
}
```

```toml
[dev-dependencies]
mcpplibs.mtest = "0.1.0"
```

A failure names its place and both sides (`expected 2, actual 3`). Under a runner the same program
speaks the protocol; alone it prints one line per case.

## Tests, with the engine

```bash
mtest run ./build/tests/*          # any program: by the protocol, else by its exit status
mtest run --set smoke              # the sets of mtest.toml: smoke, regression, pr, full, release
mtest explain                      # where the last failure was, why, and the command that repeats it
mtest plan --since origin/main     # what a change affects, and why each case is in
mtest analyze hot                  # the slowest cases; also durations, path, failures, flaky, bench
mtest serve                        # the same commands for an agent (MCP) or an IDE (JSON-RPC)
```

Every command takes `--json`, and every answer has a `conclusion`. See
[docs/commands.md](docs/commands.md) and [docs/config.md](docs/config.md).

## Levels

| Level | A program does | A runner obtains |
|---|---|---|
| L0 | nothing | one result per program, by its exit status |
| L1 | list, select, report | the case tree, single cases, failure places |
| L2 | metrics | bench verdicts, conformance clauses |
| L3 | resources, artifacts | scheduling, affected selection |

The library, the C ABI and the script adapters are L3; GoogleTest is L1.

## Build

mtest is built with [mcpp](https://github.com/mcpp-community/mcpp):

```bash
mcpp build                      # every member
mcpp test -p mtest -p engine    # the library's and the engine's tests
mcpp test -p conformance        # the protocol, against the library (MTEST_CTS_PROGRAM: another one)
```

## License

[Apache-2.0](LICENSE)
