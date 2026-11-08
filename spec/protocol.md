# The mtest test protocol, version 1

**Status:** version 1, 2026-11-08; P3.3, P3.4, P6.5 and the `fixture` event added 2026-11-08; P6.6, P7.3, P10, P11, P12 and the `usage` and `component` events added 2026-11-09 (P9.2).
**Scope:** how a test program and a runner talk. A runner is anything that starts test programs and reads their results: the mtest engine, mcpp (`mcpp test`), an IDE, a CI step. A test program is any program, in any language.

This document is normative. The words *must*, *must not* and *may* have their usual meaning. Each clause has a number; tests and conformance reports name clauses by number (`P3.2`).

## P1. Levels

| Level | What the program does | What the runner obtains |
|---|---|---|
| L0 | nothing of this protocol | one result per program, from its exit status |
| L1 | P3, P4, P5 | the case list, the selection of cases, a result, a location and a message per case |
| L2 | L1 and P6 | metrics: samples, units, bench verdicts, conformance clauses |
| L3 | L2 and P7 | resources and artifacts per case: scheduling and affected selection |

P1.1 A program that does not implement the protocol is an L0 program. A runner must run it unchanged and must judge it by its exit status alone: zero passes, anything else fails.

P1.2 A runner must not pass the protocol's variables (P2) to a program it does not know to implement the protocol, unless it accepts that an L0 program runs completely (P2.4).

## P2. The handshake

P2.1 A runner states the protocol in the program's environment. All variables are optional except `MTEST_PROTOCOL`.

| Variable | Value |
|---|---|
| `MTEST_PROTOCOL` | the protocol version the runner speaks: `1` |
| `MTEST_MODE` | `list` or `run` (default `run`) |
| `MTEST_REPORT` | a file the program appends its events to (P5); absent: no events are written |
| `MTEST_SELECT` | a file of case identifiers, one per line (P4); absent: every case |
| `MTEST_ISOLATION` | `process` (several cases in this process, the default) or `case` (the runner starts the program once per case) |
| `MTEST_TMPDIR` | a directory the program may use for its cases, removed by the runner |
| `MTEST_SEED` | the seed of anything random in the cases, as a decimal number |
| `MTEST_MONITOR` | how closely the program measures its use of the machine: `off`, `basic`, `detailed`, `trace` (P10) |
| `MTEST_MONITOR_SAMPLE` | `trace`: the milliseconds between two samples (P10.3) |
| `MTEST_FIXTURE_<NAME>` | the endpoint of the run fixture `<name>` (P11.2), its name upper-cased with each character that is not a letter or a digit written `_` |
| `MTEST_SERVE` | the run fixture the program serves instead of running cases (P11.3) |

P2.2 A program that implements the protocol must not pass these variables to a program it starts (each is removed from the started program's environment), so that a test program started by a test is not taken for the runner's. A test that starts a test program as a runner sets the variables it means for it.

P2.3 A program states that it implements the protocol in two ways:
- by a host declaration: a build system's manifest names the protocol for the program (mcpp: the test entry's package declares `test-protocol = 1`; mcpp docs/08);
- by a marker: the bytes `mtest-protocol=1` appear in the program's file, followed by a byte that is not a digit. A runner may read the file to find it. A program built from mtest's libraries carries the marker.

Either statement is a claim the program's report confirms: a program that writes no `program` event is L0 (P2.4), as a file of plain `main` linked with a library that carries the marker is.

P2.4 A runner that knows neither may still set `MTEST_MODE=list` and `MTEST_REPORT`. The first event (P5.2) tells it the level. When no `program` event arrives, the program is L0, and in `list` mode it has run all of its tests once.

