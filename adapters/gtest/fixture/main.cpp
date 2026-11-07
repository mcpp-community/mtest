// The conformance fixture of the GoogleTest adapter (spec/conformance.md), level 1: the cases
// GoogleTest can state, named `<suite>/<name>` (`conformance/passes`), with no tags, kinds or
// metrics. The main is the adapter's (src/main.cpp), linked into this program by its target.
#include <gtest/gtest.h>

TEST(conformance, passes) {}
TEST(conformance, fails) {
    EXPECT_EQ(1, 2);
}
TEST(conformance, skips) {
    GTEST_SKIP() << "not here";
}
TEST(group, inner) {}
TEST(conformance, measures) {}
TEST(conformance, covers) {}
TEST(conformance, holds) {}

int mcpplibs_mtest_gtest_main(int argc, char** argv);

int main(int argc, char** argv) {
    return mcpplibs_mtest_gtest_main(argc, argv);
}
