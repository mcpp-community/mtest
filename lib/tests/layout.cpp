// What a person reads of a failing case: its line first, then each failure under it -- where it
// is, the statement, the two sides or that it did not hold, and whether the case went on. A child
// runs the cases, as a program that is not under a runner does, and its output is read.
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;

using namespace mcpplibs::mtest;

child failing{"failing", [](std::vector<std::string>) {
                  "values"_test = [] {
                      const std::string got = "second";
                      expect(eq(got, std::string("first")));
                      expect(got == "first");
                      expect(got.size() == 6u);
                  };
                  "stops"_test = [] {
                      expect(fatal(false));
                      expect(true);
                  };
                  return 0;
              }};

UTest layout{"layout", [] {
                 "a failure is shown under its case with the statement and both sides"_test = [] {
                     auto o = e2e::run({.argv = child_command("failing", {})});
                     expect(fatal(o.started));
                     const auto at = [&](std::string_view s) { return o.out.find(s); };
                     const auto header = at("FAIL values  (2 of 3 checks failed)");
                     expect(fatal(header != std::string::npos)) << o.out;
                     expect(at("expect(eq(got, std::string(\"first\")));") > header) << o.out;
                     expect(at("expected  \"first\"") > header) << o.out;
                     expect(at("actual    \"second\"") > header) << o.out;
                     expect(at("expect(got == \"first\");") > header) << o.out;
                     expect(at("did not hold") > header) << o.out;
                 };
                 "a fatal failure says the rest of its case did not run"_test = [] {
                     auto o = e2e::run({.argv = child_command("failing", {})});
                     expect(fatal(o.started));
                     const auto header = o.out.find("FAIL stops  (1 of 1 checks failed, stopped)");
                     expect(fatal(header != std::string::npos)) << o.out;
                     expect(o.out.find("fatal: the rest of the case did not run") > header)
                         << o.out;
                 };
             }};
