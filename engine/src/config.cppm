// A project's mtest.toml (docs/config.md). Every key is optional: with no file,
// the engine runs the programs it is given, with the defaults below.
export module mcpplibs.mtest.engine.config;

import std;
export import mcpplibs.mtest.engine.model;

export namespace mcpplibs::mtest::engine {

struct set_spec {
    std::string name;
    std::vector<std::string> tags, kinds, programs, cases, include, exclude_tags;
    std::optional<std::chrono::milliseconds> budget;
    bool affected = false;    // only the cases a change affects (the `pr` set)
    bool all = false;         // every case (the `full` set)
    bool regression = false;  // the cases that guard a defect, and those that failed lately
    bool gate_bench = false;  // the `release` set's bench and conformance judgements
};

struct bench_policy {
    int pairs = 1;               // samples are judged per pair against the baseline
    double spread = 0.03;        // a ratio further than this from the median makes it invalid
    double limit = 1.15;         // the most a ratio may be
    bool repeat_invalid = true;  // an invalid measurement may be measured again
    bool repeat_valid = false;   // a valid one that failed may not (spec P6.2)
    std::string baseline =
        "last-pass";  // a run identifier, or the last run in which the case passed
};

struct trace_tool {
    std::string name;
    std::vector<std::string> run_under;  // prefixed to the program's command
    std::vector<std::string> build;      // run first: a rebuild the tool needs (a sanitizer)
    std::vector<std::pair<std::string, std::string>> env;
};

struct config {
    std::filesystem::path root;  // the directory of mtest.toml, or the working directory
    std::filesystem::path store = ".mtest";
    int jobs = 0;  // 0: the machine's
    std::vector<program_spec> programs;
    std::vector<std::string> discover;  // globs of executables
    std::string host;                   // "mcpp", or empty
    std::vector<std::string> host_args;
    std::map<std::string, set_spec> sets;
    bench_policy bench;
    int retries = 0;  // for cases tagged `flaky` and quarantined ones
    int regression_days = 30;
    int keep_days = 30;
    int keep_runs = 200;
    std::map<std::string, trace_tool> tools;
    std::vector<std::string> warnings;  // keys this engine does not know
};

// Reads `dir`/mtest.toml, or the defaults when there is none. Errors name the key.
std::expected<config, std::string> load_config(const std::filesystem::path& dir);
// "60s", "5m", "1500ms", "2h", or a number of seconds.
std::optional<std::chrono::milliseconds> parse_duration(std::string_view text);
// The built-in sets (smoke, regression, pr, full, release) under the declared ones.
void add_builtin_sets(config& c);

}  // namespace mcpplibs::mtest::engine
