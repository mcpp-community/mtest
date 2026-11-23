# Writing tests with doctest under mtest

A program's tests stay doctest's: `TEST_CASE`, `TEST_SUITE`, `CHECK`, `REQUIRE`, `SUBCASE`. The
package `mcpplibs.mtest.doctest` supplies doctest's implementation and the program's `main`, once:

```toml
[dev-dependencies]
mcpplibs.mtest.doctest = "0.11.4"
```

```cpp
#include <doctest/doctest.h>

TEST_SUITE("parse") {
    TEST_CASE("an empty line") { CHECK(parse_line("") == 0); }
}
```

- A test file includes the header and defines neither `DOCTEST_CONFIG_IMPLEMENT` nor
  `DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN`: the implementation is compiled once, in the package, and not
  in each file.
- A case's identifier is `<suite>/<name>` (`parse/an empty line`), or `<name>` outside a
  `TEST_SUITE`; it is what `mcpp test --case` and `MTEST_SELECT` name. Identifiers are distinct in a
  program: of two cases with one name, in doctest's order (file, then line), the later is
  `<suite>/<name>#2`, a third `#3`; the case's `name` stays doctest's.
- A case skipped by `doctest::skip()` is reported skipped, its `doctest::description` the reason.
- A case decorated `doctest::should_fail()` or `doctest::expected_failures(n)` is listed as expected
  to fail: it ends `xfail` when it fails (`expected_failures`: exactly `n` times), and `xpass`, which
  fails the run, when nothing fails. A case it fails otherwise (`expected_failures`: another number
  of times) fails. `doctest::may_fail()` states no expectation: the case passes either way.
- `doctest::timeout(s)` is listed as the case's `timeout`: doctest judges it only when the case
  returns, and a runner that starts a process per case ends the case at it.
- A case that throws or crashes fails at its own place, the `TEST_CASE` line.
- With no runner the program is doctest's own main and takes doctest's options. Under a runner
  (`MTEST_PROTOCOL`), doctest's queries (`--list-test-cases`) report through the protocol: the names
  go to the report, not to standard output.
- The package's `main` is linked into a test file whose text defines none. A file whose `main` comes
  from a header (`<boost/test/included/unit_test.hpp>`) declares it, `int main(int, char*[]);`, so
  that it is not linked twice.
