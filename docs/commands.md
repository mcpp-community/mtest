# Commands

Every command is one function of the engine, reached three ways with the same parameters and the same
answer: the command line, the JSON-RPC service, and the MCP tools ([service.md](service.md)). `--json`
prints the answer's data; every answer carries a `conclusion`, one sentence a person reads and an agent
acts on. Exit status: `0` done, `1` a test or a gate failed, `2` a usage error, `3` the machine's failure
(a program that did not start), never a test's. The options are the parameters of the MCP tools, with
`-` for `_`; `mtest help` and `mtest COMMAND --help` list them.

## Selection

`run`, `plan`, `affected`, `list`, `graph` and `trace` take one selection:

| Option | Selects |
|---|---|
| `PROGRAM...`, `-- PROGRAM ARGS...` | these programs, instead of mtest.toml's |
| `--program GLOB` | the programs whose name matches |
| `--case GLOB` | cases by identifier, or `program::identifier`; `*` and `?` |
| `--tag T`, `--kind K`, `--exclude-tag T` | by tag (one of those given; `a&b`: both); by kind (`unit`, `e2e`, `cts`, `bench`) |
| `--exclude-program GLOB` | the programs whose name matches are left out |
| `--set S` | a set ([sets.md](sets.md)) |
| `--since COMMIT`, `--changed PATH` | the cases a change affects: a case's `file:` artifacts, its program's `sources` |
| `--budget 60s` | the most useful cases that fit: those that failed lately, then the quickest |
| `--shard I/N` | the I-th of N parts: each program, the longest first, to the part with the least expected time, the same on every machine with the same store |

A change affects a case whose last measured run executed a changed file (the coverage map,
[coverage.md](coverage.md)), whose `file:` artifacts it touches, or whose program's `sources` it
touches; a case that states nothing it reads, and that the map does not know, is affected.

`plan` says why each case is in the selection, and runs nothing. `list --static` reads the test
files `[static] globs` names (a host's own tests by default) and gives the cases each declares with
this library, their places, kinds and tags and the attributes of `[cases]`, building and running
nothing; a built program's own listing is the authority (`manifest/src/manifest.cppm`, the package `mcpplibs.mtest.manifest`).

## Running

| Command | Does |
|---|---|
| `run` | runs the selection; `--jobs N`, `--seed N`, `--fail-fast`, `--retries N`, `--isolate` (a process per case), `--timeout 5m`, `--env K=V`, `--run-under CMD` (every start of a program, its listing included: a runner such as an emulator or a machine of the program's system), `--monitor LEVEL` (P10), `--group G` (runs reported together); `--junit`, `--html`, `--report-json FILE`, `--md FILE`; `--coverage` measures the coverage map. A program's cases are divided among workers that would otherwise idle (`[run] split`), and programs run together within the machine's memory (`[run] memory`) |
| `run --resume RUN` | the selection, less the cases RUN judged: their results are carried into the new run, so an interrupted long run (a bench over a matrix) ends as one whole run; a case RUN could not start runs again |
| `rerun` | the cases that failed in the last run |
| `smoke` | the `smoke` set |
| `golden` | a run with `MTEST_GOLDEN=update`: golden files are written, not compared |
| `trace --with TOOL` | a run under `valgrind`, `strace`, `ltrace`, `perf`, `rr`, `time`, `okrun-prof`, or a `[trace.*]` tool; its output is kept with the run |
| `bisect --case REF --good C [--bad C] [--build CMD]` | the first commit at which a case fails (git bisect, one case per step) |
| `status`, `cancel` | runs in progress; stop one after the programs now running |

A program runs by the protocol when it carries the marker or declares its level; otherwise its exit
status is its one result. Cases tagged `flaky`, and quarantined ones, are retried alone up to
`--retries`; one that then passes is `flaky`. A quarantined case's failure does not fail the run.

## Asking

| Command | Answers |
|---|---|
| `explain [--case REF]` | where a case failed, why (both sides), its output, its recent history, whether it is new, and the command that repeats it |
| `report [--format text\|json\|junit\|html\|md]` | a stored run; `md`: a Markdown report of the sections `--sections` (or `[report]`) names -- summary, kinds, failures, slowest, metrics, resources, flaky, trend, path, graph, gantt -- over `--scope` (`run`, `program:<glob>`, `kind:<kind>`), of one run, `--runs a,b`, or every run of `--group G` together |
| `diff [--before ID] [--after ID]` | newly failing, fixed, slower, faster |
| `history --case REF` | one case across the stored runs |
| `query` | stored case results by `--status`, `--kind`, `--program`, `--case`, `--tag`, `--days`, `--runs` |
| `analyze METRIC` | `hot`, `durations`, `path` (the critical path: the longest weighted path of the run's graph, spec P12.3, with what each step waited for), `failures` (grouped by message), `flaky`, `bench`, `trend` (each case's metrics over the last `--runs N` runs beside their bounds, `--case GLOB`), `cost` |
| `graph [--format json\|dot\|mermaid\|gantt]` | sources → programs → cases → resources and artifacts; with `--run ID`, the run's graph of work -- builds, processes, run fixtures and what each waited for (worker, resource, build, fixture) -- the critical path marked, or its schedule as a gantt |
| `gate [--set S]` | a set's gate on a run: no failure, the budget kept, bench verdicts that pass |

A run is named by its identifier, `last`, or `last-N`.

## Keeping

| Command | Does |
|---|---|
| `quarantine list\|add\|remove --case REF [--until DATE] [--reason TEXT]` | cases whose failures do not fail a run, each until a date (14 days by default) |
| `store gc\|verify\|import --from DIR` | the store's retention, its consistency, another store taken in (a CI artifact), with its coverage map when that is newer |
| `coverage show\|import --from FILE` | the coverage map affected selection reads, or another taken in |
| `ci status OWNER/REPO#N...` | the checks of pull requests, failing ones with their links (by the GitHub CLI) |
| `ci annotate`, `ci collect --from DIR` | a run's failures as GitHub annotations and its Markdown report as the job's summary; a CI store taken in |
| `ci report [--group G]` | every collected run of a group (default: `GITHUB_RUN_ID`) as one Markdown report, in the job's summary and `--out` |
| `ci timeline --repo OWNER/REPO --run-id N` | a workflow run's jobs as a schedule with the critical path (each job waited for the job that ended last before it began), as Markdown with a gantt; `--summary` writes it to the job's summary |
| `ci coverage [--group G]` | every program of mtest.toml judged against the collected runs of a group (default `GITHUB_RUN_ID`; none: every stored run): `ran` on some host, `unaffected` (a narrowed run left it out), `excused` or `owed` (`[ci.coverage]`), else `uncovered`, which fails, as does a listed entry that ran or names no program, and a program that did not pass on a host `[ci.coverage] hosts` requires; the capability words no run's machine provided are listed |
| `ci plan [--hosts H]... [--format github]` | the selection divided among the hosts (`[ci.plan]`) by each host's history: the cases its capability answers admit, their measured time there, and the shards that fit `[ci.plan] shard`; `github` writes `<host>=[1,...,N]` and `<host>_work=true\|false` to `GITHUB_OUTPUT`, for a job matrix |
| `capabilities` | how this machine answers each word of `[capabilities]`, and why |
| `measure --out FILE -- ARGV` | runs ARGV and writes what it used (its children's account) as JSON: how the engine measures a program that reports no use of its own (P10.4) |
