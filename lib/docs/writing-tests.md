# Writing tests with mcpplibs.mtest

The guide `mcpp test --guide` names. A test is a file under `tests/`; each file is one program, and
its cases are what `mcpp test` lists, selects and reports.

## A file of cases

```cpp
import std;
import mcpplibs.mtest;
import mylib;                         // what the cases test

using namespace mcpplibs::mtest;

suite parsing{"parsing", [] {
    "an empty line is no record"_test = [] {
        expect(eq(mylib::parse(""), 0));
    };
    "a field holds its text"_test[tag{"fields"}] = [] {
        auto r = mylib::parse("a,b");
        expect(fatal(r.size() == 2)) << "two fields";   // ends the case when it fails
        expect(eq(r[1], std::string("b")));
    };
}};
```

- A file declares suites and defines no `main` (with mcpp 2026.11.8.1 or later; an earlier mcpp
  needs `[test] main = { from = "mtest/main" }`). A file that defines `main` declares its cases in it
  and returns `report()`.
- The manifest names the library once: `[dev-dependencies] mcpplibs.mtest = "0.2.0"`.
- A case's identifier is `<suite>/<name>`; it is stable, so a selection and a history name it.

## Expectations

| Write | Holds when |
|---|---|
| `expect(cond)` | `cond` is true |
| `expect(eq(a, b))`, `ne`, `lt`, `le`, `gt`, `ge` | the comparison holds; a failure reports both sides |
| `expect(near(a, b, tol))` | `|a - b| <= tol` |
| `expect(contains(text, part))` | `text` contains `part` |
| `expect(throws<E>([] { ... }))`, `nothrow` | the body throws `E` (any exception for `throws<>`), or nothing |
| `expect(fatal(...))` | as above, and the case ends at a failure |

`<< "text"` adds a message, formed only when the expectation failed. `skip("why")` ends a case as
skipped, `fail("why")` as failed.

## Attributes, fixtures, values

- In brackets after the name: `[tag{"slow"}]`, `[kind::e2e]`, `[expect_failure{}]`,
  `[skip_if{cond, "why"}]`, `[resource{"port:8080"}]` (cases that share one do not run at once),
  `[clause{"P3.1"}]` (a conformance clause).
- `temp_dir()` is the case's own directory, removed when it ends; `seed()` the seed of its randomness
  (`mcpp test --seed N` repeats one); `produced(file)` reports a file it wrote.
- Parameters: `"is even"_test = values{2, 4, 6} | [](int v) { expect(eq(v % 2, 0)); };`
- Measurements: `import mcpplibs.mtest.bench;`, then `bench::measure("name", [] { ... });`
  in a case of `kind::bench`.

## Running

| Command | Does |
|---|---|
| `mcpp test` | every test program; a failed case is named with its place |
| `mcpp test parse` | the programs whose name contains `parse` |
| `mcpp test --case 'parsing/*'` | the cases an identifier or a pattern names |
| `mcpp test --cases` | each program's cases, running none |
| `mcpp test --message-format json` | a record per program with each case's result, place and both sides |
| `mcpp test <test> --print-launch` | how a test starts, for a debugger |

A failure reads `case parsing/a field holds its text failed at tests/parsing.cpp:12: ...`; the JSON
record has `expected` and `actual`.

Attributes stated beside the code, `[package.metadata.mtest.cases."<glob>"]` in mcpp.toml (`tags`,
`resources`, `kind`, `timeout`), apply to the cases a glob matches and take precedence over what the
program lists. `mtest list --static` reads the cases a file declares without building it.

More: [mtest](https://github.com/mcpp-community/mtest) — the library, the protocol
(`spec/protocol.md`), and the engine `mtest` for history, analysis and gates.
