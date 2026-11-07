# Adapters

A program that does not speak the protocol is a test of level 0: its exit status is its result, and
nothing needs to change. An adapter makes a framework's program speak it; each is checked by the
conformance suite (`MTEST_CTS_PROGRAM`, a command: a program and its arguments, separated by spaces).

| Adapter | Level | Use |
|---|---|---|
| `mcpplibs.mtest` (C++) | 3 | `import mcpplibs.mtest;` — the library itself |
| GoogleTest | 1 | `mcpplibs.mtest.gtest` as a dev-dependency, whose entry begins a test file (mcpp 2026.11.8.1+; earlier: `[test] main = { from = "gtest/main" }`); identifiers are `<suite>/<name>` |
| Bash | 3 | `. mtest.sh`, then `mtest_case ID FUNCTION [--tag T ...]` and `mtest_report` |
| Python | 3 | `import mtest_report as mt`, `@mt.case("id")`, `mt.report()` |
| C ABI | 3 | `include/mtest.h` and `libmtest`, for any language with a C FFI |

A script states the protocol with the line `# mtest-protocol=1`, which a runner reads (P2.3).
