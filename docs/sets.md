# Sets

A kind says what a case tests; a set says when it runs. Five sets are built in; a project declares more
under `[sets.<name>]` ([config.md](config.md)).

| Set | Holds | For |
|---|---|---|
| `smoke` | cases tagged `smoke`, within a budget (60 s) | every commit's first gate; an installation's check |
| `regression` | cases tagged `regression` or `regression:<issue>`, and those that failed within `[regression] days` | the gate of a pull request; the proof of a fix |
| `pr` | the cases a change affects, with `smoke` and `regression` | pull request CI |
| `full` | every case | main, nightly |
| `release` | every case, with the bench verdicts of the policy | the release gate (`mtest gate --set release`) |

A declared set selects by `tags`, `kinds`, `programs` and `cases` (globs), excludes by `exclude_tags`,
and `include`s other sets. A budget is reported when a run of the set exceeds it, naming its slowest
cases; `--budget` instead keeps a run within it.
