# Changelog

## [0.5.2] - 2026-11-09

- **A copy of a test program that does something else is an object** (`child`). A test that starts
  copies of itself -- the child process under test, a producer its cases talk to -- declares what a
  copy does, and starts one with `child_command(name, args)`; the entry enters the child named by
  the copy's arguments before any case, and the copy runs no case and reports nothing. Such a file
  defines no `main`, so the files of one directory are one program however many of them start
  copies (mcpp `[test] programs = "directory"`).
- The library asks for mcpplibs.os `^0.4.1` rather than 0.4.1 itself: it is in the test graph of every
  package that uses it, and such a package --- mcpplibs.os's own checks among them --- pins its os.

## [0.5.1] - 2026-11-09

- **The protocol marker survives a link that keeps only what it reaches** (P2.3). The library, the
  GoogleTest adapter and the C ABI read the marker's address through a volatile pointer where they
  read one byte of it before, which the compiler folded to a constant: a link with
  `--gc-sections` over per-object sections (`mcpp test` 2026.11.9.1) then discarded the marker,
  and the runner treated the program as one that speaks no protocol. The conformance fixture is
  linked so on Linux.

## [0.5.0] - 2026-11-09

A run is a graph of work, and mtest reports it: what ran, what each part waited for, what it used.

- **The run's graph and its critical path** (P12.3). The engine records each process's worker and
  selection, each case's start, each run fixture's serving and, from a host, each program's build;
  they form a directed acyclic graph whose edges are a build before its program, a worker's previous
  process, the previous holder of a resource and a fixture's serving. `mtest analyze path` is the
  graph's longest weighted path, with what each step waited for (it was the longest program);
  `mtest graph --run last --format mermaid|gantt|dot|json` draws it, the critical path marked.
- **Markdown reports.** `mtest report --format md` writes the sections asked for (`--sections` or
  `[report]`): the summary, kinds, failures, slowest cases, metrics beside their bounds, the use of
  the machine, flaky cases, trends, the critical path, the graph and the schedule, the last three in
  mermaid; over a scope (`program:<glob>`, `kind:<kind>`); of one run, `--runs`, or every run of a
  `--group` together. `mtest run --md FILE` writes one at the end of a run, and `mtest ci annotate`
  writes it to the job's summary.
- **CI as a whole.** `mtest ci report` reports every collected run of a group (the jobs of a CI run,
  `GITHUB_RUN_ID` by default) as one document; `mtest ci timeline --repo R --run-id N` draws a
  workflow run's jobs as a schedule with its critical path.
- **Use of the machine** (P10, P6.6). `[monitor] level` (`basic` by default), `--monitor` and
  `monitor{...}` on a case: each case's processor time; at `detailed` its largest resident set, its
  growth and its I/O; at `trace` the load and the resident set sampled over it; the program's own use
  in a `usage` event. Each is a metric with its precision. A program that reports none is measured
  from a process of the engine's (`mtest measure`, Linux and macOS). Uses mcpplibs.os 0.4.1.
- **Scheduling by measurement.** `[run] split = "auto"`: while workers would idle, a program's cases
  are divided among several processes of it by their measured times. `[run] memory = "auto"`:
  programs run together while their measured largest resident sets fit the machine. `[programs.*]
  split = false` keeps a program in one process.
- **Components and run fixtures** (P11, P7.3). `run_fixture` serves one object for the whole run,
  started before the first case that `uses` it and ended after the last, its endpoint given as
  `MTEST_FIXTURE_<NAME>` (`fixture_endpoint`); `Component` names the pools of resources it brings, a
  resource with a capacity. `[fixtures.<name>] command` serves one by a command, and `[cases."<glob>"]
  uses` states the users of a program that states none.
- **The store is specified** (P12): its files and fields, which a host's runner writes as mtest does.

## [0.4.1] - 2026-11-08

- A body that returns a value does not compile. A case ends at its end, at `return;`, at `skip()`
  or at `fail()`; a value a body returned was lost, and a `main` made into an object's body kept its
  `return 1;` as undefined behaviour.
