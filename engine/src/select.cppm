// What runs: the programs (given, declared, discovered, or a host's), the cases each lists, and the
// selection of them by set, tag, kind, identifier, change and time budget (mtest design §4.5,
// §4.6).
export module mcpplibs.mtest.engine.select;

import std;
export import mcpplibs.mtest.engine.config;
export import mcpplibs.mtest.engine.store;

export namespace mcpplibs::mtest::engine {

struct program_source {
    std::vector<std::vector<std::string>> commands;  // given on the command line
    bool host = true;                                // the configured host's programs too
};

// The programs a run considers. Errors name what could not be read (a host that did not answer).
std::expected<std::vector<program_spec>, std::string> discover(const config& c,
                                                               const program_source& given);

struct selection_request {
    std::string set;                 // a set's name; empty: every case
    std::vector<std::string> cases;  // identifier globs (`*` and `?`), `program::id` or `id`
    std::vector<std::string> tags, kinds, programs, exclude_tags, exclude_programs;
    std::optional<std::vector<std::string>>
        changed;  // the paths a change touched, for affected selection
    std::optional<std::chrono::milliseconds> budget;  // run the most useful cases that fit
    bool failed_only = false;  // the cases that failed in the last run (`rerun --failed`)
    // `--shard I/N`: the I-th (1-based) of N parts, the longest units first, each to the part
    // with the least expected time; every machine with the same store computes the same parts.
    std::optional<std::pair<int, int>> shard;
    // The change could not be read (a shallow clone without the base): a set that selects by
    // change selects every case, and says why. Empty: nothing went wrong.
    std::string unknown_change;
};

struct unit_case {
    case_info info;
    std::string why;
    double expected_ms = 0;  // from history, for dividing a program among processes
};  // why: the rule that selected it, for `mtest plan`
struct unit {
    program_spec program;
    std::vector<unit_case> cases;         // the program's selected cases
    bool all = false;                     // every case of the program: no selection is passed
    double expected_ms = 0;               // from history, for scheduling
    std::optional<double> expected_peak;  // its largest resident set, as last measured (bytes)
};

struct plan {
    std::vector<unit> units;
    // The run fixtures programs serve (P11) and the pools components bring (P7.3), from the
    // listings: a fixture's name -> the program that serves it; a pool's name -> its capacity.
    std::map<std::string, program_spec> fixture_providers;
    std::map<std::string, int> pools;
    std::vector<std::string> notes;  // what a rule decided, and what it could not know
    std::size_t considered = 0, selected = 0;
};

// Lists each program (P3) and selects; `history` gives durations and failures.
std::expected<plan, std::string> make_plan(const config& c, const store& s,
                                           const std::vector<program_spec>& programs,
                                           const selection_request& r);

// The attributes `[cases."<glob>"]` states, applied to a case of `program`, and to an L0 program
// (whose one case is the program).
void apply_attributes(const config& c, const program_spec& program, case_info& ci);
// The kind a program's place states (docs/layout.md): a directory `unit`, `e2e`, `cts` or `bench`
// directly below a directory `tests`; empty when it states none.
std::string layout_kind(std::string_view path);
void apply_attributes(const config& c, program_spec& p);
// A path glob in the vocabulary of `[build] sources`: `*` any run of characters, `**/` any
// directories, none included; a glob beginning with `!` excludes.
bool path_glob_match(std::string_view pattern, std::string_view path);
// "2/8": the second of eight parts.
std::expected<std::pair<int, int>, std::string> parse_shard(std::string_view text);
// `*` matches any run of characters, `?` one.
bool glob_match(std::string_view pattern, std::string_view text);
// The paths a change touched: `git diff --name-only <base>...HEAD`, run in `root`.
std::expected<std::vector<std::string>, std::string>
changed_since(const std::filesystem::path& root, std::string_view base);

}  // namespace mcpplibs::mtest::engine
