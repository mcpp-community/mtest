# The service

`mtest serve` speaks on its standard streams, one JSON message per line, two protocols at once:

- **JSON-RPC 2.0.** A request's method is a command ([commands.md](commands.md)) and its params the
  command's parameters; the result is `{ "status": N, "data": {...} }`. While a run runs, its events are
  notifications: `{ "method": "mtest/event", "params": { "event": "run/started" | "case/finished" |
  "program/finished" | "run/finished", ... } }`.
- **The Model Context Protocol.** `initialize`, `tools/list` and `tools/call`: the tools are the
  commands, each with the JSON Schema of its parameters and whether it changes anything
  (`readOnlyHint`). A call's result has the text and the data (`structuredContent`).

Each request runs on its own thread, so `status` and `cancel` are answered while a run runs. An MCP
client is configured with the command `mtest serve` in the project's directory.

## The test session (0.6.0)

`mtest serve` is also the test session of the mcpp test spec (mcpp `docs/specs/test.md`, O9): a client
that holds it runs an mcpp project's cases as a person asks for them, adds cases to a run in progress
and cancels a run, a program or a case. The library's entry states it (`test-session = ["mtest",
"serve"]`), and `mcpp test --guide` reports it.

| Method | Params | Result |
|---|---|---|
| `test/plan` | `project`, optional `filter`, `options` | `programs`: the records of `mcpp test --cases` |
| `test/run` | `project`, `selection` (`[{program, cases}]`; empty: every program), `options` | `run`, at once |
| `test/add` | `run`, `selection` | `run`: the cases join the run in progress |
| `test/cancel` | `run`, optional `program` and `case` | what was cancelled |
| `test/status` | optional `run` | each run's status, counts and cases |
| `shutdown` | | every run cancelled |

`options`: `mcpp` (a program, or a program and its first arguments; else `MTEST_MCPP`, else `mcpp`),
`args` (more arguments of `mcpp test`: `--profile`, `--target`), `isolation` (`case`: a process per
case).

- **Notifications.** `test/build` (`begin`, then `end` with the programs built), `test/case` (`begin`,
  `output`, `failure`, `end`, with the fields of the program's events), `test/program` (a process's
  status, exit and time) and `test/run` (the run's status and counts).
- **Builds are mcpp's.** The session asks mcpp for the programs it runs (`mcpp test --no-run
  --message-format json`, mcpp 2026.11.10.1 or later), which builds them when they need it, and starts
  each from the launch the record carries. One build runs at a time; the processes share the
  session's workers.
- **Cancelling** ends the process and everything it started. A case that had begun ends `cancelled`;
  the cases its process was to run and that had not begun run in another process, unless the run or
  the program was cancelled. A run of cancelled cases and no failure ends `cancelled`.
- **The end of the input** lets the runs in progress end; `shutdown` cancels them.
