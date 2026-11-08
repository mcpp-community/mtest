// The text states what the program lists: the cases this file declares, read by the static
// manifest, against the listing this program writes under a runner (P3), case by case -- the kind,
// the tags, the resources, the clauses and the line.
import std;
import nlohmann.json;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;
import mcpplibs.mtest.manifest;
import mcpplibs.os;

using namespace mcpplibs::mtest;
namespace mf = mcpplibs::mtest::manifest;
using json = nlohmann::json;

namespace {

// A suite type of this file's own, of its base's kind. (What its constructor adds is the program's
// to list: the text states the kind.)
class Timed : public Bench {
public:
    template <class... A>
    Timed(name_here name, A&&... args) : Bench(name, std::forward<A>(args)...) {}
};

Suite plain{"plain", tag{"fast"}, [] { "inner"_test[kind::e2e] = [] {}; }};
UTest unit{"unit", resource{"serial:db"}, [] {}};
CTS spec{"spec", clause{"P3.3"}, [] {}};
E2E cli{"cli", resource{"serial:home"}, artifact{"file:data"}, [] {}};
Bench measured{"measured", at_most{"ready", 15}, at_least{"recall", 0.9}, [] {
                   metric("ready", 1.5, "s");
                   metric("recall", 0.95, "1");
               }};
Timed timed{"timed", [] {}};

std::vector<mf::static_bound> bounds_of(const json& j) {
    std::vector<mf::static_bound> out;
    for (const auto& b : j.value("bounds", json::array())) {
        mf::static_bound one{b.value("metric", ""), std::nullopt, std::nullopt};
        if (b.contains("max"))
            one.max = b["max"].get<double>();
        if (b.contains("min"))
            one.min = b["min"].get<double>();
        out.push_back(one);
    }
    return out;
}

std::vector<std::string> strings(const json& j, const char* key) {
    std::vector<std::string> out;
    if (auto it = j.find(key); it != j.end() && it->is_array())
        for (const auto& v : *it)
            out.push_back(v.get<std::string>());
    return out;
}

// This program's listing, as a runner reads it.
std::map<std::string, json> listed() {
    const auto report = temp_dir() / "list.ndjson";
    auto argv = self_command();
    argv.push_back("--declared");
    auto r = e2e::run({.argv = argv,
                       .env = {{"MTEST_PROTOCOL", "1"},
                               {"MTEST_MODE", "list"},
                               {"MTEST_REPORT", report.string()}}});
    expect(fatal(r.started));
    std::map<std::string, json> cases;
    std::ifstream in(report);
    for (std::string line; std::getline(in, line);) {
        auto e = json::parse(line, nullptr, false);
        if (e.is_object() && e.value("e", "") == "case")
            cases[e.value("id", "")] = e;
    }
    return cases;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--declared")
        return report();
    "the text states what the program lists"_test = [] {
        auto cases = listed();
        expect(fatal(eq(cases.size(), std::size_t(6))));
        auto text = mcpplibs::os::fs::read(std::source_location::current().file_name());
        expect(fatal(text.has_value()));
        std::size_t compared = 0;
        for (const auto& sc : mf::scan(*text)) {
            auto it = cases.find(sc.id);
            if (it == cases.end())
                continue;  // a case of main, or one a body declares
            ++compared;
            expect(eq(sc.kind, it->second.value("kind", std::string{}))) << sc.id;
            expect(eq(sc.tags, strings(it->second, "tags"))) << sc.id;
            expect(eq(sc.resources, strings(it->second, "resources"))) << sc.id;
            expect(eq(sc.clauses, strings(it->second, "clauses"))) << sc.id;
            expect(eq(sc.artifacts, strings(it->second, "artifacts"))) << sc.id;
            expect(eq(sc.line, it->second.value("line", 0))) << sc.id;
            expect(sc.bounds == bounds_of(it->second)) << sc.id << ": the bounds";
        }
        expect(eq(compared, cases.size()));
    };
    "a case a body declares has its scope's attributes"_test = [] {
        auto text = mcpplibs::os::fs::read(std::source_location::current().file_name());
        expect(fatal(text.has_value()));
        auto cases = mf::scan(*text);
        auto inner = std::ranges::find(cases, std::string("plain/inner"), &mf::static_case::id);
        expect(fatal(inner != cases.end()));
        expect(eq(inner->kind, std::string("e2e")));
        expect(eq(inner->tags, std::vector<std::string>{"fast"}));
        auto bounded = std::ranges::find(cases, std::string("measured"), &mf::static_case::id);
        expect(fatal(bounded != cases.end()));
        expect(eq(bounded->bounds.size(), std::size_t(2)));
        expect(bounded->bounds[0].max == std::optional<double>(15) &&
               bounded->bounds[1].min == std::optional<double>(0.9));
    };
    return report();
}