- The static manifest reads a project's own suite types wherever the project declares them:
  `manifest::types_of(sources)` gives the types a set of sources declares (one derived from a type
  of another included), and `scan(source, types)` reads a file's cases with them. `mtest list
  --static` reads the types of every C++ source of the project, so a type written once in a module
  its tests import is its base's kind in each of them.
- A tag selection `a&b` names the cases that have both tags (`--tag 'on:linux-x64&release'`, and a
  set's `tags`); the tags given are still alternatives. A case has its program's tags as well as its
  own.

## [0.4.0] - 2026-11-08

The form of a project's tests, and what a case may state about its metrics.

- **Bounds on metrics** (P6.5): `at_most{"ready", 15}` and `at_least{"recall", 0.9}` after a case's
  name. The case fails when a metric it reports passes a limit, or when it reports a bounded metric
  not at all; a case that declared cases is judged by them. The bounds are listed with the case
  (`bounds`), read by the static manifest, stated by the Bash adapter (`--at-most`, `--at-least`), the
  Python adapter (`at_most=`, `at_least=`) and the C ABI (`mtest_bound`), and by `mtest.toml`'s
  `[cases."<glob>"] at_most = { ... }`, which the engine judges for a program that states none. The
  conformance suite's `bounded` case and clause P6.5 hold every level-3 implementation to it.
- `mtest analyze trend`: each case's metrics over the last `--runs N` stored runs, oldest first, with
  their range, median and last value beside their bounds and the margin the last leaves.
- The engine writes a program's output that is not UTF-8 (a Windows code page) with the
  replacement character; 0.3.0 ended with an exception storing such a run.
- The Python adapter states what 0.3.0 added: a time limit, isolation, retries, a diagnosis; a case
  that names no kind is of its program's.
- `docs/layout.md` is the form of a project's tests: the directory, the project, the code, the data
  and the analysis; `docs/adapters.md` lists each attribute in every implementation's words.

## [0.3.0] - 2026-11-08

A case states in code what it is and what it needs, and the runner acts on that statement and
records what it did.

- An object is a case: `UTest`, `E2E`, `CTS` and `Bench` (and `Suite`, of no kind) take a name, any
  attributes and a body. Making the object records them; a runner lists the object as one case,
  schedules it by its attributes and runs its body when it is selected. Cases the body declares are
  under its name, have its attributes and add their own, and can be selected alone; the body's other
  statements run only when one of them does. A case that runs only as the scope of a selected one is
  reported only when a statement of its own fails (P3.4). `suite` remains as the spelling of 0.2.
- Attributes for how a case is run: `timeout{}`, `isolate{}`, `retries{n}` and `diagnose{.env, .limit}`
  (a failing case runs once more with that environment; its output is kept and named by
  `mtest explain`). P3.3 gains `timeout`, `isolation`, `retries` and `diagnose`; the Bash adapter
  (`--timeout`, `--isolate`, `--retries`, `--diagnose-env`, `--diagnose-timeout`) and the C ABI
  (`mtest_case_timeout`, `mtest_case_isolate`, `mtest_case_retries`, `mtest_case_diagnose`) state
  them too.
- `shared<T>`: an object the cases of a body share, made when one first uses it; the `fixture` event
  reports which case made it and how long that took.
- A case that states no kind is of its program's: `mtest.toml`'s, else its directory's
  (`tests/unit`, `tests/e2e`, `tests/cts`, `tests/bench`; `docs/layout.md`), else unit. The library,
  the Bash adapter and the C ABI no longer state `unit` for a case that names no kind.
- `[cases."<glob>"]` takes the library's attributes in the same words (`clauses`, `artifacts`,
  `isolation`, `retries`, `diagnose`, `skip`, `expect_fail` beside `kind`, `tags`, `resources`,
  `timeout`); what the program states comes first (in 0.2 the file took precedence).
- The engine runs a case that states isolation in a process of its own, ends a process that runs one
  case at that case's time limit and gives a process of several at least the sum of theirs, retries
  a case as often as it states, records each program's start and what it held (`timeline` of
  `--report-json`), and records skips and expected failures `mtest.toml` states.
- The static manifest is a package of its own, `mcpplibs.mtest.manifest`, which a tool that reads
  tests (the engine, an editor) depends on without linking the library a test program links. It
  reads the suite types, a suite type the file derives from one
  (`class Fixture : public CTS`), a suite's attributes, `resource`, `clause` and `artifact`;
  `mtest list --static` gives a case the kind and attributes `mtest list` gives it.
- Discovery globs match `**/` as no directory too (`tests/**/*.sh` matches `tests/a.sh`).

## [0.2.0] - 2026-11-08

- `[discover.<name>]`: every file a glob matches is a program run by a command (`{path}`,
  `{name}`), with the keys of `[programs.*]`; tags and resources are read from each file's first
  lines (`tags_from`, `resources_from`), so a directory of scripts is a suite without a line of
  configuration per script. `kind` states the kind of an L0 program's one case.
- `[cases."<glob>"]`, and a host's `[package.metadata.mtest.cases."<glob>"]` in mcpp.toml, state the
  tags, resources, kind and timeout of the cases a glob matches, taking precedence over what a
  program lists.
- `--exclude-program GLOB` leaves the programs a glob matches out of a selection; globs take character classes (`[0-9]`, `[!x]`).
- `--shard I/N` selects the I-th of N parts: each program, the longest first, to the part with the
  least expected time; every machine with the same store computes the same parts.
- The coverage map (`docs/coverage.md`): `mtest run --coverage` records, for each case, the sources
  its run executed, read from GCC's `.gcda` counters and a compile database; affected selection
  reads it, and `mtest coverage show|import` and `mtest store import` carry it between machines.
- The static case manifest: `mcpplibs.mtest.manifest` reads the cases a source file declares
  (`"name"_test`, their attributes, named suites, nested cases) from its text, and
  `mtest list --static` lists a project's test files with it, building and running nothing.
- A set that selects by change (`pr`) selects every case when the change cannot be read (a shallow clone
  without its base), and says so; before, it selected none of the affected cases.
- A failing case's entry in a run's answer names its kind.

## [0.1.0] - 2026-11-08

The first release.

- The mtest test protocol, version 1 (`spec/protocol.md`): levels L0–L3, the handshake, listing,
  selection, NDJSON reporting, metrics and resources; its conformance suite checks every
  implementation (`spec/conformance.md`).
- `mcpplibs.mtest`: the C++ library, with no macro and no header; `bench`, `cts` and `e2e`.
- The engine, `mtest`: run, schedule, store, explain, analyze and gate, on the command line, as a
  JSON-RPC service and as MCP tools; released as ok images for Linux, macOS and Windows.
- Adapters: GoogleTest (`mcpplibs.mtest.gtest`), Bash and Python; the C ABI (`mcpplibs.mtest.capi`).
