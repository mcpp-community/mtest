# Writing tests with mcpplibs.mtest

The guide `mcpp test --guide` names. A test is a file under `tests/`; each file is one program, and
its cases are what a runner (`mcpp test`, `mtest`, an editor) lists, selects, schedules and reports.
A case states in code what it is and what it needs; the runner acts on that statement and records
what it did.

## A file of cases

```cpp
import std;
import mcpplibs.mtest;
import mylib;                         // what the cases test

using namespace mcpplibs::mtest;

UTest empty{"an empty line is no record", [] { expect(eq(mylib::parse(""), 0)); }};

UTest fields{"fields", tag{"parsing"}, [] {
    const auto text = mylib::sample();          // runs when a case of `fields` runs
    "a field holds its text"_test = [&] {
        auto r = mylib::parse(text);
        expect(fatal(r.size() == 2)) << "two fields";   // ends the case when it fails
        expect(eq(r[1], std::string("b")));
    };
    "an escaped separator"_test[tag{"escapes"}] = [&] { expect(eq(mylib::parse("a\\,b").size(), 1u)); };
}};

E2E cli{"cli", resource{"serial:home"}, timeout{std::chrono::minutes{2}}, [] { /* ... */ }};
```

- **An object is a case.** Making it records its name, its attributes and its body; nothing runs.
  A runner lists it with those attributes and runs its body when it is selected.
- **A case may declare cases.** `"name"_test = body` inside a body declares a case under it
  (`fields/a field holds its text`). It has the attributes of what declares it and adds its own, and
  it can be selected alone; the statements around it, which its siblings share, run only when one of
  them runs. One object is therefore a single test, a group of tests, or both.
- **The type is the kind.** `UTest` (unit), `E2E`, `CTS` (conformance) and `Bench` state their kind;
  `Suite` states none, and its case is of the kind the program's directory gives it (`tests/e2e/` is
  e2e; [docs/layout.md](../../docs/layout.md)). `suite` is the spelling of 0.1 and 0.2.
- A file declares objects and defines no `main` (with mcpp 2026.11.8.1 or later; an earlier mcpp
  needs `[test] main = { from = "mtest/main" }`). A file that defines `main` declares its cases in it
  and returns `report()`.
- **A test that starts copies of itself declares what a copy does**, and defines no `main` either
  (0.5.2):

  ```cpp
  child producer{"producer", [](std::vector<std::string> args) { return produce(args); }};
  // in a case: the program again, as the child, given its arguments
  auto o = e2e::run({.argv = child_command("producer", {"--fast"})});
  ```

  The entry enters the child before any case; the copy runs no case and reports nothing. The files of
  one directory are then one program (mcpp `[test] programs = "directory"`) however many of them start
  copies.
- The manifest names the library once: `[dev-dependencies] mcpplibs.mtest = "0.7.0"`.
- Identifiers are stable: a selection, a history and a shard name a case by them.

## Expectations

| Write | Holds when |
|---|---|
| `expect(cond)` | `cond` is true |
| `expect(eq(a, b))`, `ne`, `lt`, `le`, `gt`, `ge` | the comparison holds; a failure reports both sides |
| `expect(near(a, b, tol))` | `|a - b| <= tol` |
| `expect(contains(text, part))` | `text` contains `part` |
| `expect(throws<E>([] { ... }))`, `nothrow` | the body throws `E` (any exception for `throws<>`), or nothing |
| `expect(fatal(...))` | as above, and the case ends at a failure |

`<< "text"` adds a message, formed only when the expectation failed. `skip("why")` ends a case as
skipped, `fail("why")` as failed.

## Attributes

Written after an object's name, before its body, or in brackets after a case's name. A case has
the attributes of every scope that declares it: tags, clauses, resources and artifacts add up,
isolation holds for everything inside; the others are the innermost one stated.

