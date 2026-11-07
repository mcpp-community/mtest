// The `main` of a test program whose files declare suites (`[test] main = { from = "mtest/main"
// }`).
import mcpplibs.mtest;

int main(int argc, char** argv) {
    return mcpplibs::mtest::run(argc, argv);
}
