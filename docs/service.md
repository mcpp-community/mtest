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