| Attribute | States | The runner |
|---|---|---|
| `kind::e2e` | what is tested (unit, e2e, cts, bench) | selects and reports by kind |
| `tag{"slow"}` | a label | selects by it (`--tag`, sets) |
| `clause{"P3.1"}` | a specification clause the case covers | forms the conformance matrix |
| `resource{"serial:db"}`, `resource{"machine"}` | what it holds exclusively | never runs two holders of one `serial:` group, nor a `machine` case beside another, at once |
| `artifact{"file:data/x.txt"}` | what it reads | selects it when that changes |
| `timeout{std::chrono::seconds{90}}` | how long it may run | ends the process that runs it at that time |
| `isolate{}` | that it runs in a process of its own | starts one for it |
| `retries{2}` | that a failure may be retried | runs it again, alone; one that then passes is `flaky` |
| `diagnose{.env = {{"APP_LOG", "debug"}}}` | how to look at a failure | runs a failing case once more with that environment and keeps the output (`mtest explain`) |
| `at_most{"ready", 15}`, `at_least{"recall", 0.9}` | the limits of a metric it reports | the case fails when its metric passes a limit or is not reported; `mtest analyze trend` shows each metric beside its limits |
| `skip_if{cond, "why"}`, `expect_failure{"why"}`, `known_gap{"why", "YYYY-MM-DD"}` | its expected verdict | reports skip, xfail, xpass |
| `monitor{monitor_level::detailed}` | how closely its use of the machine is measured | measures at least that closely: its processor time (`basic`), its memory and I/O (`detailed`), both over time (`trace`), as metrics with their precision (protocol P10) |
| `uses{"index"}` | a run fixture it uses | serves the fixture once for the run before the first case that uses it, gives its endpoint (`fixture_endpoint("index")`), ends it after the last |

`Bench` holds `machine`: a measurement means something only on a machine nothing else uses.
How the tests of a project are laid out, from the directory to the line, is in
[docs/layout.md](../../docs/layout.md).

## Fixtures

- What cases share is a local of the body that declares them; it exists while one of them runs.
- `shared<T> server{"server", [] { return T{...}; }}` is made when a case first uses it (`*server`,
  `server->`), at most once, and destroyed with the body; the runner records which case made it and
  how long that took.
- `temp_dir()` is the case's own directory, removed when it ends; `seed()` the seed of its randomness
  (`mcpp test --seed N` repeats one); `produced(file)` reports a file it wrote.
- Parameters: `"is even"_test = values{2, 4, 6} | [](int v) { expect(eq(v % 2, 0)); };`
- Measurements: `import mcpplibs.mtest.bench;`, then `bench::measure("name", [] { ... });` in a
  `Bench`.

## Programs under test

`import mcpplibs.mtest.e2e;` A case of kind `e2e` starts the program it tests and judges what it did.

```cpp
auto r = e2e::run({.argv = {e2e::program("app"), "--version"}, .cwd = dir});
expect(e2e::succeeded(r));                    // started, in time, status 0
expect(e2e::matches(r.out, R"(app \d+\.\d+)"));
expect(e2e::contains(r.err, "warning"));
```

- `e2e::run` runs a command to its end or its time limit; the outcome carries its status, both
  outputs and `line`, the command as one POSIX shell line (`e2e::shell_line`). A failed
  `succeeded`, `exited(r, n)`, `contains` or `matches` reports the line that repeats it and the
  tails of both outputs, so a failure reads as a session a person can paste into a shell.
- `e2e::tree({{path, content}...})` writes a directory of files; `e2e::golden(file, text)` compares
  with a golden file (`MTEST_GOLDEN=update` writes it).
- **A session** drives a program whose input depends on what it wrote: a serial console, a
  debugger, a login, a read-eval-print loop.

  ```cpp
  auto s = e2e::session::start({.argv = {"qemu-system-x86_64", "-nographic", ...}});
  expect(fatal(s.saw("login: ", std::chrono::minutes{3})));
  s.send_line("root");
  auto prompt = s.expect(R"(# $)");
  ```

  `expect` consumes the output up to the end of the pattern's first match and returns it; nothing
  when the time ran out (`timed_out()`) or the output ended (`ended()`). `saw` is the same as a
  comparison whose failure carries the output not yet consumed. `transcript()` is everything
  written, with what was sent marked. Both streams travel in one pipe, in the order written; no
  terminal is involved, so a session behaves the same on every system.

## Conformance to a specification

