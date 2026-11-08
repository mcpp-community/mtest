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
| `--tag T`, `--kind K`, `--exclude-tag T` | by tag; by kind (`unit`, `e2e`, `cts`, `bench`) |
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
nothing; a built program's own listing is the authority (`lib/src/manifest.cppm`).

## Running

| Command | Does |
|---|---|
| `run` | runs the selection; `--jobs N`, `--seed N`, `--fail-fast`, `--retries N`, `--isolate` (a process per case), `--timeout 5m`, `--env K=V`, `--run-under CMD`; `--junit`, `--html`, `--report-json FILE`; `--coverage` measures the coverage map |
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
| `report [--format text\|json\|junit\|html]` | a stored run |
| `diff [--before ID] [--after ID]` | newly failing, fixed, slower, faster |
| `history --case REF` | one case across the stored runs |
| `query` | stored case results by `--status`, `--kind`, `--program`, `--case`, `--tag`, `--days`, `--runs` |
| `analyze METRIC` | `hot`, `durations`, `path` (the critical path), `failures` (grouped by message), `flaky`, `bench`, `cost` |
| `graph [--format json\|dot\|mermaid]` | sources → programs → cases → resources and artifacts |
| `gate [--set S]` | a set's gate on a run: no failure, the budget kept, bench verdicts that pass |

A run is named by its identifier, `last`, or `last-N`.

## Keeping

| Command | Does |
|---|---|
| `quarantine list\|add\|remove --case REF [--until DATE] [--reason TEXT]` | cases whose failures do not fail a run, each until a date (14 days by default) |
| `store gc\|verify\|import --from DIR` | the store's retention, its consistency, another store taken in (a CI artifact), with its coverage map when that is newer |
| `coverage show\|import --from FILE` | the coverage map affected selection reads, or another taken in |
| `ci status OWNER/REPO#N...` | the checks of pull requests, failing ones with their links (by the GitHub CLI) |
| `ci annotate`, `ci collect --from DIR` | a run's failures as GitHub annotations and a job summary; a CI store taken in |
