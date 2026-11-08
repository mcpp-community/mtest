// The suite types, attributes stated by scope, shared objects and time limits: the cases this file
// declares at namespace scope, as a runner lists and runs them.
import std;
import nlohmann.json;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;
import mcpplibs.os;

using namespace mcpplibs::mtest;
using json = nlohmann::json;

namespace {

// Where the shared object of `kept` says what happened to it; set by the program a case starts.
std::filesystem::path journal() {
    if (auto p = std::getenv("SUITES_JOURNAL"))
        return p;
    return {};
}
void note(std::string_view what) {
    if (const auto j = journal(); !j.empty()) {
        std::ofstream out(j, std::ios::app);
        out << what << "\n";
    }
}

struct expensive {
    expensive() {
        note("made");
    }
    ~expensive() {
        note("destroyed");
    }
    int value = 42;
};

// The suites the program lists and runs when a case starts it with --declared.
Suite plain{"plain", tag{"fast"}, [] {
                "of no kind"_test = [] {};
                "of its own kind"_test[kind::e2e] = [] {};
            }};

UTest units{"units", [] { "a unit"_test = [] {}; }};

CTS conformance{"conformance", clause{"P3.3"}, resource{"serial:spec"}, [] {
                    "names its clause"_test[clause{"P7.1"}] = [] {};
                    "an outer case"_test = [] { "and an inner one"_test[tag{"inner"}] = [] {}; };
                }};

E2E programs{"programs", resource{"serial:home"}, timeout{std::chrono::seconds{90}}, [] {
                 "runs with the home"_test = [] {};
                 "states its own limit"_test[timeout{std::chrono::seconds{5}}] = [] {};
             }};

Bench measured{"measured", [] { "holds the machine"_test = [] {}; }};

Suite kept{"kept", [] {
               shared<expensive> object{"expensive"};
               "uses the object"_test = [&] { expect(eq(object->value, 42)); };
               "uses it again"_test = [&] { expect(eq((*object).value, 42)); };
               "does not use it"_test = [] {};
           }};

Suite scoped{"scoped", [] {
                 expect(!std::getenv("SCOPE_FAILS")) << "the scope's own statement";
                 "inner"_test = [] {};
             }};

Suite skipped{"skipped", skip_if{true, "not on this system"},
              [] { "would fail"_test = [] { expect(false); }; }};

// The program again, under a runner: its report's events.
std::vector<json> events_of(std::string_view mode, std::vector<std::string> select = {},
                            std::filesystem::path journal_file = {},
                            std::vector<std::pair<std::string, std::string>> extra = {}) {
    const auto dir = temp_dir();
    const auto report_file = dir / "report.ndjson";
    std::filesystem::remove(report_file);
    auto argv = self_command();
    argv.push_back("--declared");
    e2e::command c{.argv = argv};
    c.env = {{"MTEST_PROTOCOL", "1"},
             {"MTEST_MODE", std::string(mode)},
             {"MTEST_REPORT", report_file.string()}};
    if (!select.empty()) {
        std::ofstream out(dir / "select");
        for (const auto& id : select)
            out << id << "\n";
        c.env.emplace_back("MTEST_SELECT", (dir / "select").string());
    }
    if (!journal_file.empty())
        c.env.emplace_back("SUITES_JOURNAL", journal_file.string());
    for (auto& [k, v] : extra)
        c.env.emplace_back(k, v);
    auto r = e2e::run(std::move(c));
    expect(fatal(r.started)) << "the program did not start again";
    std::vector<json> out;
    std::ifstream in(report_file);
    for (std::string line; std::getline(in, line);)
        if (!line.empty())
            out.push_back(json::parse(line));
    return out;
}

std::map<std::string, json> listed() {
    std::map<std::string, json> cases;
    for (const auto& e : events_of("list"))
        if (e.value("e", "") == "case")
            cases[e.value("id", "")] = e;
    return cases;
}

std::vector<std::string> strings(const json& j, const char* key) {
    std::vector<std::string> out;
    if (auto it = j.find(key); it != j.end() && it->is_array())
        for (const auto& v : *it)
            out.push_back(v.get<std::string>());
    return out;
}

std::vector<std::string> lines_of(const std::filesystem::path& file) {
    std::vector<std::string> out;
    std::ifstream in(file);
    for (std::string line; std::getline(in, line);)
        out.push_back(line);
    return out;
}

std::map<std::string, json> begun(const std::vector<json>& events) {
    std::map<std::string, json> out;
    for (const auto& e : events)
        if (e.value("e", "") == "begin")
            out[e.value("id", "")] = e;
    return out;
}

std::map<std::string, std::string> ended(const std::vector<json>& events) {
    std::map<std::string, std::string> out;
    for (const auto& e : events)
        if (e.value("e", "") == "end")
            out[e.value("id", "")] = e.value("status", "");
    return out;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--declared")
        return report();
    "each object is one case, listed without running its body"_test = [] {
        auto cases = listed();
        expect(eq(cases.size(), std::size_t(8))) << [&] {
            std::string ids;
            for (const auto& [id, _] : cases)
                ids += id + " ";
            return ids;
        }();
        expect(cases.contains("plain") && cases.contains("kept") && cases.contains("skipped"));
        expect(!cases.contains("plain/of no kind"))
            << "a case its body declares is reported when it runs";
    };
    "a suite's type is its kind; a Suite states none"_test = [] {
        auto cases = listed();
        expect(!cases["plain"].contains("kind"));
        expect(eq(cases["units"].value("kind", ""), std::string("unit")));
        expect(eq(cases["conformance"].value("kind", ""), std::string("cts")));
        expect(eq(cases["programs"].value("kind", ""), std::string("e2e")));
        expect(eq(cases["measured"].value("kind", ""), std::string("bench")));
    };
    "an object's attributes are its case's"_test = [] {
        auto cases = listed();
        expect(eq(strings(cases["plain"], "tags"), std::vector<std::string>{"fast"}));
        expect(eq(strings(cases["conformance"], "clauses"), std::vector<std::string>{"P3.3"}));
        expect(eq(strings(cases["conformance"], "resources"),
                  std::vector<std::string>{"serial:spec"}));
        expect(eq(strings(cases["measured"], "resources"), std::vector<std::string>{"machine"}));
        expect(eq(cases["skipped"].value("expect", ""), std::string("skip")));
        expect(eq(cases["programs"].value("timeout", 0.0), 90.0));
        expect(!cases["units"].contains("timeout"));
    };
    "the cases a body declares have its attributes and add their own"_test = [] {
        auto run = events_of("run", {"conformance", "plain"});
        auto b = begun(run);
        auto inner = b["conformance/an outer case/and an inner one"];
        expect(eq(inner.value("kind", ""), std::string("cts")));
        expect(eq(strings(inner, "tags"), std::vector<std::string>{"inner"}));
        expect(eq(strings(b["conformance/names its clause"], "clauses"),
                  std::vector<std::string>{"P3.3", "P7.1"}));
        expect(!b["plain/of no kind"].contains("kind"));
        expect(eq(b["plain/of its own kind"].value("kind", ""), std::string("e2e")));
        expect(eq(ended(run)["conformance"], std::string("pass")));
    };
    "one case of a body runs alone"_test = [] {
        auto run = events_of("run", {"conformance/names its clause"});
        auto e = ended(run);
        expect(eq(e["conformance/names its clause"], std::string("pass")));
        expect(!e.contains("conformance/an outer case")) << "a case not selected ran";
        expect(!e.contains("conformance")) << "the scope of the selected case was reported";
        expect(
            !std::ranges::any_of(run, [](const json& x) { return x.value("e", "") == "missing"; }));
    };
    "a scope is reported when a statement of its own fails"_test = [] {
        auto quiet = ended(events_of("run", {"scoped/inner"}));
        expect(eq(quiet["scoped/inner"], std::string("pass")));
        expect(!quiet.contains("scoped"));
        auto loud = ended(events_of("run", {"scoped/inner"}, {}, {{"SCOPE_FAILS", "1"}}));
        expect(eq(loud["scoped"], std::string("fail")));
        expect(eq(loud["scoped/inner"], std::string("pass")));
    };
    "a shared object is made by the first case that uses it, once"_test = [] {
        const auto journal_file = temp_dir() / "journal";
        events_of("list", {}, journal_file);
        expect(lines_of(journal_file).empty()) << "listing made the object";
        events_of("run", {"kept/does not use it"}, journal_file);
        expect(lines_of(journal_file).empty()) << "a case that does not use it made it";
        auto run = events_of("run", {"kept/uses the object", "kept/uses it again"}, journal_file);
        expect(eq(lines_of(journal_file), std::vector<std::string>{"made", "destroyed"}));
        auto made =
            std::ranges::find_if(run, [](const json& x) { return x.value("e", "") == "fixture"; });
        expect(fatal(made != run.end()));
        expect(eq(made->value("id", ""), std::string("kept/uses the object")));
        expect(eq(made->value("name", ""), std::string("expensive")));
    };
    return report();
}
