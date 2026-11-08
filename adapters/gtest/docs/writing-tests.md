# Writing tests with GoogleTest through mcpplibs.mtest.gtest

The guide `mcpp test --guide` names. The tests stay GoogleTest's; the adapter is their `main` and
reports each one by the mtest protocol, so `mcpp test` lists, selects and reports GoogleTest's cases.

```cpp
#include <gtest/gtest.h>
#include "mylib/parse.hpp"

TEST(Parsing, EmptyLine) { EXPECT_EQ(mylib::parse(""), 0); }
TEST(Parsing, Fields) {
    auto r = mylib::parse("a,b");
    ASSERT_EQ(r.size(), 2u);
    EXPECT_EQ(r[1], "b");
}
```

- The manifest: `[dev-dependencies] mcpplibs.mtest.gtest = "0.2.0"`. A file defines no `main` (mcpp
  2026.11.8.1 or later). An earlier mcpp needs `[test] main = { from = "mcpplibs.mtest.gtest/main" }`
  and `compat.gtest = "1.18.0"` beside it, so that GoogleTest's own `gtest_main.cc` stays out of the
  program.
- A case's identifier is `<suite>/<name>`: `Parsing/EmptyLine`.
- `mcpp test --case 'Parsing/*'` runs those cases; `mcpp test --cases` lists them; a failure is named
  with its place.

More: [mtest](https://github.com/mcpp-community/mtest), [GoogleTest](https://google.github.io/googletest/).
