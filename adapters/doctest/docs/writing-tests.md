# Writing tests with doctest under mtest

A program's tests stay doctest's: `TEST_CASE`, `TEST_SUITE`, `CHECK`, `REQUIRE`, `SUBCASE`. The
package `mcpplibs.mtest.doctest` supplies doctest's implementation and the program's `main`, once:

```toml
[dev-dependencies]
mcpplibs.mtest.doctest = "0.11.3"
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
  `TEST_SUITE`; it is what `mcpp test --case` and `MTEST_SELECT` name.
- A case skipped by `doctest::skip()` is reported skipped, its `doctest::description` the reason.
- With no runner the program is doctest's own main and takes doctest's options.
