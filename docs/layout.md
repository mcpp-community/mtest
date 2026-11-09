# The layout and the form of tests

A project written with mtest has one form for its tests, from the directory down to the line: the
directory says what kind a test is, a file what subject, an object what case and what it needs. A
person or an agent who knows the form reads any project's tests the same way, and a runner gets from
it, without configuration, what it schedules, records and analyses.

## The directory

```
tests/
  unit/       units of the program on their own                       UTest
  e2e/        the program as its users run it                         E2E
  cts/        conformance to a specification (clauses, P6.4)          CTS
  bench/      measurements                                            Bench
  *.cpp       a file directly in tests/ is a unit test
  <kind>/<data>/   what a kind's cases read: fixtures, golden files, tasks
```

The kind a case has is decided in this order:

1. what the case states: its object's type (`UTest`, `E2E`, `CTS`, `Bench`), or `kind::...` in its
   own attributes or a scope's;
2. what `mtest.toml` states for it: `[cases."<glob>"] kind`, `[programs.*] kind`, `[discover.*] kind`;
3. its program's directory, as above (`tests/e2e/t.sh` and a `Suite` in `tests/e2e/cli.cpp` are e2e);
4. unit.

## The project

A file is a program (mcpp's default), or the files of a directory are one (`[test] programs =
"directory"`). A kind whose cases need inputs from outside the build -- a payload, a server, a
toolchain -- is a member package of its own (`tests/cts/mcpp.toml`), whose program is built and
shipped with the tools it drives and run by `mtest run`. Nothing lists the cases: the program does.

## The code

```cpp
import mcpplibs.mtest;
using namespace mcpplibs::mtest;

UTest parse{"parse", [] {
    const auto text = sample();                       // shared by the cases below, made when one runs
    "an empty line"_test = [&] { expect(eq(parse_line(""), 0)); };
    "a field"_test[tag{"fields"}] = [&] { expect(eq(parse_line(text).size(), 2u)); };
}};

Bench startup{"startup", at_most{"ready", 15}, resource{"machine"}, [] {
    metric("ready", seconds_to_ready(), "s");
}};
```

- **A file is a subject; an object is a case.** One object per behaviour or scenario; the cases its
  body declares are its parts. The object's name is the case's identifier, stable across runs.
- **The type is the kind; what a case needs follows its name**: resources, a time limit, isolation,
  retries, a diagnosis, bounds on its metrics, tags, clauses. Scopes pass them down.
- **What cases share is a local of the body**, or `shared<T>` when only some cases use it.
- **No `main`**: mtest's entry begins the program. A file that starts copies of itself declares what
  a copy does (`child`) and starts one with `child_command`, so the files of a directory remain one
  program.
- **A project's own kind of case is a type of its own** (`class Fixture : public CTS { ... }`): it adds
  what every case of that kind has, and the static manifest reads it as its base's kind, in the file
  or in a module or header the project's tests share.
- **A body returns nothing**: a case ends at its end, at `return;`, at `skip()` or at `fail()`.

## The data

What a case reports is the protocol's: its expectations with places and both sides, its metrics
(`metric()`, `bench::measure()`) and their bounds, the files it produced (`produced()`), the shared
objects it caused to be made and how long they took. The runner adds the rest: each program's start,
duration and the resources it held (the timeline), retries, a diagnostic run's output. The store keeps
every run; a CI job takes the stores of main's last run, so a part of a suite is chosen by measured
time and a metric is compared with its history.

## The analysis

| Ask | Answer |
|---|---|
| `mtest explain` | where the last failure was, why, the command that repeats it, its diagnosis |
| `mtest analyze hot`, `durations`, `path` | the slowest cases, the critical path of a run |
| `mtest analyze flaky`, `failures` | cases that change their verdict, failures grouped by message |
| `mtest analyze trend` | each metric across runs beside its bounds, and the margin it leaves |
| `mtest analyze bench` | measurements against a baseline, by the policy |
| `mtest gate --set` | whether a set passes for a merge or a release |
| `mtest list --static` | the cases a project declares, read from its text, as the program lists them |
