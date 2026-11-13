// Where a case runs and what the machine can do (0.8.0): `only` and `except` skip a case elsewhere
// with one reason, a suite's and its case's narrow together, and a capability declared in code is
// answered by the program, once, for whichever runner lists and runs it. The program runs itself
// again under a runner with the cases declared, and reads the events.
import std;
import nlohmann.json;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;
import mcpplibs.os;

using namespace mcpplibs::mtest;
using json = nlohmann::json;

namespace {

// What a machine has or lacks, declared by the vocabulary of [capabilities].
const capability always{"always", {}};
const capability never{"never", {.env = {{"MTEST_PLACES_NEVER_SET", "*"}}}};
const capability on_top_of_never{"on-top-of-never", {.all = {never}}};
const capability without_never{"without-never", {.none = {never}}};
const capability refused{"refused", {.test = [] { return false; }}};

// The questions of mcpplibs.os, answered at compile time where the build has one platform.
static_assert(can_run_on(platform::linux) || can_run_on(platform::macos) ||
              can_run_on(platform::windows) || can_run_on(platform::emscripten));

const std::string this_host =
    std::format("{} {}", mcpplibs::os::name(host()), mcpplibs::os::name(mcpplibs::os::ARCH));
constexpr architecture other_architecture =
    mcpplibs::os::ARCH == architecture::x86_64 ? architecture::aarch64 : architecture::x86_64;

// What the copy started with --declared declares.
void declare_cases() {
    "here"_test[only{host()}] = [] {};
    "elsewhere"_test[except{host()}] = [] {
        expect(false) << "never runs on its excepted platform";
    };
    "on another architecture"_test[only{other_architecture}] = [] { expect(false); };
    "on this architecture, any platform"_test[only{mcpplibs::os::ARCH}] = [] {};
    Suite narrowed{"narrowed",
                   only{platform::linux, platform::macos, platform::windows, platform::openkal},
                   [] {
                       "to this host"_test[only{host()}] = [] {};
                       "to a platform its suite excludes"_test[only{platform::emscripten}] = [] {
                           expect(false);
                       };
                   }};
    (void)narrowed;
    "with always"_test[always] = [] {};
    "with never"_test[never] = [] { expect(false) << "never runs where `never` is absent"; };
    "on top of never"_test[on_top_of_never] = [] { expect(false); };
    "without never"_test[without_never] = [] {};
    "refused by its test"_test[refused] = [] { expect(false); };
}

std::vector<json> events_of(std::string_view mode) {
    const auto report = temp_dir() / std::format("{}.ndjson", mode);
    auto argv = self_command();
    argv.push_back("--declared");
    e2e::command c{.argv = argv};
    c.env = {{"MTEST_PROTOCOL", "1"},
             {"MTEST_MODE", std::string(mode)},
             {"MTEST_REPORT", report.string()},
             {"MTEST_PLACES_NEVER_SET", std::nullopt}};
    auto r = e2e::run(std::move(c));
    expect(fatal(r.started));
    std::vector<json> out;
    std::ifstream in(report);
    for (std::string line; std::getline(in, line);)
        if (!line.empty())
            out.push_back(json::parse(line));
    return out;
}

std::optional<json> event(const std::vector<json>& events, std::string_view kind,
                          std::string_view id) {
    for (const auto& e : events)
        if (e.value("e", "") == kind && e.value("id", "") == id)
            return e;
    return std::nullopt;
}

std::string status(const std::vector<json>& events, std::string_view id) {
    auto e = event(events, "end", id);
    return e ? e->value("status", "") : "(no end)";
}

std::string reason(const std::vector<json>& events, std::string_view id) {
    auto e = event(events, "end", id);
    return e ? e->value("reason", "") : "(no end)";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--declared") {
        declare_cases();
        return run(argc, argv);
    }
    "a case runs where its places hold and is skipped elsewhere, saying where"_test = [] {
        const auto events = events_of("run");
        expect(eq(status(events, "here"), std::string("pass")));
        expect(eq(status(events, "on this architecture, any platform"), std::string("pass")));
        expect(eq(status(events, "elsewhere"), std::string("skip")));
        expect(eq(reason(events, "elsewhere"),
                  std::format("not on {} (this host: {})", mcpplibs::os::name(host()), this_host)));
        expect(eq(reason(events, "on another architecture"),
                  std::format("only on {} (this host: {})", mcpplibs::os::name(other_architecture),
                              this_host)));
    };
    "a suite's places and its case's narrow together"_test = [] {
        const auto events = events_of("run");
        expect(eq(status(events, "narrowed/to this host"), std::string("pass")));
        expect(
            eq(status(events, "narrowed/to a platform its suite excludes"), std::string("skip")));
        expect(contains(reason(events, "narrowed/to a platform its suite excludes"),
                        "its `only` statements exclude one another"));
    };
    "a capability is answered by its tests, and one absent skips naming the test"_test = [] {
        const auto events = events_of("run");
        expect(eq(status(events, "with always"), std::string("pass")));
        expect(eq(status(events, "without never"), std::string("pass")));
        expect(eq(reason(events, "with never"),
                  std::string("missing capability: never (MTEST_PLACES_NEVER_SET is unset)")));
        expect(eq(reason(events, "on top of never"),
                  std::string("missing capability: on-top-of-never (`never` is absent "
                              "(MTEST_PLACES_NEVER_SET is unset))")));
        expect(eq(reason(events, "refused by its test"),
                  std::string("missing capability: refused (its test does not hold)")));
    };
    "a listing states the places, the answers and the reason of a skip"_test = [] {
        const auto events = events_of("list");
        auto here = event(events, "case", "here");
        expect(fatal(here.has_value()));
        expect(eq(here->value("only", json::array()), json::array({mcpplibs::os::name(host())})));
        expect(!here->contains("expect"));
        auto elsewhere = event(events, "case", "elsewhere");
        expect(fatal(elsewhere.has_value()));
        expect(eq(elsewhere->value("except", json::array()),
                  json::array({mcpplibs::os::name(host())})));
        expect(eq(elsewhere->value("expect", ""), std::string("skip")));
        expect(contains(elsewhere->value("reason", ""), "not on"));
        auto never_case = event(events, "case", "with never");
        expect(fatal(never_case.has_value()));
        expect(eq(never_case->value("capabilities", json::object()), json{{"never", false}}));
        expect(std::ranges::any_of(never_case->value("tags", json::array()), [](const json& t) {
            return t.get<std::string>() == "requires:never";
        })) << "a required capability is also its tag";
        auto always_case = event(events, "case", "with always");
        expect(fatal(always_case.has_value()));
        expect(eq(always_case->value("capabilities", json::object()), json{{"always", true}}));
    };
    "code for one platform runs only there"_test = [] {
        int ran = 0;
        on<platform::linux>([&](auto) { ran += host() == platform::linux; });
        on<platform::macos>([&](auto) { ran += host() == platform::macos; });
        on<platform::windows>([&](auto) { ran += host() == platform::windows; });
        expect(eq(ran, host() == platform::linux || host() == platform::macos ||
                               host() == platform::windows
                           ? 1
                           : 0));
    };
    return report();
}
