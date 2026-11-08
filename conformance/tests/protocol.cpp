// The program side of the mtest protocol, clause by clause (spec/protocol.md), against the
// implementation MTEST_CTS_PROGRAM names, or this package's fixture (mcpplibs.mtest).
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;
import mcpplibs.os;
import nlohmann.json;

using namespace mcpplibs::mtest;
namespace os = mcpplibs::os;
using json = nlohmann::json;

namespace {

// MTEST_CTS_PROGRAM is a command, the program and its arguments separated by spaces
// (`bash fixture.sh` on a system that does not start a script itself).
std::vector<std::string> program() {
    if (auto p = os::env::get("MTEST_CTS_PROGRAM"); p && !p->empty()) {
        std::vector<std::string> words;
        for (auto word : std::views::split(*p, ' '))
            if (!word.empty())
                words.emplace_back(word.begin(), word.end());
        return words;
    }
    if (auto p = os::env::get("MCPP_BIN_FIXTURE"); p && !p->empty())
        return {*p};
    return {};
}

struct result {
    int status = -1;
    std::vector<json> events;
    std::vector<std::string> bad_lines;
    std::string out;
};

// Runs the program as a runner would (P2.1) and reads its report (P5.1).
result run(std::string_view mode, std::optional<std::vector<std::string>> select = std::nullopt) {
    result r;
    const auto cmd = program();
    if (cmd.empty())
        fail("no program under test: MTEST_CTS_PROGRAM or MCPP_BIN_FIXTURE");
    static int runs = 0;
    const auto dir = temp_dir() / std::to_string(++runs);
    std::filesystem::create_directories(dir);
    const auto report = dir / "report.ndjson";
    // By the library's e2e::run: the protocol's variables of this program are not passed on (P2.2),
    // the runner's are set, and an ok image is started by its loader.
    e2e::command c;
    c.argv = cmd;
    c.timeout = std::chrono::seconds{60};
    c.env = {{"MTEST_PROTOCOL", "1"},
             {"MTEST_MODE", std::string(mode)},
             {"MTEST_REPORT", os::fs::utf8(report)},
             {"MTEST_TMPDIR", os::fs::utf8(dir / "tmp")}};
    if (select) {
        std::string lines;
        for (const auto& id : *select)
            lines += id + "\n";
        expect(fatal(os::fs::write(dir / "select.txt", lines).has_value()));
        c.env.emplace_back("MTEST_SELECT", os::fs::utf8(dir / "select.txt"));
    }
    auto done = e2e::run(std::move(c));
    expect(fatal(done.started)) << "the program did not start: " << done.err;
    r.status = done.status;
    r.out = done.out;
    if (auto text = os::fs::read(report)) {
        std::string_view rest = *text;
        while (!rest.empty()) {
            auto nl = rest.find('\n');
            std::string line(rest.substr(0, nl));
            rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
            if (line.empty())
                continue;
            auto v = json::parse(line, nullptr, false);
            if (v.is_discarded() || !v.is_object())
                r.bad_lines.push_back(line);
            else
                r.events.push_back(std::move(v));
        }
    }
    return r;
}

std::vector<json> of(const result& r, std::string_view e) {
    std::vector<json> out;
    for (const auto& v : r.events)
        if (v.value("e", "") == e)
            out.push_back(v);
    return out;
}

std::optional<json> find(const result& r, std::string_view e, std::string_view id) {
    for (const auto& v : r.events)
        if (v.value("e", "") == e && v.value("id", "") == id)
            return v;
    return std::nullopt;
}

// The fixture's identifiers as the implementation forms them (spec/conformance.md): as written, or,
// for GoogleTest (MTEST_CTS_ID_STYLE=gtest), `<suite>/<name>` with the suite `conformance` and no
// spaces.
std::string id(std::string_view spec) {
    if (os::env::get("MTEST_CTS_ID_STYLE").value_or("") != "gtest")
        return std::string(spec);
    std::string out = spec.contains('/') ? std::string(spec) : "conformance/" + std::string(spec);
    std::ranges::replace(out, ' ', '_');
    return out;
}

// The fixture's cases an implementation of `level` has: below level 3 it cannot expect a failure.
std::vector<std::string> fixture_ids(int level) {
    std::vector<std::string> out;
    for (std::string_view s : {"passes", "fails", "skips", "expected to fail", "group/inner",
                               "measures", "covers", "holds"})
        if (level >= 3 || s != "expected to fail")
            out.push_back(id(s));
    return out;
}

int level(const result& r) {
    for (const auto& v : of(r, "program"))
        return v.value("level", 0);
    return 0;
}

}  // namespace

