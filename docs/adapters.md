# Adapters

A program that does not speak the protocol is a test of level 0: its exit status is its result, and
nothing needs to change. An adapter makes a framework's program speak it; each is checked by the
conformance suite (`MTEST_CTS_PROGRAM`, a command: a program and its arguments, separated by spaces).

| Adapter | Level | Use |
|---|---|---|
| `mcpplibs.mtest` (C++) | 3 | `import mcpplibs.mtest;` — the library itself |
| GoogleTest | 1 | `mcpplibs.mtest.gtest` as a dev-dependency, whose entry begins a test file (mcpp 2026.11.8.1+; earlier: `[test] main = { from = "mcpplibs.mtest.gtest/main" }` and `compat.gtest` as a dev-dependency too); identifiers are `<suite>/<name>` |
| Bash | 3 | `. mtest.sh`, then `mtest_case ID FUNCTION [--tag T ...]` and `mtest_report` |
| Python | 3 | `import mtest_report as mt`, `@mt.case("id")`, `mt.report()` |
| C ABI | 3 | `include/mtest.h` and `libmtest`, for any language with a C FFI |

A script states the protocol with the line `# mtest-protocol=1`, which a runner reads (P2.3).

Every level-3 implementation states the same attributes of a case, in its own words:

| Attribute | C++ | Bash `mtest_case` | Python `@mt.case` | C ABI |
|---|---|---|---|---|
| kind | type, `kind::e2e` | `--kind e2e` | `kind="e2e"` | `mtest_case(id, file, line, "e2e")` |
| tag, clause, resource, artifact | `tag{}`, `clause{}`, `resource{}`, `artifact{}` | `--tag`, `--clause`, `--resource`, `--artifact` | `tags=`, `clauses=`, `resources=`, `artifacts=` | `mtest_case_tag()` and the like |
| time limit | `timeout{}` | `--timeout S` | `timeout=S` | `mtest_case_timeout(S)` |
| isolation, retries | `isolate{}`, `retries{n}` | `--isolate`, `--retries N` | `isolate=True`, `retries=N` | `mtest_case_isolate()`, `mtest_case_retries(n)` |
| diagnosis | `diagnose{}` | `--diagnose-env K=V`, `--diagnose-timeout S` | `diagnose={"env": {...}}` | `mtest_case_diagnose(k, v, s)` |
| bounds (P6.5) | `at_most{}`, `at_least{}` | `--at-most M=V`, `--at-least M=V` | `at_most={M: V}`, `at_least=` | `mtest_bound(id, metric, min, max)` |
| expected verdict | `expect_failure{}`, `skip_if{}` | `--expect-fail`, `--skip` | `expect_fail=`, `skip_reason=` | `mtest_case_expect_fail()` |

A case that states no kind is of its program's (its directory's, docs/layout.md).
