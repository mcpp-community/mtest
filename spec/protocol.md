# The mtest test protocol, version 1

**Status:** the normative text is the mcpp test spec, SPEC-017 v2.0
([mcpp docs/specs/test.md](https://github.com/mcpp-community/mcpp/blob/main/docs/specs/test.md)),
from mtest 0.6.0 (2026-11-10). The spec keeps this protocol's clause numbers, P1 to P12, so a test,
a conformance report or a document that names `P3.2` names the same clause there; it adds the base
capabilities B1–B6 that every runner and framework implements and the optional capabilities O1–O9
that a framework or its tools declare.

**Scope of this file:** what mtest implements of the spec, part by part. A runner or a client relies
on the spec; this list says which of its clauses mtest's parts answer, and since which release.

## The library (`mcpplibs.mtest`)

| Clause | What the library does | Since |
|---|---|---|
| B2: P1–P5, P8, P9 | a program built with it speaks the protocol at level 3 when a runner asks (P2.1), lists (P3), selects (P4) and reports (P5) | 0.1.0 |
| B6 | its entry (`mtest/main`) states `test-protocol = 1`, `test-main`, `test-guide`, `test-capabilities` and `test-session` | 0.1.0; the last two 0.6.0 |
| O3 `check-details` | a failure's `expected`, `actual`, `source` (the statement) and `fatal`; a case's `checks` (`held`, `failed`) at its `end` | `expected`/`actual` 0.1.0; the rest 0.6.0 |
| O4 `metrics` | `metric` events; bounds judged at the case's end (P6.5); standard names with their precision (P6.6) | 0.1.0; P6.5 0.4.0; P6.6 0.5.0 |
| O5 `scheduling` | `resources` and `artifacts` (P7); `timeout`, `isolation`, `retries`, `diagnose` (P3.3) in the `case` events | 0.1.0; P3.3's 0.3.0 |
| O6 `fixtures` | `shared<T>` and the `fixture` event; components and run fixtures (P11), served under `MTEST_SERVE` | 0.3.0; P11 0.5.0 |
| O7 `usage` | the program's `usage` at its end, and the cases' use at `MTEST_MONITOR` (P10) | 0.5.0 |
| O8 `output` | each case's output, kept while it runs under a runner and stated as `output` events; a case declared inside another keeps its own. Where the system cannot rearrange the program's streams (an ok image), no output is kept for a case, the spec's fallback | 0.6.0 |

## The engine (`mtest`)

| Clause | What the engine does | Since |
|---|---|---|
| B2, runner side | starts programs of any language, by the protocol or by their exit status (P1.1) | 0.1.0 |
| B4 | keeps every run in the store (P12), and reads the runs a host (`mcpp test`) keeps | 0.1.0; the store's form specified 0.5.0 |
| O5 | schedules cases by their resources and history, by pools since 0.5.0; ends a case at its `timeout`, runs `retries` and `diagnose` since 0.3.0 | 0.1.0 |
| O6 | serves run fixtures once per run (P11.2, P11.3) | 0.5.0 |
| O7 | measures a program that reports no use from a process of its own (P10.4) | 0.5.0 |
| O9 `session` | `mtest serve` answers `test/plan`, `test/run`, `test/add`, `test/cancel`, `test/status` and `shutdown`, and states `test/build`, `test/case`, `test/program`, `test/run`; it builds nothing, and starts each program from the launch mcpp gives (B5) | 0.6.0 |

## The adapters and the C ABI

| Part | Level | Clauses |
|---|---|---|
| `mcpplibs.mtest.gtest` (GoogleTest) | L1 | B2 |
| the Bash and Python adapters (`adapters/script`) | L3 | B2, O4, O5 ([docs/adapters.md](../docs/adapters.md)) |
| `mcpplibs.mtest.capi` (C ABI) | L3 | B2, O4, O5 |

## Conformance

`conformance/` checks a program against the clauses it states, and reports by clause number
([conformance.md](conformance.md)).
