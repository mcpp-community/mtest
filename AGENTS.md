# AGENTS.md

mtest is a test system for the agent era: a protocol any test program can speak, an engine that
runs, stores and explains the results, and a C++ library, adapters and a C ABI that implement the
protocol. This file states the core ideas and rules; the design and its record are in
[.agents/docs/](.agents/docs/README.md).

## Ideas

1. **The protocol is the contract.** The engine reads only what [spec/protocol.md](spec/protocol.md)
   defines. The C++ library has no privilege over the adapters; every implementation passes the
   conformance suite ([spec/conformance.md](spec/conformance.md)). A program that speaks no protocol
   is still a test, judged by its exit status (L0).
2. **Each part is usable alone.** The parts share the data model and nothing else; a command reads
   only the data it needs, and missing data gives a shorter answer, not an error.
3. **People and agents are both first users.** Every answer is data with a conclusion (`--json`, the
   MCP tools); every failure states where, why and the command that repeats it.
4. **Policy is enforced by the tool.** Whether a measurement is valid, may be repeated or retried is
   decided by the engine from a declared policy, not remembered by a person.
5. **mtest is its own user.** Each capability is first used by mtest's own tests and CI.
6. **openkal native.** The engine and the library reach the system only through `mcpplibs.os`: no
   system header, no preprocessor platform branch; what varies by system is decided at run time
   (`os::host()`), so one ok image serves every system.

## Layout

| Member | Role | Depends on |
|---|---|---|
| `lib` | the C++ library: cases, expectations, `bench`, `cts`, `e2e` | `mcpplibs.os`, JSON |
| `engine` | the `mtest` command and its service | `mcpplibs.os`, `mcpplibs.cmdline`, JSON, TOML |
| `conformance` | the protocol's conformance suite | `lib` |
| `adapters/*` | GoogleTest, Bash, Python | the framework adapted, JSON |
| `capi` | `include/mtest.h` for any language with a C FFI | JSON |

## Code

- What a library already does is depended on, not written again: first a released `mcpplibs.*`
  package that others use, then a well-known library of mcpp-index (nlohmann/json, toml++,
  GoogleTest), and only then an implementation of one's own, of what neither provides.
- C++23 modules; an interface unit (`.cppm`) and its implementation (`.cpp`) apart.
- Format by [.clang-format](.clang-format) (the same as mcpp's); CI checks it with the pinned
  clang-format.
- No macros: modules, `inline constexpr`, `if constexpr`, `std::source_location`. Headers appear
  where a library is only a header: GoogleTest's in its adapter (with nlohmann/json's beside it), and
  the C ABI's `include/mtest.h`.
- Names are readable words; loops nest at most two deep (a helper otherwise).
- Comments and documents are declarative and concise; they carry no local path, user name or
  conversation.

## Work

- `mcpp.toml` is the only project file; build and test with mcpp (`mcpp test -p <member>`), and as
  images with `--target <arch>-openkal`. Tests leave the working tree clean (CI checks it).
- A change to the protocol changes the specification, the conformance suite and every
  implementation together.
- A release is one pull request; each commit is a checkpoint; it is squash-merged when CI passes.
- READMEs are concise, English by default with a Chinese translation; the license is Apache-2.0.
