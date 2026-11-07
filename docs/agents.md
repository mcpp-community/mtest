# Working with an agent

The engine's answers are data with a `conclusion`; an agent reads the conclusion and acts on the fields,
through `--json` or the MCP tools ([service.md](service.md)). Four tasks, as command sequences:

**A failing pull request.** `mtest ci status OWNER/REPO#N` names the failing checks. In the run's store
(`mtest ci collect --from DIR`), `mtest explain` gives the place, both sides, the output and the command
that repeats it; `mtest analyze failures` groups the failures by message.

**A fix and its proof.** Tag the case that guards the defect `regression:<issue>`. `mtest run --case REF`
repeats it; `mtest history --case REF` shows it failing before and passing after.

**A slower test.** `mtest diff` names the cases that became slower; `mtest analyze hot` and
`mtest analyze path` say where the time went; `mtest trace --with perf --case REF` records the case.

**A release gate.** `mtest run --set release`, then `mtest gate --set release`: the bench verdicts say
`pass`, `fail` or `invalid`, and whether an invalid one may be measured again.
