// The `main` of a doctest program under the mtest protocol: the package's entry, which mcpp begins a
// test file that defines no `main` with (`test-main`).
int mcpplibs_mtest_doctest_main(int argc, char** argv);

int main(int argc, char** argv) {
    return mcpplibs_mtest_doctest_main(argc, argv);
}
