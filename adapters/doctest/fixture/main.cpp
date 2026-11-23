// The conformance fixture of the doctest adapter (spec/conformance.md), level 1: the cases doctest
// can state, named `<suite>/<name>` (`conformance/passes`), with no tags, kinds or metrics; the
// expected failure is doctest's `should_fail`.
// doctest's implementation and the main are the adapter's (src/protocol.cpp, src/main.cpp).
#include <doctest/doctest.h>

TEST_SUITE("conformance") {
    TEST_CASE("passes") {}
    TEST_CASE("fails") {
        CHECK(1 == 2);
    }
    TEST_CASE("skips" * doctest::skip() * doctest::description("not here")) {}
    TEST_CASE("expected to fail" * doctest::should_fail()) {
        CHECK(1 == 2);
    }
    TEST_CASE("measures") {}
    TEST_CASE("covers") {}
    TEST_CASE("holds") {}
}
TEST_SUITE("group") {
    TEST_CASE("inner") {}
}

int mcpplibs_mtest_doctest_main(int argc, char** argv);

int main(int argc, char** argv) {
    return mcpplibs_mtest_doctest_main(argc, argv);
}
