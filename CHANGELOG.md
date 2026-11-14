# Changelog

## [0.8.4] - 2026-11-14

- **A test program links where no program can be started** (the Web). A capability that asks for a
  command (`probe::command`) ran it through `mcpplibs.os`'s process functions, which every test
  program could reach; a target whose system provides none of them -- `wasm32-emscripten`, by
  openkal's rule that an absent capability is absent at link -- then linked no test program at all
  (`undefined symbol: kal_process_wait`, ...), since 0.8.0. The target answers at compile time now:
  asked for a command there, the capability is absent ("this system starts no programs"), and
  nothing of starting one is in the program. Nothing changes where programs are started.

## [0.8.3] - 2026-11-14

- **Each call measured by itself, as a loop** (`bench::laps`). `bench::each` takes the call as a
  function; a body that keeps its state in the function it runs in writes the same measurement as
  a loop: `bench::laps run{"draw"}; while (run.next()) { ... }`. The time from one `next` to the
  following one is a call's, `run` is the call's `lap` (`pause`, `resume`, `items`, `part`, `time`,
  `value`, `say`, `last`), and when `next` returns false the metrics have been reported as `each`
  reports them; `result()` is where the calls lay. `each` is that loop around its function: one
  way of measuring, in two forms.
- **Run alone, a program shows what it measured.** A measurement's line in the program's output --
  `draw: 2.100 ms (p95 5.300 ms, p99 7.700 ms, max 8.400 ms; mean 2.900 ms; 139 calls; 0 over
  8.333 ms)`, then a line for each part and quantity -- where no runner keeps the run's record.
  Under a runner nothing is added to the case's output: the record has the metrics.
- **What the lap keeps is not the call's.** The allocations `part`, `time` and `value` make to keep
  what a call says are left out of the call's count: a frame that allocates nothing reports none
  while it states its parts (`lib/tests/allocations.cpp`, a program that replaces `operator new`).
- **A program run alone selects as the runner does.** Its arguments name cases by identifier, by
  the name they lie under (`suite/group`, which was run and then reported as no case), or by a
  pattern with `*` (the cases it matches, as `mcpp test --case` takes one); a selection no
  case answers fails the run. The interface said "by prefix" and the program took whole
  identifiers: the statement and the behaviour are now one (`lib/tests/alone.cpp`).
- Additive: `each` measures and reports as it did.

## [0.8.2] - 2026-11-14

- 0.8.1 as released: its tag was made and its release was not (a test of `bench::each` held that
  the call it had slowed was the slowest of all, which a loaded machine does not grant). The tests
  now hold each stated call to what that call said, whichever calls the machine slowed. The library
  is 0.8.1's.

## [0.8.1] - 2026-11-14

- **A measured call says what it was made of** (`bench::each`, `mcpplibs.mtest.bench`). The body's
  `bench::lap` takes `part(name, spent)` -- a part of the call the body timed, or `auto t =
  l.time(name);` to the end of `t` -- reported over the calls as `name.<part>` (the median) with
  `.p95`, `.p99`, `.max` and `.mean`, metrics a bound can name (`at_most{"draw.encode.p99",
  0.004}`); `value(name, v)`, a quantity of the call, reported as `name.<quantity>` (the mean)
  and `.max`; and `say(text)`, words about the call.
