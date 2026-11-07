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
    std::vector<std::string> tags, kinds, programs, exclude_tags;
    std::optional<std::vector<std::string>>
        changed;  // the paths a change touched, for affected selection
    std::optional<std::chrono::milliseconds> budget;  // run the most useful cases that fit
    bool failed_only = false;  // the cases that failed in the last run (`rerun --failed`)
};

struct unit_case {
    case_info info;
    std::string why;
};  // why: the rule that selected it, for `mtest plan`
struct unit {
    program_spec program;
    std::vector<unit_case> cases;  // the program's selected cases
    bool all = false;              // every case of the program: no selection is passed
    double expected_ms = 0;        // from history, for scheduling
};

struct plan {
    std::vector<unit> units;
    std::vector<std::string> notes;  // what a rule decided, and what it could not know
    std::size_t considered = 0, selected = 0;
};

// Lists each program (P3) and selects; `history` gives durations and failures.
std::expected<plan, std::string> make_plan(const config& c, const store& s,
                                           const std::vector<program_spec>& programs,
                                           const selection_request& r);

// `*` matches any run of characters, `?` one.
bool glob_match(std::string_view pattern, std::string_view text);
// The paths a change touched: `git diff --name-only <base>...HEAD`, run in `root`.
std::expected<std::vector<std::string>, std::string>
changed_since(const std::filesystem::path& root, std::string_view base);

}  // namespace mcpplibs::mtest::engine