`import mcpplibs.mtest.cts;` A conformance case is of kind `cts` (a `CTS` object, or `kind::cts`) and
names the clauses it covers; `cts::coverage({...})` fails while a clause has no case.

A suite is written once and run against each implementation, which may lack what the specification
leaves optional:

```cpp
void device_suite(Device& device) {
    "copies are exact"_test[clause{"R2.1"}] = [&] { ... };
    "indirect draws"_test[clause{"R4.3"}][cts::needs{"indirect"}] = [&] { ... };
}

cts::against({.name = "lavapipe",
              .has = [&](std::string_view capability) { return device.has(capability); },
              .required = {"compute"}},
             [&] { device_suite(device); });
```

- The cases are the parts of a case named after the **subject** (`lavapipe/copies are exact`), which
  carries the tag `subject:lavapipe`: one program runs the suite against several implementations, and
  each is selected and reported.
- A case that **needs** what the subject lacks is not run. Its status is `skip`, its reason
  `unsupported: indirect`. That is said from what the subject states it can do, never from a run that
  went wrong, and it is never a pass.
- What the run **requires** of the subject and it lacks fails, once: the case
  `lavapipe/requires compute`. A capability only some cases need leaves those unsupported.
- A subject that states no `has` is taken to have every capability; outside `against` a case that
  states needs runs. `cts::current_subject()` is the subject of the case that runs.

## Components and run fixtures

A part that many tests use is written once, in a library, and made known to the runner:

```cpp
// in a library the tests depend on
run_fixture index{"index", [](serving& s) {
    auto root = make_index();          // prepared once for the whole run
    s.ready(root.string());            // the endpoint the cases receive
    s.stopped();                       // returns when the runner ends the fixture
    remove(root);
}};
Component sandbox{"sandbox", {resource_pool{"port", 4}}};   // at most four holders of `port` at once

// in a test
E2E fetch{"fetch", uses{"index"}, resource{"port"}, [] {
    auto root = fixture_endpoint("index");
    ...
}};
```

- A **run fixture** is one object for the whole run, shared by the cases of every program that use it:
  a sandboxed home, a server, an index. The runner starts one program that declares it with
  `MTEST_SERVE`, gives its endpoint to every case that uses it, and ends it after the last; its
  serving is a node of the run's graph (`mtest graph --run last`).
- A **component** names the pools of resources it brings: a resource with a capacity, held by at
  most that many cases at once.
- What a case shares only with the cases of its own program is a `shared<T>` (above).

## Use of the machine

Under a runner each case reports its processor time (`cpu.user`, `cpu.sys`), and the program its own
use when it ends; `[monitor] level = "detailed"` (or `monitor{monitor_level::detailed}` on a case)
adds the case's largest resident set (`mem.peak`) and its I/O (`io.read`, `io.write`), and `trace`
samples the load and the resident set over the case (`cpu.load`, `mem.rss`). Each is a metric with
its precision, so a bound is written as for any metric: `at_most{"mem.peak", 64.0 * 1024 * 1024}`.
`mtest report --format md` shows them, the run's critical path and its schedule.

## Running

| Command | Does |
|---|---|
| `mcpp test` | every test program; a failed case is named with its place |
| `mcpp test parse` | the programs whose name contains `parse` |
| `mcpp test --case 'fields/*'` | the cases an identifier or a pattern names |
| `mcpp test --cases` | each program's cases, running none |
| `mcpp test --message-format json` | a record per program with each case's result, place and both sides |
| `mcpp test <test> --print-launch` | how a test starts, for a debugger |
| `mtest run --isolate --jobs N --shard I/N` | the cases scheduled by their resources and history, a part of them |

A failure reads `case fields/a field holds its text failed at tests/fields.cpp:9: ...`; the JSON
record has `expected` and `actual`. `mtest list --static` reads the cases a file declares, with
their kinds and attributes, without building it.

A program whose source cannot state its cases (a script, a program of another framework) states
them in `mtest.toml`'s `[cases."<glob>"]` with the same attributes ([docs/config.md](../../docs/config.md)).

More: [mtest](https://github.com/mcpp-community/mtest) — the library, the protocol
(`spec/protocol.md`), and the engine `mtest` for history, analysis and gates.
