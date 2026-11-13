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
    std::string skip, expect_fail;   // reasons
    std::vector<std::string> uses;   // run fixtures (P11)
    std::vector<bound_info> bounds;  // at_most / at_least of its metrics (P6.5)
    std::string origin;              // where it is stated, for `mtest plan`
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

// `[run]`: how the engine places work (docs/config.md "Running").
struct run_spec {
    // "auto": a program's cases are divided among several processes of it while workers would
    // otherwise idle, by their measured times; "off": a program runs in one process.
    std::string split = "auto";
    // The memory the engine schedules within: "auto" (the machine's), "off", or a size ("16GiB").
    // Programs run together only while their largest resident sets, as measured, fit.
    std::string memory = "auto";
};

// `[monitor]` (P10): how closely use is measured; a case may ask for more.
struct monitor_spec {
    std::string level = "basic";  // off, basic, detailed, trace
    int sample_ms = 50;           // `trace`: how often the load and the resident set are sampled
};

// `[report]`: what `mtest report --format md` writes when the command does not say.
struct report_spec {
    std::vector<std::string> sections{"summary",   "failures", "slowest", "metrics",
                                      "resources", "path",     "gantt"};
    std::string scope = "run";  // run, program:<name>, kind:<kind>, group
    int top = 10;               // rows of the ranked tables
    int max_nodes = 60;         // nodes of a drawn graph
};

// `[fixtures.<name>]`: a run fixture a command serves (P11), for one no test program declares.
struct fixture_spec {
    std::string name;
    std::vector<std::string> command;
};

// `[capabilities]` (0.7.0, M1): a word a case requires through a tag (`requires:gcc`), and how
// this machine answers it. Every test an entry states holds for the word to be present; an entry
// that states none is present everywhere. The words replace a runner's own probing, so that a
// requirement no machine can answer is a configuration error rather than a silent skip.
struct capability_spec {
    std::string name;
    std::vector<std::string> systems;   // `os`: the engine's system names (linux, macos, windows)
    std::vector<std::string> archs;     // `arch`: x86_64, aarch64, riscv64, wasm32
    std::vector<std::string> programs;  // `program`: each found on PATH
    std::vector<std::string> command;   // `command`: runs and exits 0 within 30 s
    std::vector<std::string> paths;     // `path`: some glob names a file; `${NAME}` is the environment's
    std::vector<std::pair<std::string, std::string>> env;  // `env`: NAME equals the value ("*": set)
    std::vector<std::string> all, none;  // other words: every one present; none present
};

struct capabilities_spec {
    std::string tag = "requires:";                // the tag prefix whose words are capabilities
    std::map<std::string, capability_spec> words;
};

// `[ci.coverage]` (0.7.0): the files `mtest ci coverage` reads beside the collected runs. Each is a
// tab-separated table whose first column is a case (`program`, or `program::id`); `#` begins a
// comment.
struct ci_coverage_spec {
    std::string exceptions;  // <case>\t<why no machine of CI can run it>
    std::string debt;        // <case>\t<what the machines lack>: it skips everywhere; only shrinks
    std::string hosts;       // <case>\t<hosts, space-separated>\t<why>: it must pass on each
};

// `[ci.plan]` (0.7.0, M6): how `mtest ci plan` divides a selection among the hosts of CI.
struct ci_plan_spec {
    std::vector<std::string> hosts;                      // the host names runs record (MTEST_HOST)
    std::chrono::milliseconds shard{std::chrono::minutes{15}};  // the expected time of one shard
    int max_shards = 8;
    std::chrono::milliseconds unknown{std::chrono::seconds{30}};  // a case with no history
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
    run_spec run;
    monitor_spec monitor;
    report_spec report;
    std::vector<fixture_spec> fixtures;
    capabilities_spec capabilities;
    ci_coverage_spec ci_coverage;
    ci_plan_spec ci_plan;
    std::vector<std::string> warnings;  // keys this engine does not know
};

// "16GiB", "512MiB", "1.5GB": bytes; nullopt for anything else.
std::optional<double> parse_size(std::string_view text);

// Reads `dir`/mtest.toml, or the defaults when there is none. Errors name the key.
std::expected<config, std::string> load_config(const std::filesystem::path& dir);
// "60s", "5m", "1500ms", "2h", or a number of seconds.
std::optional<std::chrono::milliseconds> parse_duration(std::string_view text);
// The built-in sets (smoke, regression, pr, full, release) under the declared ones.
void add_builtin_sets(config& c);

}  // namespace mcpplibs::mtest::engine
