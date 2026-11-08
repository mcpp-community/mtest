# mtest.toml

Every key is optional. With no file the engine runs the programs it is named.

```toml
[project]
store = ".mtest"        # the store of runs; it ignores itself in version control
jobs  = 0               # 0: the machine's processors

# A program of any language. `level` states the protocol's level; without it the marker decides (P2.3).
[programs.e2e]
command   = ["python3", "tests/e2e.py"]
cwd       = "."
env       = { MODE = "ci" }
sources   = ["src/*", "tests/e2e.py"]   # a change of these affects it (affected selection)
resources = ["serial:db"]               # what it holds alone (P7.1)
tags      = ["slow"]
timeout   = "10m"
isolation = "process"                   # or "case": a process per case
kind      = "e2e"                       # the kind of an L0 program's one case (default unit)

[discover]
globs = ["build/tests/*"]               # every executable that matches is a program

# A group: every file the globs match is a program, run by the command ({path}: the file,
# absolute; {name}: its path from the root), with the keys of [programs.*]. Each word after a
# prefix on the file's first 64 lines is a tag (with the tag prefix given) or a resource.
[discover.e2e]
globs          = ["tests/e2e/[0-9]*.sh"]
command        = ["bash", "{path}"]
kind           = "e2e"
timeout        = "20m"
tags_from      = { "# requires:" = "requires:" }   # `# requires: gcc elf` -> requires:gcc, requires:elf
resources_from = ["# resources:"]                   # `# resources: serial:home`

[host]
kind = "mcpp"                           # the tests of `mcpp test` (mcpp 2026.11.8.1+)
args = ["--workspace"]

# What the cases a glob matches (`program::id` or `id`) are, stated beside the code; it takes
# precedence over what a program lists. A host states the same table in its own manifest:
# mcpp.toml's [package.metadata.mtest.cases."<glob>"].
[cases."parsing/*"]
tags      = ["fast"]
resources = ["serial:db"]
kind      = "unit"
timeout   = "30s"                       # an L0 program's

# The test files `mtest list --static` reads; with host.kind = "mcpp", mcpp.toml's [test] discover
# (else tests/**/*.cpp).
[static]
globs = ["tests/**/*.cpp"]

# The coverage map a run under `--coverage` measures (coverage.md).
[coverage]
objects  = "target/x86_64-linux-gnu/0123abcd/obj"  # the instrumented objects (.gcno, .gcda)
database = "target/x86_64-linux-gnu/0123abcd/compile_commands.json"
max_age  = "7d"                         # an older map is not read: every case is then affected

[sets.fast]                             # sets.md
tags    = ["fast"]
include = ["smoke"]
budget  = "2m"

[policy.bench]                          # the bench verdict (spec/protocol.md P6.2)
limit          = 1.15                   # the most a ratio to the baseline may be
spread         = 0.03                   # samples further than this from their median: invalid
repeat_invalid = true                   # an invalid measurement may be measured again
repeat_valid   = false                  # a valid one that failed may not
baseline       = "last-pass"            # or a run identifier

[retry]
flaky = 2                               # retries of cases tagged `flaky`, and of quarantined ones

[regression]
days = 30                               # cases that failed within this join the `regression` set

[store]
keep_days = 30
keep_runs = 200

[trace.asan]                            # a tool for `mtest trace --with asan`
build     = ["mcpp", "test", "--profile", "asan", "--no-run"]
run_under = []
env       = { ASAN_OPTIONS = "halt_on_error=1" }
```

A key the engine does not know is reported, not refused.
