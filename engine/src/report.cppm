// A run, or the runs of one group, as a Markdown document (mtest design 2026-11-09 §17.4): the
// sections a reader asks for -- the summary, the failures, the slowest cases, the metrics beside
// their bounds, the use of the machine, the flaky cases, the trends, the critical path, the graph
// and the schedule -- in tables and mermaid, for a file, a pull request or a CI job's summary.
export module mcpplibs.mtest.engine.report;

import std;
export import mcpplibs.mtest.engine.config;
export import mcpplibs.mtest.engine.store;

export namespace mcpplibs::mtest::engine {

struct report_options {
    std::vector<std::string> sections;  // empty: the configuration's
    std::string scope = "run";          // run, program:<glob>, kind:<kind>
    int top = 10;
    int max_nodes = 60;
    std::string title;  // empty: "mtest report"
};

// Every section a report knows, in the order it writes them.
inline constexpr std::string_view kSections[] = {
    "summary", "kinds", "failures", "slowest", "metrics", "resources",
    "flaky",   "trend", "path",     "graph",   "gantt",
};

std::string markdown_report(const store& s, const std::vector<run_record>& runs,
                            const report_options& o);

// `analyze trend`: each metric of each case over the last `count` stored runs, oldest first, beside
// the bounds the case states (P6.5).
json trend(const store& s, std::size_t count, const std::vector<std::string>& cases);

}  // namespace mcpplibs::mtest::engine
