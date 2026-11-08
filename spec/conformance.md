# The conformance fixture

An implementation of the protocol's program side (a test library, an adapter, the C ABI) is checked by running a program that declares exactly these cases, in any order, and reading what it reports (conformance/). Identifiers are written as the implementation forms them from these names.

| Identifier | Body | Attributes |
|---|---|---|
| `passes` | nothing fails | tag `fast` |
| `fails` | one expectation fails at a known line, with expected `2` and actual `1` | |
| `skips` | the case skips itself, reason `not here` | |
| `expected to fail` | one expectation fails | expected to fail |
| `group/inner` | nothing fails | in a group named `group` |
| `measures` | one metric `elapsed`, unit `s`, value `0.5`, samples `[0.4, 0.5, 0.6]` | kind `bench` |
| `covers` | nothing fails | kind `cts`, clause `P3.1` |
| `holds` | nothing fails | resource `serial:db`, artifact `file:data/x.txt` |
| `bounded` | one metric `elapsed`, unit `s`, value `0.5` | at most `0.1` of `elapsed` (P6.5): the case fails |

An implementation below level 3 omits the attributes it cannot express and states its level in its `program` event; the suite skips the clauses above that level.
