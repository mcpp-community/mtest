// mtest: the engine's command line (docs/commands.md). Each command is dispatch()'s, so the CLI,
// the service and the MCP tools give one answer; `--json` prints the data instead of the text.
import std;
import mcpplibs.os;
import mcpplibs.mtest.engine.model;
import mcpplibs.mtest.engine.commands;
import mcpplibs.mtest.engine.service;

namespace os = mcpplibs::os;
using namespace mcpplibs::mtest::engine;

namespace {

constexpr std::string_view help_text =
    R"(mtest 0.1.0 -- run tests of any language, and ask about their runs

  mtest run [PROGRAM...] [-- PROGRAM ARGS...]   run (the programs named, else mtest.toml's)
      --set S  --case GLOB  --tag T  --kind K  --program GLOB  --since COMMIT  --changed PATH  --budget 60s
      --jobs N  --seed N  --fail-fast  --retries N  --isolate  --timeout 5m  --env K=V  --run-under CMD
      --junit FILE  --html FILE  --report-json FILE  --verbose
  mtest rerun                                   the cases that failed in the last run
  mtest smoke | golden                          the smoke set | write golden files (MTEST_GOLDEN=update)
  mtest plan | affected | list [selection]      what would run, and why
  mtest status | cancel [--run ID]              runs in progress
  mtest report [--run ID] [--format text|json|junit|html] [--out FILE]
  mtest diff [--before ID] [--after ID]         newly failing, fixed, slower, faster
  mtest explain [--run ID] [--case REF]         where, why, the output, and the command that repeats it
  mtest history --case REF | query [--status S] [--kind K] [--days N] ...
  mtest analyze hot|durations|path|failures|flaky|bench|cost [--run ID]   (also: mtest hot, mtest path)
  mtest graph [--format json|dot|mermaid]       sources, programs, cases, resources, artifacts
  mtest gate [--set S] [--run ID]               a set's gate on a run
  mtest quarantine list|add|remove --case REF [--until DATE] [--reason TEXT]
  mtest store gc|verify|import [--from DIR]
  mtest trace --with TOOL [selection]           valgrind strace ltrace perf rr time okrun-prof, or [trace.*]
  mtest bisect --case REF --good C [--bad C] [--build CMD]
  mtest ci status OWNER/REPO#N... | ci annotate [--run ID] | ci collect --from DIR
  mtest serve                                   JSON-RPC and MCP on the standard streams
Every command takes --json. Exit status: 0 done, 1 a test or a gate failed, 2 a usage error, 3 the machine's.
)";

const std::set<std::string_view> list_keys = {"case",      "tag",         "kind",     "program",
                                              "changed",   "exclude_tag", "programs", "prs",
                                              "run_under", "status"};
const std::set<std::string_view> flag_keys = {"fail_fast", "isolate", "verbose",
                                              "failed",    "json",    "update"};
const std::set<std::string_view> int_keys = {"jobs", "seed", "retries",   "limit",    "top",
                                             "runs", "days", "keep_days", "keep_runs"};

std::string key_of(std::string_view option) {
    std::string k(option.substr(2));
    std::ranges::replace(k, '-', '_');
    return k;
}

}  // namespace

int main(int argc, char** argv) {
    std::vector<std::string> args(argv + 1, argv + argc);
    if (args.empty() || args[0] == "help" || args[0] == "--help" || args[0] == "-h") {
        std::print("{}", help_text);
        return args.empty() ? 2 : 0;
    }
    if (args[0] == "version" || args[0] == "--version") {
        std::println("mtest 0.1.0");
        return 0;
    }
    const auto cwd = std::filesystem::current_path();
    if (args[0] == "serve")
        return serve(cwd);

    const std::string command = args[0];
    json params = json::object();
    bool as_json = false;
    std::vector<std::string> positional;
    for (std::size_t i = 1; i < args.size(); ++i) {
        std::string_view a = args[i];
        if (a == "--") {
            params["argv"] =
                std::vector<std::string>(args.begin() + std::ptrdiff_t(i) + 1, args.end());
            break;
        }
        if (!a.starts_with("--")) {
            positional.emplace_back(a);
            continue;
        }
        std::string key, value;
        bool has_value = false;
        if (auto eq = a.find('='); eq != std::string_view::npos) {
            key = key_of(a.substr(0, eq));
            value = std::string(a.substr(eq + 1));
            has_value = true;
        } else
            key = key_of(a);
        if (key == "json") {
            as_json = true;
            continue;
        }
        if (flag_keys.contains(key) && !has_value) {
            params[key] = true;
            continue;
        }
        if (!has_value) {
            if (i + 1 >= args.size()) {
                std::println(std::cerr, "mtest: --{} needs a value", key);
                return 2;
            }
            value = args[++i];
        }
        if (key == "env") {
            auto eq = value.find('=');
            if (eq == std::string::npos) {
                std::println(std::cerr, "mtest: --env takes NAME=VALUE");
                return 2;
            }
            params["env"][value.substr(0, eq)] = value.substr(eq + 1);
        } else if (key == "junit" || key == "html" || key == "report_json") {
            params["report"][key == "report_json" ? "json" : key] = value;
        } else if (key == "run_under") {
            for (std::size_t s = 0, e; s <= value.size(); s = e + 1) {
                e = value.find(' ', s);
                if (e == std::string::npos)
                    e = value.size();
                if (e > s)
                    params["run_under"].push_back(value.substr(s, e - s));
            }
        } else if (list_keys.contains(key)) {
            params[key].push_back(value);
        } else if (int_keys.contains(key)) {
            std::int64_t v = 0;
            auto [p, ec] = std::from_chars(value.data(), value.data() + value.size(), v);
            if (ec != std::errc{} || p != value.data() + value.size()) {
                std::println(std::cerr, "mtest: --{} takes a number", key);
                return 2;
            }
            params[key] = v;
        } else if (key == "ratio") {
            params[key] = std::stod(value);
        } else {
            params[key] = value;
        }
    }
    // Positional words: the programs of a run, the action of a command that has actions, the pull
    // requests of ci.
    if (!positional.empty()) {
        if (command == "quarantine" || command == "store") {
            params["action"] = positional[0];
            positional.erase(positional.begin());
        } else if (command == "analyze") {
            params["metric"] = positional[0];
            positional.erase(positional.begin());
        } else if (command == "ci") {
            params["action"] = positional[0];
            positional.erase(positional.begin());
            for (auto& p : positional)
                params["prs"].push_back(p);
            positional.clear();
        } else if (command == "explain" || command == "history") {
            params["case"] = positional[0];
            positional.erase(positional.begin());
        }
        for (auto& p : positional)
            params["programs"].push_back(p);
    }
    auto a = dispatch(cwd, command, params, {});
    if (as_json)
        std::println("{}", a.data.dump(2));
    else if (a.status == 2)
        std::print(std::cerr, "{}", a.text);
    else
        std::print("{}", a.text);
    return a.status;
}
