// Bounds on a case's metrics (P6.5): checked as expectations at the case's end. Each case below
// states the verdict it must have, so the program passes exactly when the bounds are judged as
// stated.
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

Bench startup{"startup", at_most{"ready", 15}, at_least{"recall", 0.9}, [] {
                  "within its bounds"_test = [] {
                      metric("ready", 3.5, "s");
                      metric("recall", 0.95, "1");
                  };
                  "over a bound"_test[expect_failure{"ready is over 15 s"}] = [] {
                      metric("ready", 16.2, "s");
                      metric("recall", 0.95, "1");
                  };
                  "under a bound"_test[expect_failure{"recall is under 0.9"}] = [] {
                      metric("ready", 1, "s");
                      metric("recall", 0.5, "1");
                  };
                  "a bounded metric it never reports"_test[expect_failure{"no metric ready"}] = [] {
                  };
              }};

UTest single{"a case with no cases is judged by its own metrics", at_most{"bytes", 1024},
             [] { metric("bytes", 512, "B"); }};

int main() {
    return report();
}
