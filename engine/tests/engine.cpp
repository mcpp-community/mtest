// The engine, as its users run it: the `mtest` program over programs of each level, written here as
// scripts with the Bash adapter (adapters/script/mtest.sh), in a project of their own.
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;
import mcpplibs.os;
import nlohmann.json;

using namespace mcpplibs::mtest;
using json = nlohmann::json;

namespace {

// The repository's root, from this file's place in it.
std::filesystem::path repository() {
    return std::filesystem::path(std::source_location::current().file_name())
        .parent_path()
        .parent_path()
        .parent_path();
}

std::string adapter() {
    return (repository() / "adapters" / "script" / "mtest.sh").generic_string();
}

// A project directory holding `files`, scripts made executable. The tests name a script `bash
// t.sh`, as Windows starts no script by its first line.
std::filesystem::path
project(std::initializer_list<std::pair<std::string_view, std::string_view>> files) {
    auto root = e2e::tree(files);
    for (const auto& [name, _] : files)
        if (name.ends_with(".sh"))
            (void)mcpplibs::os::fs::set_executable(root / name, true);
    return root;
}

e2e::outcome mtest(const std::filesystem::path& cwd, std::vector<std::string> args,
                   std::string input = {}) {
    args.insert(args.begin(), e2e::program("mtest"));
    return e2e::run({.argv = args,
                     .cwd = cwd,
                     .input = std::move(input),
                     .timeout = std::chrono::seconds{120}});
}

json json_of(const e2e::outcome& r) {
    auto j = json::parse(r.out, nullptr, false);
    if (j.is_discarded())
        fail("not JSON: " + r.out.substr(0, 400) + r.err.substr(0, 400));
    return j;
}

// A protocol script: the cases given as `id:status`, each with a function that does what its status
// says.
std::string script(std::initializer_list<std::string_view> cases, std::string_view extra = {}) {
    std::string s = "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\n";
    s += "ok() { :; }\nbad() { mtest_eq 1 2; }\n";
    s += std::string(extra);
    for (auto c : cases) {
        const auto colon = c.find(':');
        const auto id = c.substr(0, colon);
        const auto what = c.substr(colon + 1);
        s += std::format("mtest_case '{}' {}\n", id,
                         what == "fail"   ? "bad"
                         : what == "pass" ? "ok"
                                          : what);
    }
    return s + "mtest_report\n";
}

}  // namespace

