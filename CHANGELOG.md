# Changelog

## [0.2.0] - 2026-11-08

- `[discover.<name>]`: every file a glob matches is a program run by a command (`{path}`,
  `{name}`), with the keys of `[programs.*]`; tags and resources are read from each file's first
  lines (`tags_from`, `resources_from`), so a directory of scripts is a suite without a line of
  configuration per script. `kind` states the kind of an L0 program's one case.
- `[cases."<glob>"]`, and a host's `[package.metadata.mtest.cases."<glob>"]` in mcpp.toml, state the
  tags, resources, kind and timeout of the cases a glob matches, taking precedence over what a
  program lists.
- `--exclude-program GLOB` leaves the programs a glob matches out of a selection; globs take character classes (`[0-9]`, `[!x]`).
- `--shard I/N` selects the I-th of N parts: each program, the longest first, to the part with the
  least expected time; every machine with the same store computes the same parts.
- The coverage map (`docs/coverage.md`): `mtest run --coverage` records, for each case, the sources
  its run executed, read from GCC's `.gcda` counters and a compile database; affected selection
  reads it, and `mtest coverage show|import` and `mtest store import` carry it between machines.
- The static case manifest: `mcpplibs.mtest.manifest` reads the cases a source file declares
  (`"name"_test`, their attributes, named suites, nested cases) from its text, and
  `mtest list --static` lists a project's test files with it, building and running nothing.
- A set that selects by change (`pr`) selects every case when the change cannot be read (a shallow clone
  without its base), and says so; before, it selected none of the affected cases.
- A failing case's entry in a run's answer names its kind.

## [0.1.0] - 2026-11-08

The first release.

- The mtest test protocol, version 1 (`spec/protocol.md`): levels L0–L3, the handshake, listing,
  selection, NDJSON reporting, metrics and resources; its conformance suite checks every
  implementation (`spec/conformance.md`).
- `mcpplibs.mtest`: the C++ library, with no macro and no header; `bench`, `cts` and `e2e`.
- The engine, `mtest`: run, schedule, store, explain, analyze and gate, on the command line, as a
  JSON-RPC service and as MCP tools; released as ok images for Linux, macOS and Windows.
- Adapters: GoogleTest (`mcpplibs.mtest.gtest`), Bash and Python; the C ABI (`mcpplibs.mtest.capi`).
