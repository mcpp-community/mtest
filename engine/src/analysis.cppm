// What the runs say (mtest design §7, §6.2): each answer is data with its unit, its source runs,
// and a short conclusion, so a person reads one line and an agent acts on the fields.
export module mcpplibs.mtest.engine.analysis;

import std;
export import mcpplibs.mtest.engine.config;
export import mcpplibs.mtest.engine.store;

export namespace mcpplibs::mtest::engine {

// The slowest cases of a run.
json hot(const run_record& r, std::size_t top);
// Where a run's time went: by kind and by program, the share each program had of the wall clock,
// and the critical path (the longest program: no schedule finishes before it) against the wall
// clock.
json durations(const run_record& r);
// The cases that changed between two runs: newly failing, fixed, slower and faster beyond `ratio`.
json diff(const run_record& before, const run_record& after, double ratio = 1.25);
// One case across the stored runs, newest first.
json history(const store& s, std::string_view program, std::string_view id, std::size_t limit);
// The cases whose status changed between consecutive runs of the same commit, or that passed on a
// retry.
json flaky(const store& s, std::size_t runs);
// Failures grouped by their message with numbers and paths blanked out, largest group first.
json failures(const run_record& r);

// THE BENCH VERDICT (spec/protocol.md P6.2, mtest design §4.4). Each metric of a bench case is
// compared with the same metric in the baseline run: the ratio of the medians (or of the values)
// against the policy's limit, the samples' spread against its bound. `pass`, `fail` (over the
// limit), or `invalid` (too dispersed to judge, or no baseline); `repeatable` says whether the
// policy lets it be measured again.
struct bench_verdict {
    std::string program, id, metric, verdict, reason;
    double ratio = 0, spread = 0;
    bool repeatable = false;
};
std::vector<bench_verdict>
judge_bench(const run_record& r, const std::optional<run_record>& baseline, const bench_policy& p);
// The baseline of a policy: a named run, or the newest earlier run in which each bench case passed.
std::optional<run_record> bench_baseline(const store& s, const run_record& r,
                                         const bench_policy& p);

// A set's gate (§4.5): no failing case, a smoke set within its budget, bench verdicts that pass,
// and the clauses each cts case names all covered when a clause list is given.
json gate(const config& c, const store& s, const run_record& r, std::string_view set);

}  // namespace mcpplibs::mtest::engine