- **The slowest calls are stated one by one** (`each_options::slowest`): each with its time, its
  parts, its quantities and its words, a line in the case's output (`draw: call 412 took 9.100 ms:
  encode 7.900 ms, glyphs 24570 -- zoom 0.43`) and in `distribution::slowest` -- why the slow
  calls were slow, from the run that measured them, with no second run under a tool.
- **A limit a call, and a run the body ends** (`each_options::limit`, `lap::last`). With a limit (a
  frame's 8.33 ms) `name.over` is how many calls took more and `name.over.share` their part of
  all -- `at_most{"frame.over", 0}` is no late frame -- and the calls over it are the ones stated
  one by one. `l.last()` makes this call the last: what is measured has run out (a recorded
  session's last frame), whatever the options still ask.
- **Heap allocations a call** (`bench::allocated`). The library replaces no `operator new`; a
  program that counts says so from its own (`bench::allocated(n)`), and `each` then reports
  `name.allocations` (the mean a call), `name.allocations.max` and `name.allocated` (bytes), less
  what a call paused for: `at_most{"draw.allocations.max", 0}` is a frame that allocates nothing.
  Counted a thread -- the calling thread's allocations are the call's, and the count is no line
  two threads write: a program whose workers allocate is not slowed by being measured.
- Additive: a body that says none of these is measured and reported as it was; no event and no
  clause changed.

## [0.8.0] - 2026-11-14

- **Where a case runs is stated by the case** (`only{}`, `except{}`). A case or a suite names the
  platforms and architectures of `mcpplibs.os` it runs on (`only{platform::windows}`,
  `only{platform::linux, architecture::x86_64}`) or not (`except{platform::macos}`); a suite's and
  its case's narrow together. Elsewhere it is skipped with one reason, `only on windows (this host:
  linux x86_64)`. The Bash adapter (`--only`, `--except`), the Python adapter (`only=`,
  `excepted=`) and the C ABI (`mtest_case_only`, `mtest_case_except`) state the same; the engine
  skips a listed case outside its places without starting it.
- **Code for one platform is selected when it is compiled.** mtest re-exports mcpplibs.os's
  `platform`, `architecture`, `can_run_on`, `host` and `on<P>`: `on<platform::windows>([&](auto) {
  ... })` is a body that a build for another platform discards, and an ok image decides when it is
  loaded.
- **What a machine can do is declared in code and answered by the program** (`capability`). The
  tests of `[capabilities]` -- a system, an architecture, programs on PATH, a file a glob matches, a
  command, environment values, other capabilities that must or must not hold -- and a function for
  what they cannot state, answered once per process. A capability is an attribute
  (`"links"_test[gcc]`, with the tag `requires:gcc`); a case whose machine lacks it is skipped,
  `missing capability: gcc (no g++ on PATH)`, under any runner (`mtest`, `mcpp test`, the program
  alone). The engine takes the program's answers: a word the program answers needs no
  `[capabilities]` entry, and the run records it.
- **The listing says why a case is skipped, and where it runs** (P3.3): a `case` event may carry
  `reason` (with `expect: skip`), `only`, `except` and `capabilities`; the schema states them. The
  conformance suite checks them of every level-3 implementation (`placed`).
- **`mtest ci coverage` checks each case on its places**: a case that states `only` must pass on
  each of its places that a collected run covered, and some run must have been on one -- what
  `[ci.coverage] hosts` states of a program, stated by the case.
- Additive: a program, a listing and a configuration that state none of these mean what they did.

## [0.7.2] - 2026-11-13

- **Each call measured by itself** (`bench::each`, `mcpplibs.mtest.bench`). `bench::measure` times
  batches and states what one call costs on the whole; `each` times every call, for work whose
  calls differ and whose slow ones are the point -- a frame, a request, a step of a simulation. It
  reports where the calls lie as metrics a bound can name: the median as `name`, and `name.p95`,
  `name.p99`, `name.max`, `name.mean` (`at_most{"draw.p95", 0.016}`). The body may take a
  `bench::lap`: `pause()` and `resume()` leave out what is not the call's (preparing the next
  input), `items(n)` states what the call worked through and adds `name.per_item`. The samples are
  not stated with these metrics: the verdict on samples that spread is for a measurement meant to
  be one number. `bench::distribute` and `bench::percentile` are the same arithmetic on samples a
  test has already. Additive; no event and no clause changed.

## [0.7.1] - 2026-11-13

- **`mcpplibs.mtest.e2e`, `.bench` and `.cts` are public modules of the library.** Each re-exports
  `mcpplibs.mtest`, so the lib root cannot re-export them, and a program that imported one was told
  that it was not public and would fail against the packed library (mcpp SPEC-008). Each is now a
  feature, on by default (`e2e`, `bench`, `cts`), which is how mcpp counts a module beside the root
  as public; a program's imports and its manifest are unchanged.

## [0.7.0] - 2026-11-13

- **What a machine can do is declared once, and a requirement no machine answers is an error**
  (`[capabilities]`). Each word states its tests -- a system, an architecture, a program on PATH, a
  command that succeeds, a file a glob matches, an environment value, other words that must or must
  not hold -- and a case that requires a word (`requires:<word>`, from `# requires:` lines through
  `tags_from`) that this machine lacks is skipped with the reason `missing capability: <word> (<the
  test that failed>)`. A word no entry declares stops the run, naming the program. `mtest
  capabilities` prints the answers; each run records its host (`MTEST_HOST`, else the system) and
  its answers. Before, a project probed in a script of its own and kept a list of known words beside
  it.
- **Whether every program ran somewhere is a command** (`mtest ci coverage`). Over the runs of all
  the jobs of a CI run, collected into one store, each program is `ran`, `unaffected`, `excused` or
  `owed` by the tables `[ci.coverage]` names, or `uncovered`, which fails; an entry that outlived its
  reason fails too, and a program that must pass on given hosts is checked on each. The capability
  words no job's machine provided are listed.