int main() {
    "an L0 program is one case, judged by its exit status"_test[kind::e2e] = [] {
        auto root = project({{"good.sh", "exit 0\n"}, {"bad.sh", "exit 4\n"}});
        auto r = mtest(root, {"run", "--json", "bash good.sh", "bash bad.sh"});
        expect(eq(r.status, 1));
        auto j = json_of(r);
        expect(eq(j["counts"].value("pass", 0), 1));
        expect(eq(j["counts"].value("fail", 0), 1));
        expect(eq(j["failing"][0]["why"].get<std::string>(), std::string("exit 4")));
    };
    "a protocol program's cases, their failures and a selection"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", script({"fs/a:pass", "fs/b:fail", "net/c:pass"})}});
        auto r = mtest(root, {"run", "--json", "bash t.sh"});
        auto j = json_of(r);
        expect(eq(j["cases"].get<int>(), 3));
        expect(eq(j["failing"].size(), std::size_t(1)));
        expect(eq(j["failing"][0]["case"].get<std::string>(), std::string("fs/b")));
        expect(contains(j["failing"][0]["where"].get<std::string>(), "t.sh:"));
        expect(contains(j["failing"][0]["again"].get<std::string>(), "--case fs/b"));
        auto only = json_of(mtest(root, {"run", "--json", "--case", "net/*", "bash t.sh"}));
        expect(eq(only["cases"].get<int>(), 1));
        expect(eq(only["status"].get<std::string>(), std::string("pass")));
    };
    "a program only a runner starts is listed and run through it (--run-under)"_test[kind::e2e] =
        [] {
            // Not executable by itself, as a program of another system is not on this one: both the
            // listing (P3) and the run start it under the runner.
            auto root = project({{"t.cross", script({"a:pass", "b:pass"})}});
            auto j = json_of(mtest(root, {"run", "--json", "--run-under", "bash", "./t.cross"}));
            expect(eq(j["cases"].get<int>(), 2));
            expect(eq(j["status"].get<std::string>(), std::string("pass")));
        };
    "a case a crash ended fails with the program's status (P5.4)"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", script({"dies:crash"}, "crash() { exit 9; }\n")}});
        auto j = json_of(mtest(root, {"run", "--json", "bash t.sh"}));
        expect(eq(j["status"].get<std::string>(), std::string("fail")));
        expect(contains(j["failing"][0]["why"].get<std::string>(), "exit 9"));
    };
    "a program past its timeout is ended and reported so"_test[kind::e2e][tag{"slow"}] = [] {
        auto root = project({{"hang.sh", "sleep 30\n"}});
        auto j = json_of(mtest(root, {"run", "--json", "--timeout", "1s", "bash hang.sh"}));
        expect(eq(j["counts"].value("timeout", 0), 1));
    };
    "a program that does not start is the machine's failure, not the test's"_test[kind::e2e] = [] {
        auto root = project({{"x.txt", ""}});
        auto r = mtest(root, {"run", "--json", "./no-such-program"});
        expect(eq(r.status, 3));
        expect(eq(json_of(r)["status"].get<std::string>(), std::string("infra")));
    };
    "explain says where, why and how to repeat it"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", script({"x:fail"})}});
        (void)mtest(root, {"run", "bash t.sh"});
        auto j = json_of(mtest(root, {"explain", "--json"}));
        const auto& c = j["cases"][0];
        expect(eq(c["case"].get<std::string>(), std::string("x")));
        expect(eq(c["why"][0]["expected"].get<std::string>(), std::string("2")));
        expect(eq(c["kind_of_failure"].get<std::string>(), std::string("first seen")));
        expect(contains(j["conclusion"].get<std::string>(), "again:"));
    };
    "rerun runs the cases that failed last"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", script({"a:pass", "b:fail"})},
                             {"mtest.toml", "[programs.t]\ncommand = [\"bash\", \"t.sh\"]\n"}});
        (void)mtest(root, {"run"});
        auto j = json_of(mtest(root, {"rerun", "--json"}));
        expect(eq(j["cases"].get<int>(), 1));
    };
    "a declared program, a set and a plan that says why"_test[kind::e2e] = [] {
        auto root = project(
            {{"t.sh", script({"a:pass", "b:pass"})},
             {"mtest.toml", "[programs.t]\ncommand = [\"bash\", \"t.sh\"]\ntags = [\"smoke\"]\n"
                            "sources = [\"src/*\"]\n"}});
        auto plan = json_of(mtest(root, {"plan", "--json", "--set", "smoke"}));
        expect(eq(plan["selected"].get<int>(), 2));
        auto changed = json_of(mtest(root, {"plan", "--json", "--changed", "docs/x.md"}));
        expect(eq(changed["selected"].get<int>(), 0))
            << "a change of nothing the program reads affects none of it";
        auto src = json_of(mtest(root, {"plan", "--json", "--changed", "src/a.cpp"}));
        expect(eq(src["selected"].get<int>(), 2));
        expect(contains(src["units"][0]["cases"][0]["why"].get<std::string>(), "src/a.cpp"));
    };
    "the pr set runs every case when the change cannot be read"_test[kind::e2e] = [] {
        // A repository of its own, with no `origin/main`: `git diff` cannot say what changed.
        auto root =
            project({{"t.sh", script({"a:pass", "b:pass"})},
                     {"mtest.toml",
                      "[programs.t]\ncommand = [\"bash\", \"t.sh\"]\nsources = [\"src/*\"]\n"}});
        if (!mcpplibs::os::env::find_executable("git"))
            return;
        (void)e2e::run({.argv = {"git", "init", "-q", "."}, .cwd = root});
        auto plan = json_of(mtest(root, {"plan", "--json", "--set", "pr"}));
        expect(eq(plan["selected"].get<int>(), 2)) << plan.dump();
        expect(contains(plan["notes"].dump(), "every case is affected"));
    };
    "a quarantined case fails without failing the run"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", script({"flaky:fail", "ok:pass"})}});
        (void)mtest(root, {"quarantine", "add", "--case", "bash t.sh::flaky", "--reason", "known"});
        auto r = mtest(root, {"run", "--json", "bash t.sh"});
        expect(eq(r.status, 0));
        expect(eq(json_of(r)["status"].get<std::string>(), std::string("pass")));
    };
    "a bench is judged against its baseline by the policy"_test[kind::e2e][kind::bench] = [] {
        auto root = project(
            {{"mtest.toml", "[programs.b]\ncommand = [\"bash\", \"b.sh\"]\n[policy.bench]\nlimit = "
                            "1.10\nspread = 0.05\n"},
             {"b.sh", script({"m:measure"}, "measure() { mtest_metric t \"$T\" s $T $T $T; }\n")}});
        (void)mtest(root, {"run", "--env", "T=1.0"});
        auto same = json_of(mtest(root, {"run", "--json", "--env", "T=1.05"}));
        auto pass = json_of(mtest(root, {"analyze", "bench", "--json"}));
        expect(eq(pass["items"][0]["verdict"].get<std::string>(), std::string("pass")))
            << pass.dump();
        (void)mtest(root, {"run", "--env", "T=2.0"});
        auto over = json_of(mtest(root, {"gate", "--set", "release", "--json"}));
        expect(!over["pass"].get<bool>());
        expect(contains(over["reasons"][0].get<std::string>(), "fail"));
    };
    "the graph names programs, cases, resources and artifacts"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() +
                                          "'\nok() { :; }\nmtest_case db ok --resource serial:db "
                                          "--artifact file:data.txt\nmtest_report\n"}});
        auto j = json_of(mtest(root, {"graph", "--json", "bash t.sh"}));
        std::set<std::string> kinds;
        for (const auto& n : j["nodes"])
            kinds.insert(n["kind"].get<std::string>());
        expect(kinds == std::set<std::string>{"program", "case", "resource", "artifact"})
            << j.dump();
        auto dot = mtest(root, {"graph", "--format", "dot", "bash t.sh"});
        expect(contains(dot.out, "digraph mtest"));
    };
    "a run as JUnit XML"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", script({"a:pass", "b:fail"})}});
        (void)mtest(root, {"run", "--junit", "report.xml", "bash t.sh"});
        auto xml = std::filesystem::exists(root / "report.xml");
        expect(xml);
    };
    "the store keeps every run and verifies itself"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", script({"a:pass"})}});
        for (int i = 0; i < 3; ++i)
            (void)mtest(root, {"run", "bash t.sh"});
        auto v = mtest(root, {"store", "verify", "--json"});
        expect(eq(v.status, 0)) << v.out;
        auto g =
            json_of(mtest(root, {"store", "gc", "--keep-days", "0", "--keep-runs", "1", "--json"}));
        expect(eq(g["removed"].get<int>(), 2));
        expect(std::filesystem::exists(root / ".mtest" / ".gitignore"));
    };
    "the service answers JSON-RPC and MCP on its standard streams"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", script({"a:pass"})}});
        const std::string input =
            R"({"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18"}})"
            "\n"
            R"({"jsonrpc":"2.0","id":2,"method":"tools/list"})"
            "\n"
            R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"run","arguments":{"programs":["bash t.sh"]}}})"
            "\n"
            R"({"jsonrpc":"2.0","id":4,"method":"status"})"
            "\n";
        auto r = mtest(root, {"serve"}, input);
        std::map<int, json> replies;
        std::string_view rest = r.out;
        while (!rest.empty()) {
            auto nl = rest.find('\n');
            auto j = json::parse(rest.substr(0, nl), nullptr, false);
            rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
            if (!j.is_discarded() && j.contains("id") && j["id"].is_number())
                replies[j["id"].get<int>()] = j;
        }
        expect(fatal(replies.size() == 4)) << r.out;
        expect(eq(replies[1]["result"]["serverInfo"]["name"].get<std::string>(),
                  std::string("mtest")));
        expect(gt(replies[2]["result"]["tools"].size(), std::size_t(10)));
        expect(eq(replies[3]["result"]["structuredContent"]["status"].get<std::string>(),
                  std::string("pass")));
        expect(!replies[3]["result"]["isError"].get<bool>());
        expect(replies[4]["result"]["data"].contains("conclusion"));
    };
    "a test session runs the cases asked for, from the launches mcpp gives (spec O9)"_test
        [kind::e2e] = [] {
            // mcpp's part, B5: the program built, and how it starts.
            auto root =
                project({{"t.sh", script({"a:pass", "b:fail"})},
                         {"mcpp.sh", "echo '{\"test\":\"t\",\"status\":\"built\",\"protocol\":1,"
                                     "\"launch\":{\"argv\":[\"bash\",\"t.sh\"],\"env\":{},\"cwd\":"
                                     "\"\",\"via\":\"\"}}'\n"}});
            const json run = {{"jsonrpc", "2.0"},
                              {"id", 1},
                              {"method", "test/run"},
                              {"params",
                               {{"project", root.generic_string()},
                                {"options", {{"mcpp", {"bash", "mcpp.sh"}}, {"isolation", "case"}}},
                                {"selection", {{{"program", "t"}, {"cases", {"a", "b"}}}}}}}};
            auto r = mtest(root, {"serve"}, run.dump() + "\n");
            std::vector<json> notes;
            json reply;
            std::string_view rest = r.out;
            while (!rest.empty()) {
                auto nl = rest.find('\n');
                auto j = json::parse(rest.substr(0, nl), nullptr, false);
                rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
                if (j.is_discarded())
                    continue;
                if (j.contains("id"))
                    reply = j;
                else
                    notes.push_back(j);
            }
            expect(fatal(reply.contains("result"))) << r.out << r.err;
            const auto id = reply["result"]["run"].get<std::string>();
            auto count = [&](std::string_view method, std::string_view key,
                             std::string_view value) {
                return std::ranges::count_if(notes, [&](const json& n) {
                    return n.value("method", "") == method &&
                           n["params"].value(std::string(key), "") == value;
                });
            };
            expect(eq(count("test/build", "event", "end"), 1)) << r.out;
            // A process per case: two programs, each case's end.
            expect(eq(count("test/program", "program", "t"), 2)) << r.out;
            expect(eq(count("test/case", "event", "end"), 2)) << r.out;
            const auto end = std::ranges::find_if(notes, [](const json& n) {
                return n.value("method", "") == "test/run";
            });
            expect(fatal(end != notes.end())) << r.out;
            expect(eq((*end)["params"]["run"].get<std::string>(), id));
            expect(eq((*end)["params"]["status"].get<std::string>(), std::string("fail")));
            expect(eq((*end)["params"]["counts"].value("pass", 0), 1));
            expect(eq((*end)["params"]["counts"].value("fail", 0), 1));
        };
    "every answer carries a conclusion"_test[kind::e2e] = [] {
        auto root = project({{"t.sh", script({"a:pass"})}});
        (void)mtest(root, {"run", "bash t.sh"});
        for (std::vector<std::string> args :
             {std::vector<std::string>{"status", "--json"},
              {"report", "--format", "json"},
              {"analyze", "durations", "--json"},
              {"analyze", "failures", "--json"},
              {"analyze", "flaky", "--json"},
              {"diff", "--before", "last", "--after", "last", "--json"},
              {"history", "--case", "a", "--json"},
              {"query", "--status", "pass", "--json"}}) {
            auto j = json_of(mtest(root, args));
            expect(j.contains("conclusion")) << args.front();
        }
    };
    "a program judged by its exit status says it did not apply: skip_exit"_test[kind::e2e] = [] {
        auto root = project(
            {{"suite/01_holds.sh", "exit 0\n"},
             {"suite/02_absent.sh", "echo 'SKIP: no emulator on this machine'\nexit 77\n"},
             {"suite/03_broken.sh", "exit 3\n"},
             {"mtest.toml", "[discover.e2e]\nglobs = [\"suite/[0-9]*.sh\"]\ncommand = [\"bash\", "
                            "\"{path}\"]\nskip_exit = 77\n"}});
        auto j = json_of(mtest(root, {"run", "--json"}));
        expect(eq(j["counts"].value("pass", 0), 1)) << j.dump();
        expect(eq(j["counts"].value("skip", 0), 1)) << j.dump();
        expect(eq(j["counts"].value("fail", 0), 1)) << j.dump();
        // The reason is the script's own `SKIP` line, kept with the case.
        auto skipped = json_of(mtest(root, {"query", "--status", "skip", "--json"}));
        expect(contains(skipped.dump(), "02_absent")) << skipped.dump();
        expect(contains(skipped.dump(), "no emulator on this machine")) << skipped.dump();
        // Without the key the same status is a failure, as it always was.
        auto plain = project({{"a.sh", "exit 77\n"}});
        auto k = json_of(mtest(plain, {"run", "--json", "bash a.sh"}));
        expect(eq(k["counts"].value("fail", 0), 1)) << k.dump();
        expect(eq(k["counts"].value("skip", 0), 0)) << k.dump();
    };
    "a discovered group: a command per file, tags and resources from its first lines"_test
        [kind::e2e] = [] {
            auto root = project(
                {{"suite/01_a.sh", "# requires: gcc elf\n# resources: serial:home\nexit 0\n"},
                 {"suite/02_b.sh", "# requires:\nexit 3\n"},
                 {"suite/helper.txt", ""},
                 {"suite/helper.sh", "exit 7\n"},
                 {"mtest.toml",
                  "[discover.e2e]\nglobs = [\"suite/[0-9]*.sh\"]\ncommand = [\"bash\", "
                  "\"{path}\"]\n"
                  "kind = \"e2e\"\ntimeout = \"30s\"\n"
                  "tags_from = { \"# requires:\" = \"requires:\" }\nresources_from = [\"# "
                  "resources:\"]\n"
                  "[cases.\"suite/02_*\"]\ntags = [\"slow\"]\n"}});
            auto plan = json_of(mtest(root, {"plan", "--json"}));
            expect(eq(plan["selected"].get<int>(), 2)) << plan.dump();
            auto graph = json_of(mtest(root, {"graph", "--json"}));
            std::set<std::string> nodes;
            for (const auto& n : graph["nodes"])
                nodes.insert(n.value("id", ""));
            expect(nodes.contains("resource:serial:home")) << graph.dump();
            auto gcc = json_of(mtest(root, {"plan", "--json", "--exclude-tag", "requires:gcc"}));
            expect(eq(gcc["selected"].get<int>(), 1));
            auto rest = json_of(mtest(root, {"plan", "--json", "--exclude-program", "suite/02*"}));
            expect(eq(rest["selected"].get<int>(), 1));
            auto slow = json_of(mtest(root, {"plan", "--json", "--tag", "slow"}));
            expect(
                eq(slow["units"][0]["program"].get<std::string>(), std::string("suite/02_b.sh")));
            auto both =
                json_of(mtest(root, {"plan", "--json", "--tag", "requires:gcc&requires:elf"}));
            expect(eq(both["selected"].get<int>(), 1)) << "a case with both tags";
            auto neither = json_of(mtest(root, {"plan", "--json", "--tag", "requires:gcc&slow"}));
            expect(eq(neither["selected"].get<int>(), 0)) << "no case has both";
            auto run = json_of(mtest(root, {"run", "--json"}));
            expect(eq(run["counts"].value("fail", 0), 1));
            expect(eq(run["failing"][0]["kind"].get<std::string>(), std::string("e2e")))
                << run.dump();
        };
    "parts of a selection: every program in one part, the longest first"_test[kind::e2e] = [] {
        auto root =
            project({{"a.sh", "sleep 0.4\n"},
                     {"b.sh", "sleep 0.2\n"},
                     {"c.sh", "exit 0\n"},
                     {"d.sh", "exit 0\n"},
                     {"mtest.toml",
                      "[discover.s]\nglobs = [\"*.sh\"]\ncommand = [\"bash\", \"{path}\"]\n"}});
        (void)mtest(root, {"run"});
        std::multiset<std::string> seen;
        for (const char* part : {"1/2", "2/2"}) {
            auto j = json_of(mtest(root, {"plan", "--json", "--shard", part}));
            for (const auto& u : j["units"])
                seen.insert(u["program"].get<std::string>());
        }
        expect(eq(seen.size(), std::size_t(4)));
        expect(eq(seen.count("a.sh"), std::size_t(1)));
        auto first = json_of(mtest(root, {"plan", "--json", "--shard", "1/2"}));
        expect(eq(first["units"][0]["program"].get<std::string>(), std::string("a.sh")))
            << "the longest program opens the first part";
        expect(eq(mtest(root, {"plan", "--shard", "3/2"}).status, 2));
    };
    "a coverage map decides which cases a change affects"_test[kind::e2e] = [] {
        auto root = project(
            {{"a.sh", "exit 0\n"},
             {"b.sh", "exit 0\n"},
             {"mtest.toml", "[discover.s]\nglobs = [\"*.sh\"]\ncommand = [\"bash\", \"{path}\"]\n"},
             {"map.json",
              R"({"format":"mtest-coverage/1","commit":"c0ffee","time":"t","epoch":)" +
                  std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count()) +
                  R"(,"cases":{"a.sh::a.sh":["src/x.cpp"],"b.sh::b.sh":["src/y.cpp"]}})"}});
        auto before = json_of(mtest(root, {"plan", "--json", "--changed", "src/x.cpp"}));
        expect(eq(before["selected"].get<int>(), 2))
            << "without a map, a program that states nothing is affected";
        expect(eq(mtest(root, {"coverage", "import", "--from", "map.json"}).status, 0));
        auto x = json_of(mtest(root, {"plan", "--json", "--changed", "src/x.cpp"}));
        expect(eq(x["selected"].get<int>(), 1)) << x.dump();
        expect(contains(x["units"][0]["cases"][0]["why"].get<std::string>(), "ran src/x.cpp"));
        auto shown = json_of(mtest(root, {"coverage", "--json"}));
        expect(shown["fresh"].get<bool>());
    };
    "a run under --coverage reads the counters each program's .gcda files record"_test[kind::
                                                                                           e2e] = [] {
        // GCC's format, written by the program itself: a.gcda with an arc that ran, b.gcda with a
        // run of zero counters (a negative length, GCC 12 and later).
        const std::string header =
            R"(\x61\x64\x63\x67\x2a\x31\x36\x42\x01\x02\x03\x04\x05\x06\x07\x08)"
            R"(\x00\x00\x00\x01\x0c\x00\x00\x00\x01\x00\x00\x00\x02\x00\x00\x00\x03\x00\x00\x00)";
        auto root = project(
            {{"t.sh",
              "mkdir -p \"$GCOV_PREFIX\"\nprintf '" + header +
                  R"(\x00\x00\xa1\x01\x08\x00\x00\x00\x01\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00' > "$GCOV_PREFIX/a.gcda"
printf ')" + header +
                  R"(\x00\x00\xa1\x01\xf8\xff\xff\xff\x00\x00\x00\x00' > "$GCOV_PREFIX/b.gcda"
)"},
             {"compile_commands.json",
              R"([{"directory":".","file":"a.c","output":"obj/a.o"},{"directory":".","file":"b.c","output":"obj/b.o"}])"},
             {"mtest.toml",
              "[programs.t]\ncommand = [\"bash\", \"t.sh\"]\n"
              "[coverage]\nobjects = \"obj\"\ndatabase = \"compile_commands.json\"\n"}});
        std::filesystem::create_directories(root / "obj");
        auto run = json_of(mtest(root, {"run", "--json", "--coverage"}));
        expect(eq(run["status"].get<std::string>(), std::string("pass"))) << run.dump();
        auto map = json_of(mtest(root, {"coverage", "--json"}));
        expect(eq(map["cases"]["t::t"], json::array({"a.c"}))) << map.dump();
    };
    "a run under --coverage maps each case to the sources it executed"_test[kind::e2e] = [] {
        // GCC's own `.gcda` files, where a plain `gcc` builds a program here.
        auto gcc = mcpplibs::os::env::find_executable("gcc");
        if (mcpplibs::os::host() != mcpplibs::os::platform::linux || !gcc)
            return;
        auto root =
            project({{"a.c", "int used(int x) { return x + 1; }\n"},
                     {"b.c", "int unused(int x) { return x * 2; }\n"},
                     {"main.c", "int used(int);\nint main(void) { return used(-1); }\n"},
                     {"mtest.toml",
                      "[programs.p]\ncommand = [\"./prog\"]\n"
                      "[coverage]\nobjects = \"obj\"\ndatabase = \"compile_commands.json\"\n"}});
        std::filesystem::create_directories(root / "obj");
        std::string db = "[";
        for (const char* f : {"a", "b", "main"}) {
            auto r =
                e2e::run({.argv = {mcpplibs::os::fs::utf8(*gcc), "--coverage", "-c",
                                   std::string(f) + ".c", "-o", "obj/" + std::string(f) + ".o"},
                          .cwd = root});
            if (r.status != 0)
                return;  // no plain C toolchain on this machine: the case above covers the format
            db += std::format(R"({}{{"directory":"{}","file":"{}.c","output":"obj/{}.o"}})",
                              db.size() > 1 ? "," : "", mcpplibs::os::fs::generic_utf8(root), f, f);
        }
        std::ofstream(root / "compile_commands.json") << db << "]";
        auto link = e2e::run({.argv = {mcpplibs::os::fs::utf8(*gcc), "--coverage", "obj/a.o",
                                       "obj/b.o", "obj/main.o", "-o", "prog"},
                              .cwd = root});
        if (link.status != 0)
            return;
        auto run = json_of(mtest(root, {"run", "--json", "--coverage"}));
        expect(eq(run["status"].get<std::string>(), std::string("pass"))) << run.dump();
        auto map = json_of(mtest(root, {"coverage", "--json"}));
        const auto& files = map["cases"]["p::p"];
        expect(std::ranges::find(files, json("a.c")) != files.end()) << map.dump();
        expect(std::ranges::find(files, json("b.c")) == files.end()) << map.dump();
        auto changed = json_of(mtest(root, {"plan", "--json", "--changed", "b.c"}));
        expect(eq(changed["selected"].get<int>(), 0)) << changed.dump();
    };
    "the cases a test file declares, read without building it"_test[kind::e2e] = [] {
        auto root = project(
            {{"tests/unit/parse.cpp",
              "import mcpplibs.mtest;\nusing namespace mcpplibs::mtest;\n"
              "suite parsing{\"parsing\", [] {\n"
              "    \"an empty line\"_test[tag{\"fast\"}] = [] { expect(true); };\n"
              "    // \"commented\"_test = [] {};\n"
              "    \"fields\"_test[kind::e2e] = [] {\n        \"one\"_test = [] {};\n    };\n"
              "}};\n"},
             {"mcpp.toml",
              "[package]\nname = \"p\"\nversion = \"0.1.0\"\n"
              "[package.metadata.mtest.cases.\"parsing/fields*\"]\nresources = [\"serial:db\"]\n"},
             {"mtest.toml", "[host]\nkind = \"mcpp\"\n"}});
        auto j = json_of(mtest(root, {"list", "--static", "--json"}));
        expect(fatal(eq(j["cases"].get<int>(), 4))) << j.dump();
        const auto& cases = j["programs"][0]["cases"];
        expect(eq(j["programs"][0]["program"].get<std::string>(), std::string("unit/parse")));
        expect(eq(cases[0]["case"].get<std::string>(), std::string("parsing")));
        expect(eq(cases[0]["kind"].get<std::string>(), std::string("unit")));
        expect(eq(cases[1]["case"].get<std::string>(), std::string("parsing/an empty line")));
        expect(eq(cases[1]["tags"][0].get<std::string>(), std::string("fast")));
        expect(eq(cases[1]["line"].get<int>(), 4));
        expect(eq(cases[2]["kind"].get<std::string>(), std::string("e2e")));
        expect(eq(cases[3]["case"].get<std::string>(), std::string("parsing/fields/one")));
        expect(eq(cases[2]["resources"][0].get<std::string>(), std::string("serial:db")));
    };
    "a case that states no kind is of its program's directory"_test[kind::e2e] = [] {
        const std::string head =
            "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\nok() { :; }\n";
        auto root = project(
            {{"tests/e2e/t.sh",
              head + "mtest_case plain ok\nmtest_case stated ok --kind unit\nmtest_report\n"},
             {"tests/l0.sh", "exit 0\n"},
             {"tests/cts/l0.sh", "exit 0\n"},
             {"mtest.toml",
              "[discover.all]\nglobs = [\"tests/**/*.sh\"]\ncommand = [\"bash\", \"{path}\"]\n"}});
        auto plan = json_of(mtest(root, {"plan", "--json"}));
        std::map<std::string, std::string> kinds;
        for (const auto& u : plan["units"])
            for (const auto& c : u["cases"])
                kinds[u.value("program", "") + "::" + c.value("case", "")] = c.value("kind", "");
        expect(eq(kinds["tests/e2e/t.sh::plain"], std::string("e2e"))) << plan.dump();
        expect(eq(kinds["tests/e2e/t.sh::stated"], std::string("unit")));
        expect(eq(kinds["tests/l0.sh::tests/l0.sh"], std::string("unit")));
        expect(eq(kinds["tests/cts/l0.sh::tests/cts/l0.sh"], std::string("cts")));
    };
    "a case's time limit ends the process that runs it alone"_test[kind::e2e][tag{"slow"}] = [] {
        const std::string head = "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\n";
        auto root = project(
            {{"t.sh",
              head + "ok() { :; }\nhang() { sleep 30; }\n"
                     "mtest_case quick ok\nmtest_case hangs hang --timeout 1\nmtest_report\n"}});
        const auto began = std::chrono::steady_clock::now();
        auto j = json_of(mtest(root, {"run", "--json", "--isolate", "bash t.sh"}));
        expect(eq(j["counts"].value("pass", 0), 1)) << j.dump();
        expect(eq(j["counts"].value("timeout", 0), 1));
        expect(lt(std::chrono::steady_clock::now() - began, std::chrono::seconds{20}));
    };
    "a suite's kind and attributes, read without building"_test[kind::e2e] = [] {
        auto root = project({{"tests/cts/spec.cpp",
                              "import mcpplibs.mtest;\nusing namespace mcpplibs::mtest;\n"
                              "CTS spec{\"spec\", clause{\"S1\"}, resource{\"machine\"}, [] {\n"
                              "    \"answers\"_test[clause{\"S2\"}] = [] {};\n}};\n"
                              "Suite plain{\"plain\", [] { \"by place\"_test = [] {}; }};\n"},
                             {"mcpp.toml", "[package]\nname = \"p\"\nversion = \"0.1.0\"\n"},
                             {"mtest.toml", "[host]\nkind = \"mcpp\"\n"}});
        auto j = json_of(mtest(root, {"list", "--static", "--json"}));
        const auto& cases = j["programs"][0]["cases"];
        expect(fatal(eq(cases.size(), std::size_t(4)))) << j.dump();
        expect(eq(cases[0]["case"].get<std::string>(), std::string("spec")));
        expect(eq(cases[0]["kind"].get<std::string>(), std::string("cts")));
        expect(eq(cases[0]["clauses"], json{"S1"}));
        expect(eq(cases[1]["clauses"], json{"S1", "S2"}));
        expect(eq(cases[1]["resources"], json{"machine"}));
        expect(eq(cases[3]["case"].get<std::string>(), std::string("plain/by place")));
        expect(eq(cases[3]["kind"].get<std::string>(), std::string("cts"))) << "by its directory";
    };
    "a suite type a module beside the tests declares"_test[kind::e2e] = [] {
        auto root = project(
            {{"tests/bench/timed.cppm",
              "export module timed;\nimport mcpplibs.mtest;\n"
              "export class Timed : public mcpplibs::mtest::Bench {\npublic:\n"
              "    using Bench::Bench;\n};\n"},
             {"tests/bench/startup.cpp", "import timed;\nTimed startup{\"startup\", [] {}};\n"},
             {"mcpp.toml", "[package]\nname = \"p\"\nversion = \"0.1.0\"\n"},
             {"mtest.toml", "[host]\nkind = \"mcpp\"\n"}});
        auto j = json_of(mtest(root, {"list", "--static", "--json"}));
        const auto& cases = j["programs"][0]["cases"];
        expect(fatal(eq(cases.size(), std::size_t(1)))) << j.dump();
        expect(eq(cases[0]["kind"].get<std::string>(), std::string("bench")));
        expect(eq(cases[0]["resources"], json{"machine"}));
    };
    "a case's isolation, retries and diagnosis, as the program states them"_test[kind::e2e] = [] {
        const std::string head = "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\n";
        auto root = project(
            {{"t.sh", head + "pid() { echo $$ >> pids.txt; }\n"
                             "second() { [ -e tried ] || { touch tried; mtest_eq 1 2; }; }\n"
                             "diag() { echo \"diagnosing ${DIAG:-no}\"; mtest_eq 1 2; }\n"
                             "mtest_case alone pid --isolate\nmtest_case with pid\n"
                             "mtest_case again second --retries 2\n"
                             "mtest_case diag diag --diagnose-env DIAG=yes\nmtest_report\n"}});
        auto j = json_of(mtest(root, {"run", "--json", "bash t.sh"}));
        std::ifstream pids(root / "pids.txt");
        std::set<std::string> distinct;
        for (std::string line; std::getline(pids, line);)
            distinct.insert(line);
        expect(eq(distinct.size(), std::size_t(2))) << "the isolated case shared a process";
        expect(eq(j["counts"].value("flaky", 0), 1)) << j.dump();
        expect(fatal(eq(j["failing"].size(), std::size_t(1))));
        const auto dir = j["failing"][0]["diagnosis"].get<std::string>();
        auto out = mcpplibs::os::fs::read(mcpplibs::os::fs::from_utf8(dir) / "out.txt");
        expect(fatal(out.has_value())) << dir;
        expect(contains(*out, "diagnosing yes"));
        auto explained = mtest(root, {"explain"});
        expect(contains(explained.out, "diagnosis: "));
    };
    "mtest.toml states what a program cannot: the same attributes, after the program's"_test
        [kind::e2e] = [] {
            const std::string head =
                "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\n";
            auto root = project(
                {{"t.sh", head + "ok() { :; }\nbad() { mtest_eq 1 2; }\n"
                                 "mtest_case known bad\nmtest_case off bad\nmtest_case own ok "
                                 "--kind e2e\nmtest_report\n"},
                 {"l0.sh", "exit 1\n"},
                 {"mtest.toml", "[cases.\"known\"]\nexpect_fail = \"a defect upstream\"\n"
                                "[cases.\"off\"]\nskip = \"not on this host\"\n"
                                "[cases.\"own\"]\nkind = \"bench\"\nclauses = [\"S1\"]\n"
                                "[cases.\"bash l0.sh\"]\nexpect_fail = \"exits 1 by design\"\n"}});
            auto j = json_of(mtest(root, {"run", "--json", "--report-json", "r.json", "bash t.sh",
                                          "bash l0.sh"}));
            expect(eq(j["status"].get<std::string>(), std::string("pass"))) << j.dump();
            expect(eq(j["counts"].value("xfail", 0), 2));
            expect(eq(j["counts"].value("skip", 0), 1));
            auto record = json::parse(*mcpplibs::os::fs::read(root / "r.json"));
            for (const auto& c : record["cases"])
                if (c.value("id", "") == "own")
                    expect(eq(c.value("kind", ""), std::string("e2e")))
                        << "the program's kind comes first";
        };
    "a run's timeline: when each program started and what it held"_test[kind::e2e] = [] {
        auto root =
            project({{"a.sh", "sleep 0.3\n"},
                     {"b.sh", "sleep 0.3\n"},
                     {"mtest.toml", "[cases.\"bash a.sh\"]\nresources = [\"serial:x\"]\n"
                                    "[cases.\"bash b.sh\"]\nresources = [\"serial:x\"]\n"}});
        json_of(mtest(root, {"run", "--json", "--jobs", "2", "--report-json", "r.json", "bash a.sh",
                             "bash b.sh"}));
        auto record = json::parse(*mcpplibs::os::fs::read(root / "r.json"));
        std::vector<double> starts;
        for (const auto& p : record["timeline"]) {
            expect(eq(p["resources"], json{"serial:x"}));
            starts.push_back(p.value("started_ms", 0.0));
        }
        expect(fatal(eq(starts.size(), std::size_t(2))));
        expect(ge(std::abs(starts[0] - starts[1]), 250.0))
            << "two holders of one serial group overlapped";
    };
    "a case's metrics across runs beside their bounds (analyze trend)"_test[kind::e2e] = [] {
        const std::string head = "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\n";
        auto root = project(
            {{"t.sh", head + "fast() { mtest_metric ready \"${READY:-3}\" s; }\n"
                             "mtest_case startup fast --at-most ready=15\nmtest_report\n"}});
        auto first = json_of(mtest(root, {"run", "--json", "bash t.sh"}));
        expect(eq(first["status"].get<std::string>(), std::string("pass"))) << first.dump();
        auto over = e2e::run(
            {.argv = {e2e::program("mtest"), "run", "--json", "--env", "READY=20", "bash t.sh"},
             .cwd = root});
        auto second = json_of(over);
        expect(eq(second["status"].get<std::string>(), std::string("fail"))) << second.dump();
        expect(contains(second["failing"][0]["why"].get<std::string>(), "over its bound 15"));
        auto t = json_of(mtest(root, {"analyze", "trend", "--json"}));
        expect(fatal(eq(t["items"].size(), std::size_t(1)))) << t.dump();
        const auto& item = t["items"][0];
        expect(eq(item["values"], json{3, 20}));
        expect(eq(item["bound"]["max"].get<double>(), 15.0));
        expect(lt(item["margin"].get<double>(), 0.0)) << "the last value is past its bound";
        expect(contains(t["conclusion"].get<std::string>(), "1 past its bound"));
    };
    "mtest.toml bounds a metric of a program that states none"_test[kind::e2e] = [] {
        const std::string head = "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\n";
        auto root = project(
            {{"t.sh", head + "m() { mtest_metric size 5 MB; }\nmtest_case sized m\nmtest_report\n"},
             {"mtest.toml", "[cases.\"sized\"]\nat_most = { size = 3 }\n"}});
        auto j = json_of(mtest(root, {"run", "--json", "bash t.sh"}));
        expect(eq(j["status"].get<std::string>(), std::string("fail"))) << j.dump();
        expect(contains(j["failing"][0]["why"].get<std::string>(), "over its bound 3"));
    };
    "output that is not UTF-8 is stored and reported, not refused"_test[kind::e2e] = [] {
        auto root = project(
            {{"bad.sh", "printf 'cp1252 \\x97 \\xff\\n'; printf '\\x97\\n' >&2; exit 1\n"}});
        auto r = mtest(root, {"run", "--json", "bash bad.sh"});
        expect(eq(r.status, 1)) << r.err;
        auto j = json_of(r);
        expect(eq(j["counts"].value("fail", 0), 1));
        expect(mcpplibs::os::fs::read(root / ".mtest" / "index.ndjson").has_value());
    };
    // ---- 0.5.0: the run as a graph, its report, its use of the machine, its fixtures ----------
    "a run's critical path is the longest path of its graph"_test[kind::e2e] = [] {
        // One worker: the second program waited for the first, so both are on the path.
        auto root = project({{"a.sh", "sleep 0.3\n"}, {"b.sh", "sleep 0.1\n"}});
        expect(fatal(
            eq(mtest(root, {"run", "--json", "--jobs", "1", "bash a.sh", "bash b.sh"}).status, 0)));
        auto j = json_of(mtest(root, {"analyze", "path", "--json"}));
        expect(fatal(eq(j["path"].size(), std::size_t(2)))) << j.dump();
        expect(eq(j["path"][1]["waited_for"].get<std::string>(), std::string("worker")));
        expect(ge(j["path_ms"].get<double>(), 350.0));
        auto g = mtest(root, {"graph", "--run", "last", "--format", "mermaid"});
        expect(contains(g.out, "flowchart LR"));
        expect(contains(g.out, "class n0,n1 crit")) << g.out;
    };
    "a markdown report has the sections asked for, with mermaid"_test[kind::e2e] = [] {
        auto root = project({{"a.sh", "exit 0\n"}, {"b.sh", "echo broken >&2; exit 3\n"}});
        (void)mtest(root, {"run", "--json", "bash a.sh", "bash b.sh"});
        auto r =
            mtest(root, {"report", "--format", "md", "--sections", "summary,failures,path,gantt"});
        expect(contains(r.out, "## Summary"));
        expect(contains(r.out, "## Failures"));
        expect(contains(r.out, "exit 3"));
        expect(contains(r.out, "## Critical path"));
        expect(contains(r.out, "```mermaid\ngantt"));
        expect(!contains(r.out, "## Slowest cases")) << "a section not asked for is left out";
    };
    "a run writes its markdown report when asked"_test[kind::e2e] = [] {
        auto root =
            project({{"a.sh", "exit 0\n"}, {"mtest.toml", "[report]\nsections = [\"summary\"]\n"}});
        expect(fatal(eq(mtest(root, {"run", "--json", "--md", "r.md", "bash a.sh"}).status, 0)));
        auto text = mcpplibs::os::fs::read(root / "r.md");
        expect(fatal(text.has_value()));
        expect(contains(*text, "## Summary"));
    };
    "the runs of a group are one report"_test[kind::e2e] = [] {
        auto root = project({{"a.sh", "exit 0\n"}, {"b.sh", "exit 0\n"}});
        (void)mtest(root, {"run", "--json", "--group", "ci-7", "bash a.sh"});
        (void)mtest(root, {"run", "--json", "--group", "ci-7", "bash b.sh"});
        auto r =
            mtest(root, {"report", "--format", "md", "--group", "ci-7", "--sections", "summary"});
        expect(contains(r.out, "| **all** |")) << r.out;
    };
    "a program's cases are divided among idle workers"_test[kind::e2e] = [] {
        const std::string head = "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\n";
        auto root = project({{"t.sh", head + "w() { sleep 0.2; }\nmtest_case a w\nmtest_case b w\n"
                                             "mtest_case c w\nmtest_case d w\nmtest_report\n"}});
        expect(fatal(
            eq(mtest(root, {"run", "--json", "--jobs", "2", "--report-json", "r.json", "bash t.sh"})
                   .status,
               0)));
        auto r = json::parse(*mcpplibs::os::fs::read(root / "r.json"));
        expect(fatal(eq(r["timeline"].size(), std::size_t(2)))) << r["timeline"].dump();
        expect(eq(r["timeline"][0]["select"].size() + r["timeline"][1]["select"].size(),
                  std::size_t(4)));
        expect(ne(r["timeline"][0]["worker"], r["timeline"][1]["worker"]));
    };
    "a run fixture is served once and its endpoint reaches its users"_test[kind::e2e] = [] {
        auto root = project(
            // Ready once the endpoint exists: announced first, a user started at once found no
            // directory (seen once in a macOS run of CI, 43359543216).
            {{"serve.sh",
              "mkdir -p served; echo \"mtest-ready $PWD/served\"; cat >/dev/null; rmdir served\n"},
             {"u1.sh", "test -d \"$MTEST_FIXTURE_HOME\" && echo one >> "
                       "\"$MTEST_FIXTURE_HOME/../uses.txt\"\n"},
             {"u2.sh", "test -d \"$MTEST_FIXTURE_HOME\" && echo two >> "
                       "\"$MTEST_FIXTURE_HOME/../uses.txt\"\n"},
             {"mtest.toml", "[fixtures.home]\ncommand = [\"bash\", \"serve.sh\"]\n"
                            "[cases.\"*u*\"]\nuses = [\"home\"]\n"}});
        auto j = json_of(mtest(root, {"run", "--json", "--jobs", "2", "bash u1.sh", "bash u2.sh"}));
        expect(eq(j["status"].get<std::string>(), std::string("pass"))) << j.dump();
        auto uses = mcpplibs::os::fs::read(root / "uses.txt");
        expect(fatal(uses.has_value()));
        expect(eq(std::ranges::count(*uses, '\n'), 2));
        expect(!std::filesystem::exists(root / "served"))
            << "the fixture ended after its last user";
        auto path = json_of(mtest(root, {"graph", "--run", "last", "--json"}));
        expect(contains(path.dump(), "fixture:home"));
    };
    "a program that does not speak the protocol is measured from outside"_test[kind::e2e] = [] {
        if (mcpplibs::os::host() == mcpplibs::os::platform::windows)
            skip("Windows does not account a program's children");
        if (!mcpplibs::os::process::resources(mcpplibs::os::process::of::children))
            skip("this system tells no program its children's use of resources");
        auto root = project({{"big.sh", "head -c 40000000 /dev/zero | tail -c 1 >/dev/null\n"}});
        expect(fatal(
            eq(mtest(root, {"run", "--json", "--report-json", "r.json", "bash big.sh"}).status,
               0)));
        auto r = json::parse(*mcpplibs::os::fs::read(root / "r.json"));
        const auto& u = r["timeline"][0]["usage"];
        expect(eq(u["source"].get<std::string>(), std::string("engine"))) << u.dump();
        expect(gt(u["peak_bytes"].get<double>(), 1.0e6));
    };
    // --- 0.7.0: capabilities (M1), coverage over collected runs, plan per host (M6), resume (M5)
    const std::string discovered =
        "[discover.s]\nglobs = [\"[0-9]*.sh\"]\ncommand = [\"bash\", \"{path}\"]\nlevel = 0\n"
        "skip_exit = 77\ntags_from = { \"# requires:\" = \"requires:\" }\n";
    "a capability this machine lacks makes a skip that names it"_test[kind::e2e] = [&] {
        auto root = project(
            {{"1_has.sh", "# requires: here\nexit 0\n"},
             {"2_lacks.sh", "# requires: absent\nexit 0\n"},
             {"mtest.toml", discovered + "[capabilities]\nhere = {}\n"
                                         "absent = { program = \"no-such-program-mtest\" }\n"}});
        auto j = json_of(mtest(root, {"run", "--json", "--report-json", "r.json"}));
        expect(eq(j["status"].get<std::string>(), std::string("pass"))) << j.dump();
        auto r = json::parse(*mcpplibs::os::fs::read(root / "r.json"));
        std::string reason;
        for (const auto& c : r["cases"])
            if (c["program"] == "2_lacks.sh")
                reason = c.value("reason", "");
        expect(contains(reason, "missing capability: absent")) << r.dump();
        expect(eq(r["capabilities"]["here"].get<bool>(), true));
        auto caps = json_of(mtest(root, {"capabilities", "--json"}));
        expect(eq(caps["capabilities"]["absent"]["present"].get<bool>(), false));
    };
    "a capability no entry declares is an error, not a skip"_test[kind::e2e] = [&] {
        auto root = project({{"1_x.sh", "# requires: nowhere\nexit 0\n"},
                             {"mtest.toml", discovered + "[capabilities]\nhere = {}\n"}});
        auto r = mtest(root, {"run"});
        expect(ne(r.status, 0));
        expect(contains(r.out + r.err, "does not declare")) << r.out << r.err;
    };
    "a capability is composed of others and of the environment"_test[kind::e2e] = [&] {
        auto root =
            project({{"mtest.toml", discovered + "[capabilities]\na = {}\n"
                                                 "b = { none = [\"a\"] }\nc = { all = [\"a\"] }\n"
                                                 "d = { env = { MTEST_TEST_SET = \"yes\" } }\n"}});
        auto caps = json_of(mtest(root, {"capabilities", "--json"}));
        expect(eq(caps["capabilities"]["b"]["present"].get<bool>(), false));
        expect(eq(caps["capabilities"]["c"]["present"].get<bool>(), true));
        expect(eq(caps["capabilities"]["d"]["present"].get<bool>(), false));
    };
    // --- 0.8.0: where a case runs, stated by the case; capabilities its program answers
    "a case runs only where its places hold, and the engine says where"_test[kind::e2e] = [] {
        const std::string head =
            "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\nok() { :; }\n";
        const std::string here(mcpplibs::os::name(mcpplibs::os::host()));
        auto root =
            project({{"t.sh", head + "mtest_case here ok --only " + here +
                                  "\nmtest_case elsewhere ok --except " + here +
                                  "\nmtest_case 'on another architecture' ok --only riscv64\n"
                                  "mtest_report\n"}});
        auto j = json_of(mtest(root, {"run", "--json", "--report-json", "r.json", "bash t.sh"}));
        expect(eq(j["status"].get<std::string>(), std::string("pass"))) << j.dump();
        auto r = json::parse(*mcpplibs::os::fs::read(root / "r.json"));
        std::map<std::string, json> by_id;
        for (const auto& c : r["cases"])
            by_id[c["id"].get<std::string>()] = c;
        expect(eq(by_id["here"]["status"].get<std::string>(), std::string("pass"))) << r.dump();
        expect(eq(by_id["here"].value("only", json::array()), json::array({here})))
            << "the record keeps its places";
        expect(eq(by_id["elsewhere"]["status"].get<std::string>(), std::string("skip")));
        expect(contains(by_id["elsewhere"].value("reason", ""), "not on " + here));
        if (mcpplibs::os::ARCH != mcpplibs::os::architecture::riscv64)
            expect(
                contains(by_id["on another architecture"].value("reason", ""), "only on riscv64"));
    };
    "a capability its program answers needs no entry in [capabilities]"_test[kind::e2e] = [&] {
        // A program that answers `requires:` words itself (the C++ library's `capability`) lists
        // them under `capabilities`, and the reason of a skip; the engine takes both.
        const std::string listing =
            R"js({"e":"program","framework":"fake","level":3}
{"e":"case","id":"has","name":"has","tags":["requires:mine"],"capabilities":{"mine":true}}
{"e":"case","id":"lacks","name":"lacks","tags":["requires:theirs"],"capabilities":{"theirs":false},"expect":"skip","reason":"missing capability: theirs (no theirs on PATH)"}
{"e":"done"}
)js";
        const std::string ran =
            R"({"e":"program","framework":"fake","level":3}
{"e":"begin","id":"has","t":0}
{"e":"end","id":"has","status":"pass","ms":1}
{"e":"done"}
)";
        auto root =
            project({{"listing.ndjson", listing},
                     {"ran.ndjson", ran},
                     {"1_fake.sh", "# mtest-protocol=1\nif [ \"$MTEST_MODE\" = list ]; then cat "
                                   "listing.ndjson > \"$MTEST_REPORT\"; "
                                   "else cat ran.ndjson > \"$MTEST_REPORT\"; fi\n"},
                     {"mtest.toml",
                      "[discover.s]\nglobs = [\"[0-9]*.sh\"]\ncommand = [\"bash\", \"{path}\"]\n"
                      "[capabilities]\nother = {}\n"}});
        auto j = json_of(mtest(root, {"run", "--json", "--report-json", "r.json"}));
        expect(eq(j["status"].get<std::string>(), std::string("pass"))) << j.dump();
        auto r = json::parse(*mcpplibs::os::fs::read(root / "r.json"));
        std::map<std::string, json> by_id;
        for (const auto& c : r["cases"])
            by_id[c["id"].get<std::string>()] = c;
        expect(eq(by_id["has"]["status"].get<std::string>(), std::string("pass"))) << r.dump();
        expect(eq(by_id["lacks"]["status"].get<std::string>(), std::string("skip")));
        expect(contains(by_id["lacks"].value("reason", ""), "missing capability: theirs"));
        expect(eq(r["capabilities"].value("mine", false), true))
            << "the run records the program's answers";
        expect(eq(r["capabilities"].value("theirs", true), false));
    };
    "a case stated for a place must pass there"_test[kind::e2e] = [] {
        const std::string head = "#!/usr/bin/env bash\n# mtest-protocol=1\n. '" + adapter() + "'\n";
        const std::string here(mcpplibs::os::name(mcpplibs::os::host()));
        auto root = project(
            {{"t.sh", head + "bad() { mtest_eq 1 2; }\nok() { :; }\nmtest_case fails bad --only " +
                          here + "\nmtest_case passes ok --only " + here +
                          "\nmtest_case nowhere ok --only riscv64 --only " + here +
                          "\nmtest_report\n"},
             {"mtest.toml",
              "[discover.s]\nglobs = [\"t.sh\"]\ncommand = [\"bash\", \"{path}\"]\n"}});
        (void)mtest(root, {"run", "--json", "--group", "g8"});
        auto j = json_of(mtest(root, {"ci", "coverage", "--group", "g8", "--json"}));
        const auto problems = j["problems"].dump();
        expect(contains(problems, "t.sh::fails runs only on " + here + " and did not pass on"))
            << j.dump();
        expect(!contains(problems, "t.sh::passes")) << j.dump();
        if (mcpplibs::os::ARCH != mcpplibs::os::architecture::riscv64)
            expect(contains(problems, "t.sh::nowhere runs only on riscv64, " + here +
                                          " and no run was there"))
                << j.dump();
    };
    "every program ran somewhere or is accounted for"_test[kind::e2e] = [&] {
        auto root = project({{"1_ok.sh", "exit 0\n"},
                             {"2_skips.sh", "echo 'SKIP: nothing here'; exit 77\n"},
                             {"3_owed.sh", "echo 'SKIP: nothing here'; exit 77\n"},
                             {"debt.tsv", "# owed\n3_owed.sh\tno host has it\n"},
                             {"mtest.toml", discovered + "[ci.coverage]\ndebt = \"debt.tsv\"\n"}});
        (void)mtest(root, {"run", "--json", "--group", "g1"});
        auto r = mtest(root, {"ci", "coverage", "--group", "g1", "--json"});
        expect(eq(r.status, 1));
        auto j = json_of(r);
        expect(eq(j["cases"]["1_ok.sh"].get<std::string>(), std::string("ran")));
        expect(eq(j["cases"]["2_skips.sh"].get<std::string>(), std::string("uncovered")));
        expect(eq(j["cases"]["3_owed.sh"].get<std::string>(), std::string("owed")));
    };
    "a debt entry for a program that ran fails the check"_test[kind::e2e] = [&] {
        auto root = project({{"1_ok.sh", "exit 0\n"},
                             {"debt.tsv", "1_ok.sh\tno host has it\n"},
                             {"mtest.toml", discovered + "[ci.coverage]\ndebt = \"debt.tsv\"\n"}});
        (void)mtest(root, {"run", "--json", "--group", "g2"});
        auto j = json_of(mtest(root, {"ci", "coverage", "--group", "g2", "--json"}));
        expect(contains(j["problems"].dump(), "remove the entry")) << j.dump();
    };
    "a plan divides the selection among the hosts by their history"_test[kind::e2e] = [&] {
        auto root =
            project({{"1_any.sh", "exit 0\n"},
                     {"2_linuxish.sh", "# requires: special\nexit 0\n"},
                     {"mtest.toml",
                      discovered + "[capabilities]\nspecial = { env = { MTEST_SPECIAL = \"1\" } }\n"
                                   "[ci.plan]\nshard = \"10m\"\n"}});
        auto run = e2e::run({.argv = {e2e::program("mtest"), "run", "--json"},
                             .cwd = root,
                             .env = {{"MTEST_HOST", "h1"}},
                             .timeout = std::chrono::seconds{120}});
        expect(fatal(eq(run.status, 0))) << run.out << run.err;
        auto j = json_of(mtest(root, {"ci", "plan", "--hosts", "h1", "--hosts", "h2", "--json"}));
        expect(fatal(eq(j["hosts"].size(), std::size_t(2))));
        expect(eq(j["hosts"][0]["cases"].get<int>(), 1)) << "h1 lacks `special`: " << j.dump();
        expect(eq(j["hosts"][0]["shards"].get<int>(), 1));
        expect(eq(j["hosts"][1]["cases"].get<int>(), 2)) << "h2 has no history: every case";
    };
    "a resumed run carries what the first judged and runs the rest"_test[kind::e2e] = [&] {
        auto root =
            project({{"1_a.sh", "exit 0\n"}, {"2_b.sh", "exit 0\n"}, {"mtest.toml", discovered}});
        expect(fatal(eq(mtest(root, {"run", "--json", "--program", "1_a.sh"}).status, 0)));
        auto j =
            json_of(mtest(root, {"run", "--json", "--resume", "last", "--report-json", "r.json"}));
        expect(eq(j["status"].get<std::string>(), std::string("pass")));
        auto r = json::parse(*mcpplibs::os::fs::read(root / "r.json"));
        expect(eq(r["cases"].size(), std::size_t(2))) << r.dump();
        expect(!r["resumed"].get<std::string>().empty());
        auto again = mtest(root, {"run", "--resume", "last"});
        expect(contains(again.out, "judged every selected case")) << again.out;
    };
    return report();
}
