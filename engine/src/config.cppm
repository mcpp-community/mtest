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

// `[discover.<name>]`: the files a glob matches, each a program run by a command (`{path}`: the
// file, absolute; `{name}`: its path from the root), with the keys of `[programs.*]`, and tags and
// resources read from the file's first lines: each word after a line's prefix.
struct discover_group {
    std::string name;
    std::vector<std::string> globs;
    std::vector<std::string> command;
    program_spec base;
    std::vector<std::pair<std::string, std::string>> tags_from;  // line prefix, tag prefix
    std::vector<std::string> resources_from;                     // line prefixes
};

// `[cases."<glob>"]` of mtest.toml, and `[package.metadata.mtest.cases."<glob>"]` of a host's
// mcpp.toml: the attributes of the cases a glob matches (`program::id` or `id`; an L0 program's
// one case is the program), in the vocabulary of the library's (docs/config.md "Case
// attributes"), for a program whose source states none. What a program states comes first: a
// kind, a time limit, isolation, retries, a diagnosis, a skip and an expected failure apply where
// it states none; tags, clauses, resources and artifacts add up.
struct case_attributes {
    std::string glob;
    std::vector<std::string> tags, clauses, resources, artifacts;
    std::string kind;
    std::optional<std::chrono::seconds> timeout;
    bool isolated = false;
    int retries = 0;
    std::optional<std::vector<std::pair<std::string, std::string>>> diagnose_env;
    double diagnose_timeout = 0;
    std::string skip, expect_fail;  // reasons
    std::string origin;             // where it is stated, for `mtest plan`
};

// `[coverage]`: the coverage map a run under `--coverage` writes (docs/coverage.md).
struct coverage_spec {
    std::string objects;   // the directory of the instrumented objects (`.gcno`, `.gcda`)
    std::string database;  // a compile database that names each object's source
    std::chrono::milliseconds max_age{std::chrono::days{7}};  // an older map affects every case
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
    std::vector<std::string> discover;   // globs of executables
    std::vector<discover_group> groups;  // [discover.<name>]
    std::vector<case_attributes> attributes;
    // `[static] globs`: the test files `mtest list --static` reads; a host's own when it names
    // them (mcpp.toml `[test] discover`, else `tests/**/*.cpp`).
    std::vector<std::string> static_globs;
    coverage_spec coverage;
    std::string host;  // "mcpp", or empty
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
