# mtest.toml

Every key is optional. With no file the engine runs the programs it is named, and a program written
with the library states its cases, their kinds and their attributes in its own code
([lib/docs/writing-tests.md](../lib/docs/writing-tests.md)); that is the form a test is written in.
The file states what code cannot: programs of another language or framework, where the tests are,
sets, policies and the store. Its case attributes are the library's, in the same words.

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
kind      = "e2e"                       # the kind of its cases that state none (default: its directory's, else unit)
split     = true                        # whether [run] split may divide its cases among processes
skip_exit = 77                          # a program judged by its exit status: the status that says its case did not
                                        # apply here (skip, with the last `SKIP...` line of its output as the reason);
                                        # unset: every status but 0 fails

[discover]
globs = ["build/tests/*"]               # every executable that matches is a program

# A group: every file the globs match (`*`, `?`, `[0-9]`, `[!x]`) is a program, run by the command ({path}: the file,
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

# Case attributes: the cases a glob matches (`program::id` or `id`; an L0 program's one case is the
# program), for a program whose source states none. The words are the library's attributes. What the
# program states comes first: a kind, a time limit, retries, a diagnosis, a skip and an expected
# failure apply where it states none; tags, clauses, resources, artifacts and isolation add up. A
# host states the same table in its own manifest: mcpp.toml's [package.metadata.mtest.cases."<glob>"].
[cases."parsing/*"]
kind        = "unit"
tags        = ["fast"]
clauses     = ["S1"]
resources   = ["serial:db"]
artifacts   = ["file:tests/data"]
timeout     = "30s"
isolation   = "case"                    # a process of its own
retries     = 2                         # runs again, alone, after failing
diagnose    = { env = { APP_LOG = "debug" }, timeout = "60s" }   # once more after a failure, kept
skip        = "not on this host"        # recorded as skipped, not run
expect_fail = "a defect upstream"       # a failure is xfail, a pass xpass
at_most     = { ready = 15 }            # bounds on its metrics (P6.5); at_least likewise
uses        = ["index"]                 # the run fixtures it uses (P11.2)

# How the engine places work.
[run]
split  = "auto"                         # divide a program's cases among processes while workers idle; "off"
memory = "auto"                         # schedule within the machine's memory, by each program's measured
                                        # largest resident set; "off", or a size such as "16GiB"

# How closely use is measured (P10): each case's processor time and the program's use at `basic`; also
# each case's memory and I/O at `detailed`; also the load and memory over time at `trace`.
[monitor]
level  = "basic"                        # off, basic, detailed, trace
sample = "50ms"                         # trace: how often

# What `mtest report --format md`, `--md FILE` and `mtest ci annotate` write.
[report]
sections  = ["summary", "failures", "slowest", "metrics", "resources", "path", "gantt"]
                                        # also kinds, flaky, trend, graph
scope     = "run"                       # run, program:<glob>, kind:<kind>
top       = 10                          # rows of a ranked table
max_nodes = 60                          # nodes of a drawn graph: the critical path's, then the longest

# A run fixture a command serves (P11.3), for one no test program declares: it prints
# `mtest-ready <endpoint>` and ends when its input ends.
[fixtures.index]
command = ["python3", "tests/serve_index.py"]

# The test files `mtest list --static` reads; with host.kind = "mcpp", mcpp.toml's [test] discover
# (else tests/**/*.cpp).
[static]
globs = ["tests/**/*.cpp"]

# The coverage map a run under `--coverage` measures (coverage.md).
[coverage]
objects  = "target/x86_64-linux-gnu/0123abcd/obj"  # the instrumented objects (.gcno, .gcda)
database = "target/x86_64-linux-gnu/0123abcd/compile_commands.json"
max_age  = "7d"                         # an older map is not read: every case is then affected

# What this machine can do, in the words cases require (`# requires: gcc` with tags_from above, or a
# case's tag `requires:gcc`). Every test an entry states must hold; an entry that states none holds
# everywhere. A case that requires a word this machine lacks is skipped, with the reason
# `missing capability: <word> (<the test that failed>)`; a word no entry declares is an error, for no
# machine could answer it. `mtest capabilities` prints the answers; each run records them. A word a
# program answers itself (the C++ library's `capability`, declared in code with these tests) needs
# no entry: the program lists its answer and the reason of the skip, and the run records it too.
[capabilities]
tag      = "requires:"                  # the tag prefix whose words are capabilities (the default)
gcc      = { program = "g++" }          # found on PATH (a list: each)
elf      = { os = ["linux"] }           # the engine's system names: linux, macos, windows, ...
x86_64   = { arch = ["x86_64"] }
musl     = { path = ["${HOME}/.mcpp/registry/data/xpkgs/xim-x-musl-gcc/*/bin/*-gcc"] }  # some file matches
msvc     = { command = ["vswhere", "-latest"] }   # runs and exits 0 within 30 s
xmsvc    = { env = { CI_MSVC = "1" } }  # equals; "*": set and not empty
pack     = { all = ["elf", "patchelf"] }
no-msvc  = { os = ["windows"], none = ["msvc"] }

# `mtest ci coverage`: the tables it reads beside the runs collected from every job. A first column
# names a program (or program::case); `#` begins a comment.
[ci.coverage]
exceptions = "tests/coverage-exceptions.tsv"   # <program>\t<why no machine of CI can run it>
debt       = "tests/coverage-debt.tsv"         # <program>\t<what the machines lack>: skips everywhere
hosts      = "tests/host-requirements.tsv"     # <program>\t<hosts>\t<why>: must pass on each host
# A case that states its places (`only{platform::windows}`, `--only windows`) must also pass on each
# of them that a collected run was on, and some run must have been on one: no table names it.

# `mtest ci plan`: a selection divided among CI's hosts by each host's history.
[ci.plan]
hosts      = ["linux", "macos", "windows"]     # the names runs record (MTEST_HOST, else the system)
shard      = "15m"                             # the expected time of one shard
max_shards = 8
unknown    = "30s"                             # the expected time of a case a host never ran

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
