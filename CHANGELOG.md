# Changelog

## [0.1.0] - 2026-11-08

The first release.

- The mtest test protocol, version 1 (`spec/protocol.md`): levels L0–L3, the handshake, listing,
  selection, NDJSON reporting, metrics and resources; its conformance suite checks every
  implementation (`spec/conformance.md`).
- `mcpplibs.mtest`: the C++ library, with no macro and no header; `bench`, `cts` and `e2e`.
- The engine, `mtest`: run, schedule, store, explain, analyze and gate, on the command line, as a
  JSON-RPC service and as MCP tools; released as ok images for Linux, macOS and Windows.
- Adapters: GoogleTest (`mcpplibs.mtest.gtest`), Bash and Python; the C ABI (`mcpplibs.mtest.capi`).