- **A selection is divided among CI's hosts before any builds** (`mtest ci plan`): for each host, the
  cases its recorded capabilities admit, their measured time on that host, and the number of shards
  that fit `[ci.plan] shard`; `--format github` writes a job matrix to `GITHUB_OUTPUT`. A host with
  no history counts every case.
- **An interrupted run resumes** (`mtest run --resume RUN`): the cases RUN judged are carried into
  the new run and are not run again.
- **A program under test is judged with matchers that carry the session**
  (`mcpplibs.mtest.e2e`): `succeeded`, `exited`, `contains`, `matches`; an outcome carries the
  command as a POSIX shell line (`shell_line`), and a failed matcher reports it with the tails of
  both outputs.
- **An interactive program is driven by expecting and sending** (`e2e::session`: `start`, `expect`,
  `saw`, `send`, `send_line`, `finish`, `transcript`). Both streams travel in one pipe; no terminal
  is involved, so the same session runs on every system. A serial console of an emulator is driven
  this way (measured: QEMU's serial console delivers the same bytes over a pipe as over a pseudo
  terminal).

## [0.6.3] - 2026-11-12

- **A conformance suite is run against a subject, which may lack what a specification leaves
  optional** (`mcpplibs.mtest.cts`: `against`, `subject`, `needs`). `cts::against({.name, .has,
  .required}, declare)` runs the cases `declare` declares as the parts of a case named after the
  implementation, tagged `subject:<name>`, so one program runs a suite against several. A case that
  states `cts::needs{"capability"}` and whose subject lacks it is not run: `skip`, with the reason
  `unsupported: <capability>` -- from what the subject states it can do, never a pass. A capability
  the run requires and the subject lacks fails once, as the case `requires <capability>`. Before, a
  suite wrote this in each body (`if (!has(x)) skip(...)`), where a missing capability and a
  machine's reason read alike, and a required one could only be skipped. The protocol is unchanged:
  the statuses and events are the ones it has.

## [0.6.2] - 2026-11-12

- **The engine and the libraries write the version they are.** 0.6.1's manifests said 0.6.1 and its
  sources still wrote 0.6.0: `mtest version`, the catalog, a session's `serverInfo` and a test
  program's hello. A CI that asks the installed engine for its version refused it. They write 0.6.2,
  CI compares every source's statement with the manifests, and the packaged engine is asked.

## [0.6.1] - 2026-11-12

- **A program judged by its exit status can say that it did not apply** (`skip_exit`, in
  `[programs.<name>]` and `[discover.<group>]`): the status that means its one case was not run
  here, for a reason of the machine. The case is then `skip`, not `pass`, and its reason is the last
  line of the program's output that begins with `SKIP`. Before, such a program had two words, and a
  script that could hold nothing on a machine exited 0 and was counted among the passes. Without
  the key every status but 0 fails, as before.

## [0.6.0] - 2026-11-10

The mtest protocol's normative text is the mcpp test spec (mcpp `docs/specs/test.md`, SPEC-017 v2.0),
which keeps its clause numbers; `spec/protocol.md` lists what each part of mtest implements of it.

- **A test session** (spec O9): `mtest serve` answers `test/plan`, `test/run`, `test/add`,
  `test/cancel` and `test/status`, and states `test/build`, `test/case`, `test/program` and
  `test/run` as they happen. A run is answered at once and its programs run on the session's
  workers beside other runs; a case added joins the run in progress; cancelling ends the process and
  everything it started, and the cases that had not begun run in another process. The session builds
  nothing: it starts each program from the launch mcpp gives (`mcpp test --no-run`, spec B5).
- **A case's own output** (spec O8). Under a runner the library keeps what each case writes, on each
  stream, states it as `output` events and writes it on to the terminal; a case declared inside
  another keeps its own.
- **A case's checks** (spec O3): its `end` states the checks that held and failed (`checks`); a
  failure states its statement (`source`) and whether it ended the case (`fatal`).
- **A failing case reads as a person reads it.** Under its line, each failure: its place, the
  statement, the two sides or that it did not hold, the message the check added, and whether the case
  went on; the line counts the checks that failed, and a case that declared cases counts them.
- The library's entry states the capabilities it implements and its session (`test-capabilities`,
  `test-session`; mcpp 2026.11.10.1 reads them, an earlier mcpp ignores them).
- The engine and the conformance suite ask for mcpplibs.os `^0.4.1`, as the library does, and build
  `<arch>-openkal` over openkal-platform 0.25.0.
- **A program only a runner starts is listed through that runner.** The listing pass (P3) starts a
  program as its run does: under `--run-under` (and a `[trace.*]` tool's `run_under`), with the
  run's `--env` variables. A program of another system -- started on a virtual machine of its system
  or under an emulator -- was started directly to list its cases and failed with "did not start";
  its cases could not be run through mtest from the host that built it.

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
