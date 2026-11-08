# The coverage map

Affected selection asks which cases a change can reach. A program's `sources` and a case's `file:`
artifacts answer it where a program states them. An end-to-end case states neither: it runs a
program built from most of a repository, and without more knowledge every change affects it. The
coverage map is that knowledge, measured: for each case, the source files its run executed.

## Measuring it

The programs under test are built with GCC's instrumentation (`--coverage`, or `-fprofile-arcs`),
and `mtest.toml` names their objects and a compile database that maps each object to its source:

```toml
[coverage]
objects  = "build/obj"
database = "build/compile_commands.json"
max_age  = "7d"
```

`mtest run --coverage` gives each program of the run a directory of its own (`GCOV_PREFIX`, with
`GCOV_PREFIX_STRIP` set so that each `.gcda` file lies at its object's path below it); the files a
program's run wrote record which of its objects ran. A `.gcda` file records a counter that ran when
one of its arc records holds a non-zero count; GCC 12 and later write a run of zero counters as a
negative length. Each object that ran names its source through the database, and the map is written
to the store:

```json
{"format": "mtest-coverage/1", "commit": "<sha>", "time": "<ISO 8601>", "epoch": 1794100000,
 "cases": {"<program>::<case>": ["src/a.cpp", "src/b.cpp"]}}
```

The cases of one program share its directory, so the map is per program; `--isolate` measures each
case in its own process and directory.

## Reading it

`--changed`, `--since` and the `pr` set read the store's map when it is no older than `max_age`. A
case the map knows is affected when the change touches a file its run executed, a `file:` artifact,
or a source of its program; a case the map does not know is decided as before, and one that states
nothing it reads is affected. A map older than `max_age` is not read, so a stale map widens the
selection and never narrows it.

## Sharing it

A map is measured where the instrumented build runs, on a schedule of the project's choosing (a
nightly run of the `full` set), and travels as a file: `mtest coverage import --from FILE` takes one
in, `mtest store import --from DIR` takes a store's when it is newer, and `mtest coverage show`
prints what the store holds and whether it is read.
