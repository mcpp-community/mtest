// What the library costs a program (mtest design record of mcpp, 2026-11-20 §3.4 and §5.12), as
// measurements with bounds: the run time of an expectation, and what the protocol adds to a case
// when a runner keeps each case's output. They hold the machine (Bench). A bound holds on every row
// of CI and names a regression of the library's order of magnitude: the protocol took 2 ms a case
// through 0.10.0 and takes 0.075 to 0.21 ms (a workstation, a hosted runner); an expectation takes
// 228 ns natively and 1,036 ns in an ok image.
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.bench;
import mcpplibs.mtest.e2e;
import mcpplibs.os;

using namespace mcpplibs::mtest;

namespace {
constexpr int kCases = 510;  // the size of the program measured in §3.4
}

// The program the second measurement runs: a copy of this one under the protocol, selecting the
// cases below, which exist only in that copy.
UTest many{"many", [] {
               if (mcpplibs::os::env::get("MTEST_COSTS_CHILD").value_or("") != "1")
                   return;
               for (int i = 0; i < kCases; ++i)
                   test(std::format("case {}", i)) = [] { expect(eq(1, 1)); };
           }};

Bench assertions{"an expectation's run time", at_most{"expectation_ns", 3000}, [] {
                     constexpr int n = 1'000'000;
                     const auto began = std::chrono::steady_clock::now();
                     for (int i = 0; i < n; ++i)
                         expect(eq(i, i));
                     const auto ns = std::chrono::duration<double, std::nano>(
                                         std::chrono::steady_clock::now() - began)
                                         .count();
                     metric("expectation_ns", ns / n, "ns");
                 }};

Bench protocol{"what the protocol adds to a case", at_most{"case_overhead_ms", 1.0}, [] {
                   const auto dir = temp_dir() / "costs";
                   std::filesystem::create_directories(dir);
                   const auto report = dir / "report.ndjson";
                   auto argv = self_command();
                   expect(fatal(!argv.empty()));
                   e2e::command c;
                   c.argv = argv;
                   c.env = {{"MTEST_COSTS_CHILD", "1"},
                            {"MTEST_PROTOCOL", "1"},
                            {"MTEST_MODE", "run"},
                            {"MTEST_REPORT", mcpplibs::os::fs::utf8(report)}};
                   std::string select = "many\n";
                   (void)mcpplibs::os::fs::write(dir / "select.txt", select);
                   c.env.emplace_back("MTEST_SELECT", mcpplibs::os::fs::utf8(dir / "select.txt"));
                   auto r = e2e::run(c);
                   expect(fatal(e2e::succeeded(r)));
                   const auto text = mcpplibs::os::fs::read(report).value_or("");
                   std::size_t ends = 0;
                   for (std::size_t at = text.find("\"e\":\"end\""); at != std::string::npos;
                        at = text.find("\"e\":\"end\"", at + 1))
                       ++ends;
                   expect(fatal(ge(ends, std::size_t{kCases}))) << "the copy ran its cases";
                   metric("case_overhead_ms", double(r.duration.count()) / kCases, "ms");
               }};
