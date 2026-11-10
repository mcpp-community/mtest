// Under a runner, a case states what it wrote and how its checks went (mcpp test spec O3, O8):
// each case's output as `output` events, a case's own apart from the cases it declares, and its
// `end` with the checks that held and failed. A child runs the cases with the protocol's variables
// set, as a runner would, and its report is read.
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;
import nlohmann.json;

using namespace mcpplibs::mtest;

child talker{"talker", [](std::vector<std::string>) {
                 "speaks"_test = [] {
                     std::println("said by speaks");
                     std::println(std::cerr, "warned by speaks");
                     expect(true);
                     expect(eq(1, 2));
                 };
                 "outer"_test = [] {
                     std::println("before inner");
                     "inner"_test = [] { std::println("said by inner"); };
                     std::println("after inner");
                 };
                 return 0;
             }};

namespace {
std::vector<nlohmann::json> run_talker(std::string& out) {
    const auto report = temp_dir() / "report.ndjson";
    auto o = e2e::run({.argv = child_command("talker", {}),
                       .env = {{"MTEST_PROTOCOL", "1"},
                               {"MTEST_MODE", "run"},
                               {"MTEST_REPORT", report.string()},
                               {"MTEST_TMPDIR", temp_dir().string()}}});
    out = o.out;
    std::vector<nlohmann::json> events;
    std::ifstream in(report);
    for (std::string line; std::getline(in, line);)
        if (auto e = nlohmann::json::parse(line, nullptr, false); e.is_object())
            events.push_back(std::move(e));
    return events;
}

std::string output_of(const std::vector<nlohmann::json>& events, std::string_view id,
                      std::string_view stream) {
    std::string text;
    for (const auto& e : events)
        if (e.value("e", "") == "output" && e.value("id", "") == id &&
            e.value("stream", "") == stream)
            text += e.value("text", "");
    return text;
}
}  // namespace

UTest output{"output", [] {
                 "a case states what it wrote, on each stream"_test = [] {
                     std::string out;
                     const auto events = run_talker(out);
                     expect(
                         eq(output_of(events, "speaks", "out"), std::string("said by speaks\n")));
                     expect(
                         eq(output_of(events, "speaks", "err"), std::string("warned by speaks\n")));
                     // A person at the terminal still reads it.
                     expect(contains(out, "said by speaks")) << out;
                 };
                 "a declared case's output is its own, and the declaring case keeps the rest"_test =
                     [] {
                         std::string out;
                         const auto events = run_talker(out);
                         expect(eq(output_of(events, "outer/inner", "out"),
                                   std::string("said by inner\n")));
                         expect(eq(output_of(events, "outer", "out"),
                                   std::string("before inner\nafter inner\n")));
                     };
                 "a case's end states the checks that held and failed"_test = [] {
                     std::string out;
                     const auto events = run_talker(out);
                     const nlohmann::json* end = nullptr;
                     for (const auto& e : events)
                         if (e.value("e", "") == "end" && e.value("id", "") == "speaks")
                             end = &e;
                     expect(fatal(end != nullptr));
                     expect(eq((*end)["checks"].value("held", -1), 1));
                     expect(eq((*end)["checks"].value("failed", -1), 1));
                 };
             }};
