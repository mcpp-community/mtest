// The conformance fixture of mcpplibs.mtest (spec/conformance.md).
import std;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

suite group = {"group", [] { "inner"_test = [] {}; }};

int main() {
    "passes"_test[tag{"fast"}] = [] {};
    "fails"_test = [] { expect(eq(1, 2)); };
    "skips"_test = [] { skip("not here"); };
    "expected to fail"_test[expect_failure{"on purpose"}] = [] { expect(false); };
    "measures"_test[kind::bench] = [] {
        const double samples[] = {0.4, 0.5, 0.6};
        metric("elapsed", 0.5, "s", samples);
    };
    "covers"_test[kind::cts][clause{"P3.1"}] = [] {};
    "holds"_test[resource{"serial:db"}][artifact{"file:data/x.txt"}] = [] {};
    "bounded"_test[at_most{"elapsed", 0.1}] = [] { metric("elapsed", 0.5, "s"); };
    return report();
}
