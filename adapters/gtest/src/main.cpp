// The `main` of a GoogleTest program under the mtest protocol: `[test] main = { from = "gtest/main"
// }`.
int mcpplibs_mtest_gtest_main(int argc, char** argv);

int main(int argc, char** argv) {
    return mcpplibs_mtest_gtest_main(argc, argv);
}