P2.5 A framework may state where its guide to writing tests is: a file of its package that a writer of a test, a person or an agent, reads before writing one or after one failed. A host declaration names it (mcpp: the entry's `test-guide = "<file>"`; `mcpp test --guide` lists each framework with its guide), and the `program` event may carry `guide`, a URL, for a runner that learns of the framework only when it runs the program. A runner that names a failure may name the guide beside it. The guide states how a file of cases is written, how an expectation is stated and what its failure reports, and how one case is run.

## P3. Listing

P3.1 With `MTEST_MODE=list` the program must write one `program` event, then one `case` event per case it would run, then one `done` event, and must exit with status 0 without running any case.

P3.2 A case identifier is a string of UTF-8. It is stable: the same case has the same identifier in every run and on every system. A program forms it from the names it gives its suites and cases, joined by `/` (for example `fs/a file is read`). Identifiers in one program are distinct.

P3.3 A case event may name the case's location (`file`, `line`), kind (P8), tags, the clauses it covers, its expected status, and (L3) its resources and artifacts, and how a runner that schedules cases runs it: `timeout`, the seconds it may run; `isolation`, `case` when it runs in a process of its own; `retries`, how many times a failing case runs again, alone; `diagnose`, an object of `env` (variables) and `timeout`, the run a runner makes once after the case fails, whose output it keeps and which judges nothing. A runner that starts a process for one case ends it at the case's `timeout`; one that runs a program whole (mcpp's `mcpp test`) may run every case in it and judges each as reported. A case that names no kind is of the kind the runner gives its program (mtest docs/layout.md).

P3.4 A case that exists only when another case runs (a case declared inside a case) need not be listed; it appears in the report when it runs, and its `begin` event may name its kind, tags and clauses. Selecting it runs the case that declares it as its scope: that case is reported only when a statement of its own fails.

## P4. Selection

P4.1 With `MTEST_SELECT` the program must run exactly the cases whose identifiers are lines of the file, compared byte for byte after removing one trailing `\r`. An identifier that names no case is reported as a `missing` event (P5) and fails the run.

P4.2 Patterns are the runner's: a runner that offers `fs/*` resolves it against the list (P3) and passes identifiers. The protocol has no pattern syntax, so no escaping either.

## P5. Reporting

P5.1 Events are JSON objects, one per line (NDJSON, UTF-8, `\n` after each). The program appends them to `MTEST_REPORT` and must not write anything else to that file. Each line is written whole: a program with several threads must not interleave lines.

P5.2 The first event is `program`. Every event has the field `e`, its kind.

| `e` | Fields | When |
|---|---|---|
| `program` | `protocol` (1), `level` (1–3), `framework`, `version`, `isolation` (the values of P2.1 the program supports), optionally `guide` (P2.5) | first |
| `case` | `id`, `name`, `file`, `line`, `kind`, `tags`, `clauses`, `expect`, `resources`, `artifacts`, optionally `uses` (P11.2) | in `list` mode, one per case |
| `component` | `name` and `pools` (`{name, capacity}`, P7.3), or `fixture` and `scope` (P11.1) | in `list` mode, one per component and run fixture |
| `begin` | `id`, `t` (milliseconds since the program started), optionally `kind`, `tags`, `clauses` (P3.4) | a case starts |
| `failure` | `id`, `file`, `line`, `message`, and optionally `expected`, `actual` (strings), `fatal` | an expectation does not hold |
| `output` | `id`, `stream` (`out` or `err`), `text` | output a case captured |
| `metric` | `id`, `name`, `value`, `unit`, optionally `samples` (array of numbers), `better` (`lower` or `higher`), `precision` (P6.6) | L2 |
| `usage` | `user_ms`, `sys_ms`, optionally `peak_bytes`, `read_bytes`, `written_bytes` | the program's own use at its end (P10.2) |
| `artifact` | `id`, `path`, `kind` | a file the case produced |
| `fixture` | `id`, `name`, `ms` | an object the case caused to be made for the cases that share it, and how long making it took |
| `end` | `id`, `status`, `ms`, optionally `reason` | a case ends |
| `missing` | `id` | P4.1 |
| `done` | `status`, `counts` (an object of status to number) | last |

P5.3 A case's `status` is one of `pass`, `fail`, `skip` (with `reason`), `xfail` (failed as expected), `xpass` (passed although expected to fail; a failure of the run), `invalid` (a measurement that cannot be judged; P6.3).

P5.4 Unknown fields and unknown event kinds must be ignored by a runner. A runner must accept a report that ends without `done` (the program crashed): the cases that have a `begin` without an `end` failed, with the program's exit status as their reason.

P5.5 The program's exit status is zero exactly when no case failed or passed unexpectedly and no selected case is missing.

## P6. Metrics (L2)

P6.1 A metric belongs to a case and has a name unique within it, a value and a unit (`s`, `ms`, `B`, `1` for a ratio or a count).

P6.2 A bench case reports its samples. The runner, not the program, decides whether a measurement is valid and whether it passes, by the policy the project declares (pairs, spread bound, limit, whether a measurement may be repeated).

P6.3 A program may itself report `invalid` when it knows its measurement cannot be judged (for example, the machine was too noisy by its own test). A runner must not count an `invalid` case as passed or failed, and its policy decides whether it may be repeated.

P6.4 A conformance case names in `clauses` the clauses of the specification it covers. A runner forms the matrix of clause × implementation × system from them.

P6.5 A case may bound its metrics: `bounds`, a list of `{metric, max, min}` (either limit may be absent), in the metric's unit, in its `case` event and, for a case not listed, its `begin` (P3.4). The program judges them as expectations at the case's end: a metric over its `max` or under its `min`, or a bounded metric the case did not report, is a `failure` of the case. A case that declared cases is judged by them. A runner reads the bounds to show a metric beside its limit (mtest's `analyze trend`); a measurement against a baseline stays the runner's (P6.2).

P6.6 The use of the machine is reported as metrics of standard names, each with its `precision`: `exact` (the case's own, measured at its bounds), `process` (the program's, of which the case is a part), `approx` (estimated), `sampled` (P10.3).

| Metric | Unit | What |
|---|---|---|
| `cpu.user`, `cpu.sys` | `ms` | processor time in user and system mode |
| `mem.peak` | `B` | the largest resident set |
| `mem.delta` | `B` | the growth of the largest resident set over the case |
| `mem.rss` | `B` | the resident set, sampled |
| `cpu.load` | `1` | the processors in use, sampled |
| `io.read`, `io.write` | `B` | bytes read and written |
| `net.sent`, `net.recv` | `B` | bytes sent and received |
| `alloc.count`, `alloc.bytes` | `1`, `B` | allocations and their bytes |
| `proc.spawned` | `1` | programs started |

## P7. Resources and artifacts (L3)

P7.1 `resources` names what a case needs exclusively or in limited number: strings such as `serial:<group>`, `machine` (alone on the machine), `gpu`, `network`, `display`. A runner must not run two cases that hold the same `serial:` group, or a `machine` case with any other, at the same time.

P7.2 `artifacts` names what a case reads: build targets (`target:<name>`), files and directories relative to the project (`file:<path>`), external programs (`program:<name>`). A runner selects the cases affected by a change through them.

P7.3 A resource may have a capacity: a pool a component brings (P11.1) states how many cases hold it at once. A runner must not run more holders of a pool than its capacity; a resource with no stated capacity has one.

## P8. Kinds

`unit`, `e2e`, `cts` and `bench`. A kind states what is tested; the set a case belongs to (smoke, regression) is the runner's and the project's (mtest docs/sets.md), not the program's.

## P9. Versions

P9.1 A runner that speaks version N sets `MTEST_PROTOCOL=N`. A program that implements only lower versions answers with the highest one it implements in its `program` event; the runner then uses that version or treats the program as L0.

P9.2 Within a version, fields and event kinds are only added. A field changes meaning only in a new version.

## P10. Use of the machine

P10.1 A runner states the level in `MTEST_MONITOR`. `off`: nothing is measured. `basic`: each case's processor time (P6.6, `exact`), and the program's own use at its end. `detailed`: also each case's largest resident set and its growth, and the bytes it read and wrote. `trace`: also the processor load and the resident set over each case, sampled. A case may ask for a level higher than the runner's; the higher one applies to it.

P10.2 A program at a level other than `off` writes one `usage` event before `done`: its processor time, and where the system tells them, its largest resident set and the bytes it read and wrote.

P10.3 At `trace`, a program samples every `MTEST_MONITOR_SAMPLE` milliseconds (50 when absent) while a case runs and reports each series as one metric whose `samples` are the series and whose `value` is its largest sample, with precision `sampled`.

P10.4 A runner measures a program that writes no `usage` event from a process of its own that starts the program, where the system accounts a process's children (Linux, macOS), and records the measurement as the engine's.

## P11. Components and run fixtures

P11.1 A component is a named part of tests that programs share: the pools of resources it brings (P7.3) and the run fixtures it serves. A program lists each in a `component` event (P5.2).

P11.2 A run fixture is one object for a whole run, used by the cases of several programs: a sandboxed home, a server, an index. A case states the run fixtures it uses in `uses`. A runner serves a run fixture before the first case that uses it starts and ends it after the last ends, and gives its endpoint to every program of a case that uses it as `MTEST_FIXTURE_<NAME>`.

P11.3 A runner serves a run fixture by starting a program that lists it with `MTEST_SERVE` naming it, with its input a pipe and without `MTEST_PROTOCOL`. The program runs no case: it prepares the fixture, writes the line `mtest-ready <endpoint>` to its output, and ends the fixture when its input ends. A runner may also serve a run fixture by a command its project names.

P11.4 A run fixture that does not become ready is the machine's failure: the cases that use it end with status `infra` and do not run.

## P12. The store

P12.1 A runner may keep its runs in a store, which other runners and tools read and write in this form (mtest's `.mtest`; a host's runner, such as mcpp's `mcpp test`, writes the same):

| Path | Content |
|---|---|
| `index.ndjson` | one line per run: the run's record without its programs and cases |
| `runs/<id>/run.json` | the run's record: `schema` (`mtest.run/1`), `id`, `started`, `finished`, `status`, `system`, `arch`, `machine`, `commit`, `set`, `selection`, `group`, `monitor`, `ms`, `jobs`, `counts` |
| `runs/<id>/programs.ndjson` | one line per process of a program: `name`, `argv`, `status`, `exit`, `ms`, `started_ms` (after the run began), `worker`, `select`, `resources`, `fixtures`, `build_started_ms` and `build_ms` when the runner built it, `usage` (P10.2, with `source`: `program` or `engine`) |
| `runs/<id>/cases.ndjson` | one line per case: `program`, `id`, `kind`, `status`, `ms`, `started_ms` (after the run began), `reason`, `failures`, `metrics`, `bounds`, `tags`, `clauses` |
| `runs/<id>/fixtures.ndjson` | one line per run fixture served: `name`, `program`, `started_ms`, `ready_ms`, `ended_ms`, `status` |

P12.2 A run identifier sorts by time. Runs of one `group` (the jobs of a CI run) are parts of one whole, which a reader reports together.

P12.3 The times of a run's programs, cases, builds and fixtures form a directed acyclic graph of its work: a build before the processes of its program, a worker's process before its next, the holders of one exclusive resource in turn, a run fixture's serving before its users and its end after them. A reader's critical path is the graph's longest weighted path, weighted by the measured times.
