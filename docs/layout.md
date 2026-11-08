# The layout of tests

A project's tests are in `tests/`. The directory below `tests/` names the kind of what it holds:

```
tests/
  unit/    units of the program on their own
  e2e/     the program as its users run it
  cts/     conformance to a specification (clauses, P6.4)
  bench/   measurements
  *.cpp    a file directly in tests/ is a unit test
```

The kind a case has is decided in this order:

1. what the case states: its object's type (`UTest`, `E2E`, `CTS`, `Bench`), or `kind::...` in its
   own attributes or a scope's;
2. what `mtest.toml` states for it: `[cases."<glob>"] kind`, `[programs.*] kind`, `[discover.*] kind`;
3. its program's directory, as above (`tests/e2e/t.sh` and a `Suite` in `tests/e2e/cli.cpp` are e2e);
4. unit.

The directory therefore matters for programs that state nothing — a script, a program of plain
`main` — and for `Suite`, which states no kind. A runner reads the same order: `mtest list`,
`mtest list --static` and `mtest run` give a case the same kind.

A file holds as many objects as it has cases worth controlling apart: each object is listed,
selected, scheduled and timed as one case, and the cases its body declares are selectable within it.
A program of many files (`[test] programs = "directory"` in mcpp) is one process for all of them;
`--isolate`, or `isolate{}` on a case, gives a case a process of its own.