int main() {
    "P2.3 the program's file carries the marker"_test[kind::cts][clause{"P2.3"}] = [] {
        const auto cmd = program();
        expect(fatal(!cmd.empty()));
        // The program's file is the command's last word: a script after its interpreter.
        auto bytes = os::fs::read(cmd.back());
        expect(fatal(bytes.has_value()));
        const std::string_view text = *bytes;
        bool found = false;
        for (auto at = text.find("mtest-protocol=1"); at != std::string_view::npos;
             at = text.find("mtest-protocol=1", at + 1)) {
            const auto next = at + 16;
            if (next >= text.size() || !std::isdigit(static_cast<unsigned char>(text[next]))) {
                found = true;
                break;
            }
        }
        expect(found) << "no `mtest-protocol=1' in " << cmd.front();
    };
    "P3.1 listing writes the program, each case and done, and runs nothing"_test[kind::cts][clause{
        "P3.1"}] = [] {
        const auto r = run("list");
        expect(eq(r.status, 0));
        expect(fatal(!r.events.empty()));
        expect(eq(r.events.front().value("e", ""), std::string("program")));
        expect(eq(r.events.back().value("e", ""), std::string("done")));
        expect(eq(of(r, "begin").size(), std::size_t(0))) << "a case ran while listing";
        std::set<std::string> listed;
        for (const auto& c : of(r, "case"))
            listed.insert(c.value("id", ""));
        // P3.4: a case declared inside a case may be listed through the case that declares it.
        auto listed_or_declared = [&](const std::string& want) {
            for (auto at = want.size(); at != std::string::npos; at = want.rfind('/', at - 1)) {
                if (listed.contains(want.substr(0, at)))
                    return true;
                if (at == 0)
                    break;
            }
            return false;
        };
        for (const auto& want : fixture_ids(level(r)))
            expect(listed_or_declared(want)) << "not listed: " << want;
    };
    "P3.2 identifiers are distinct and stable"_test[kind::cts][clause{"P3.2"}] = [] {
        const auto a = run("list");
        const auto b = run("list");
        std::vector<std::string> ia, ib;
        for (const auto& c : of(a, "case"))
            ia.push_back(c.value("id", ""));
        for (const auto& c : of(b, "case"))
            ib.push_back(c.value("id", ""));
        expect(eq(std::set<std::string>(ia.begin(), ia.end()).size(), ia.size()))
            << "two cases share an identifier";
        expect(ia == ib) << "the list changed between two runs";
    };
    "P3.3 a case event names its location and attributes"_test[kind::cts][clause{"P3.3"}] = [] {
        const auto r = run("list");
        if (level(r) < 3)
            skip("the implementation is below level 3");
        auto passes = find(r, "case", id("passes"));
        expect(fatal(passes.has_value()));
        expect(!passes->value("file", "").empty());
        expect(gt(passes->value("line", 0), 0));
        expect(passes->value("tags", json::array()) == json::array({"fast"}));
        auto measures = find(r, "case", id("measures"));
        expect(fatal(measures.has_value()));
        expect(eq(measures->value("kind", ""), std::string("bench")));
        auto covers = find(r, "case", id("covers"));
        expect(fatal(covers.has_value()));
        expect(covers->value("clauses", json::array()) == json::array({"P3.1"}));
        auto holds = find(r, "case", id("holds"));
        expect(fatal(holds.has_value()));
        expect(holds->value("resources", json::array()) == json::array({"serial:db"}));
        expect(holds->value("artifacts", json::array()) == json::array({"file:data/x.txt"}));
        auto xf = find(r, "case", id("expected to fail"));
        expect(fatal(xf.has_value()));
        expect(eq(xf->value("expect", ""), std::string("fail")));
    };
    "P4.1 a selection runs exactly the selected cases"_test[kind::cts][clause{"P4.1"}] = [] {
        const auto r = run("run", std::vector<std::string>{id("passes"), id("group/inner")});
        std::set<std::string> ran;
        for (const auto& b : of(r, "end"))
            ran.insert(b.value("id", ""));
        expect(ran == std::set<std::string>{id("passes"), id("group/inner")})
            << "ran " << ran.size() << " cases";
        expect(eq(r.status, 0));
    };
    "P4.1 an identifier that names no case is missing and fails the run"_test[kind::cts][clause{
        "P4.1"}] = [] {
        const auto r = run("run", std::vector<std::string>{id("passes"), "no such case"});
        expect(find(r, "missing", "no such case").has_value());
        expect(ne(r.status, 0));
    };
    "P5.1 every line of the report is a JSON object"_test[kind::cts][clause{"P5.1"}] = [] {
        const auto r = run("run");
        expect(r.bad_lines.empty())
            << "not JSON: " << (r.bad_lines.empty() ? "" : r.bad_lines.front());
        for (const auto& v : r.events)
            expect(v.contains("e")) << v.dump();
    };
    "P5.2 the first event states the protocol and the level"_test[kind::cts][clause{"P5.2"}] = [] {
        const auto r = run("run");
        expect(fatal(!r.events.empty()));
        const auto& p = r.events.front();
        expect(eq(p.value("e", ""), std::string("program")));
        expect(eq(p.value("protocol", 0), 1));
        expect(ge(p.value("level", 0), 1));
        expect(le(p.value("level", 0), 3));
    };
    "P5.2 a failure names its place, and both sides of a comparison"_test[kind::cts][clause{
        "P5.2"}] = [] {
        const auto r = run("run");
        auto f = find(r, "failure", id("fails"));
        expect(fatal(f.has_value())) << "no failure event for `fails'";
        expect(!f->value("file", "").empty());
        expect(gt(f->value("line", 0), 0));
        expect(!f->value("message", "").empty());
        // Both sides are the structured part of level 3; below it the message carries them.
        if (level(r) >= 3) {
            expect(eq(f->value("actual", ""), std::string("1")));
            expect(eq(f->value("expected", ""), std::string("2")));
        }
    };
    "P5.3 each case ends with its status"_test[kind::cts][clause{"P5.3"}] = [] {
        const auto r = run("run");
        std::map<std::string, std::string> want = {{id("passes"), "pass"},
                                                   {id("fails"), "fail"},
                                                   {id("skips"), "skip"},
                                                   {id("group/inner"), "pass"},
                                                   {id("measures"), "pass"},
                                                   {id("covers"), "pass"},
                                                   {id("holds"), "pass"}};
        if (level(r) >= 3)
            want[id("expected to fail")] = "xfail";
        for (const auto& [case_id, status] : want) {
            auto e = find(r, "end", case_id);
            expect(e.has_value()) << "no end for " << case_id;
            if (e)
                expect(eq(e->value("status", ""), status)) << case_id;
        }
        auto sk = find(r, "end", id("skips"));
        if (sk)
            expect(eq(sk->value("reason", ""), std::string("not here")));
        for (const auto& e : of(r, "end"))
            expect(find(r, "begin", e.value("id", "")).has_value()) << "end without begin";
    };
    "P5.5 the exit status is zero exactly when nothing failed"_test[kind::cts][clause{"P5.5"}] =
        [] {
            expect(ne(run("run").status, 0));
            expect(eq(run("run", std::vector<std::string>{id("passes"), id("skips")}).status, 0));
        };
    "P6.1 a metric has a name, a value, a unit and its samples"_test[kind::cts][clause{"P6.1"}] =
        [] {
            const auto r = run("run");
            if (level(r) < 2)
                skip("the implementation is below level 2");
            auto m = find(r, "metric", id("measures"));
            expect(fatal(m.has_value()));
            expect(eq(m->value("name", ""), std::string("elapsed")));
            expect(eq(m->value("unit", ""), std::string("s")));
            expect(near(m->value("value", 0.0), 0.5, 1e-9));
            expect(m->value("samples", json::array()) == json::array({0.4, 0.5, 0.6}));
        };
    return report();
}
