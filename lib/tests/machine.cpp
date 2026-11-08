// The use of the machine a case reports (P10, P6.6), and the components and run fixtures a program
// declares (P11): the program lists and runs itself again under a runner, and reads the events.
import std;
import nlohmann.json;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;
import mcpplibs.os;

using namespace mcpplibs::mtest;
using json = nlohmann::json;

namespace {

void busy(std::chrono::milliseconds d) {
    const auto end = std::chrono::steady_clock::now() + d;
    volatile std::uint64_t x = 0;
    while (std::chrono::steady_clock::now() < end)
        x = x + 1;
}

// What the copy started with --declared declares.
UTest worker{"worker", [] {
                 "spins"_test = [] { busy(std::chrono::milliseconds{120}); };
                 "holds memory"_test[monitor{monitor_level::detailed}] = [] {
                     std::vector<char> block(48u << 20, 1);
                     expect(eq(block[1000], char(1)));
                 };
             }};

run_fixture index{"index", [](serving& s) {
                      s.ready("endpoint-of-index");
                      s.stopped();
                  }};
Component sandbox{"sandbox", {resource_pool{"port", 3}}};

UTest user{"user", uses{"index"}, [] {
               if (fixture_endpoint("index").empty())
                   skip("no runner serves index here");
               expect(eq(fixture_endpoint("index"), std::string("from-runner")));
           }};

std::vector<json> events_of(std::string_view mode,
                            std::vector<std::pair<std::string, std::string>> env) {
    const auto report = temp_dir() / "report.ndjson";
    auto argv = self_command();
    argv.push_back("--declared");
    e2e::command c{.argv = argv};
    c.env = {{"MTEST_PROTOCOL", "1"},
             {"MTEST_MODE", std::string(mode)},
             {"MTEST_REPORT", report.string()}};
    for (auto& kv : env)
        c.env.push_back(std::move(kv));
    auto r = e2e::run(std::move(c));
    expect(fatal(r.started));
    std::vector<json> out;
    std::ifstream in(report);
    for (std::string line; std::getline(in, line);)
        if (!line.empty())
            out.push_back(json::parse(line));
    return out;
}

std::optional<json> metric(const std::vector<json>& events, std::string_view id,
                           std::string_view name) {
    for (const auto& e : events)
        if (e.value("e", "") == "metric" && e.value("id", "") == id && e.value("name", "") == name)
            return e;
    return std::nullopt;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--declared")
        return run(argc, argv);
    "basic: a case's processor time, and the program's own use"_test = [] {
        auto events =
            events_of("run", {{"MTEST_MONITOR", "basic"}, {"MTEST_FIXTURE_INDEX", "from-runner"}});
        auto cpu = metric(events, "worker/spins", "cpu.user");
        expect(fatal(cpu.has_value()));
        expect(ge((*cpu)["value"].get<double>(), 60.0)) << cpu->dump();
        expect(eq((*cpu)["precision"].get<std::string>(), std::string("exact")));
        expect(!metric(events, "worker/spins", "mem.peak").has_value())
            << "basic measures no memory per case";
        const bool usage =
            std::ranges::any_of(events, [](const json& e) { return e.value("e", "") == "usage"; });
        expect(usage) << "the program reports its own use at its end";
    };
    "detailed: a case's largest resident set"_test = [] {
        if (mcpplibs::os::host() != mcpplibs::os::platform::linux)
            skip("the per-case resident set is reset where the kernel allows (Linux)");
        auto events =
            events_of("run", {{"MTEST_MONITOR", "basic"}, {"MTEST_FIXTURE_INDEX", "from-runner"}});
        auto peak = metric(events, "worker/holds memory", "mem.peak");
        expect(fatal(peak.has_value())) << "the case asked for detail itself";
        expect(ge((*peak)["value"].get<double>(), 40.0 * 1024 * 1024));
        expect(eq((*peak)["precision"].get<std::string>(), std::string("exact")));
    };
    "trace: the load and the resident set over time"_test = [] {
        auto events = events_of("run", {{"MTEST_MONITOR", "trace"},
                                        {"MTEST_MONITOR_SAMPLE", "10"},
                                        {"MTEST_FIXTURE_INDEX", "from-runner"}});
        auto load = metric(events, "worker/spins", "cpu.load");
        expect(fatal(load.has_value()));
        expect(ge((*load)["samples"].size(), std::size_t(3)));
    };
    "a listing names the components, their pools and run fixtures"_test = [] {
        auto events = events_of("list", {});
        bool pool = false, fixture = false, uses = false;
        for (const auto& e : events) {
            if (e.value("e", "") == "component" && e.value("name", "") == "sandbox")
                pool = e["pools"][0].value("capacity", 0) == 3;
            if (e.value("e", "") == "component" && e.value("fixture", "") == "index")
                fixture = true;
            if (e.value("e", "") == "case" && e.value("id", "") == "user")
                uses = e.contains("uses") && e["uses"][0] == "index";
        }
        expect(pool);
        expect(fixture);
        expect(uses);
    };
    "a program serves its run fixture when asked"_test = [] {
        auto argv = self_command();
        argv.push_back("--declared");
        e2e::command c{.argv = argv, .input = ""};
        c.env = {{"MTEST_SERVE", "index"}};
        auto r = e2e::run(std::move(c));
        expect(eq(r.status, 0)) << r.err;
        expect(contains(r.out, "mtest-ready endpoint-of-index"));
    };
    return report();
}
