// The coverage map (docs/coverage.md): for each case, the source files its run executed, measured
// by a run under `--coverage` of instrumented programs and kept in the store. Affected selection
// reads it as each case's statement of what it reads, so a change selects the cases whose runs
// executed a file it touched.
//
//   <store>/coverage.json   {"format": "mtest-coverage/1", "commit", "time", "epoch", "cases":
//                            {"<program>::<case>": ["src/a.cpp", ...]}}
export module mcpplibs.mtest.engine.coverage;

import std;
export import mcpplibs.mtest.engine.config;
export import mcpplibs.mtest.engine.store;

export namespace mcpplibs::mtest::engine {

inline constexpr std::string_view kCoverageFormat = "mtest-coverage/1";

struct coverage_map {
    std::string commit, time;  // the run it was measured from
    std::int64_t epoch = 0;    // when, in seconds since 1970: what `max_age` is measured against
    std::map<std::string, std::vector<std::string>> cases;  // program::case -> files from the root
};

std::filesystem::path coverage_file(const store& s);
std::expected<coverage_map, std::string> read_coverage(const std::filesystem::path& file);
void write_coverage(const std::filesystem::path& file, const coverage_map& m);
// The store's map when it is no older than `max_age`; none otherwise.
std::optional<coverage_map> fresh_coverage(const store& s, std::chrono::milliseconds max_age);

// Whether a `.gcda` file of GCC's records a counter that ran: GCC 12 and later write a run of zero
// counters as a negative length, so a file whose every arc record is zero shows no positive one.
bool gcda_executed(const std::filesystem::path& gcda);

// The directory `GCOV_PREFIX` names for a program's run, and the `GCOV_PREFIX_STRIP` that writes
// each `.gcda` at its object's path below the objects directory.
int gcov_strip(const std::filesystem::path& objects);

// The map of a run: `dirs` gives, for each directory a program wrote its `.gcda` files in, the
// cases of that program; the compile database names each object's source.
std::expected<coverage_map, std::string>
collect_coverage(const config& c,
                 const std::map<std::filesystem::path, std::vector<std::string>>& dirs,
                 std::string commit);

}  // namespace mcpplibs::mtest::engine
