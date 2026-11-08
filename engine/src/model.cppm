// The engine's data: what a program is, what a run of it gave, and a run of many,
// in the shapes the store keeps and every command prints (spec/schema/).
export module mcpplibs.mtest.engine.model;

import std;
export import nlohmann.json;

export namespace mcpplibs::mtest::engine {

using json = nlohmann::json;

// A program to run: its command (a runner first, for an image), where, with what.
struct program_spec {
    std::string name;  // stable: the host's test name, or the path
    std::vector<std::string> argv;
    std::string cwd;
    std::vector<std::pair<std::string, std::string>> env;
    int level = -1;                      // -1: not known yet (spec P1, P2.3)
    std::vector<std::string> resources;  // the program's own, for an L0 program
    std::vector<std::string> sources;    // paths whose change affects it (affected selection)
    std::vector<std::string> tags;
    std::chrono::seconds timeout{300};
    std::string isolation = "process";  // "process" or "case"
    std::string origin = "config";      // "given" (the command line), "config", "discover", "host"
    // The kind of an L0 program's one case and of a case that states none: stated in mtest.toml,
    // else by the program's directory (docs/layout.md), else unit. Empty until `discover` gives it.
    std::string kind;
};

struct case_info {
    std::string id, name, file, kind, expect;  // kind: empty when the case states none
    int line = 0;
    std::vector<std::string> tags, clauses, resources, artifacts;
    double timeout = 0;     // seconds the case may run; 0: the program's (P3.3)
    bool isolated = false;  // runs in a process of its own
    int retries = 0;        // runs again, alone, after failing
    // After it fails: run once more with this environment, within this limit (0: its own).
    std::optional<std::vector<std::pair<std::string, std::string>>> diagnose_env;
    double diagnose_timeout = 0;
    // mtest.toml's skip and expected failure, for a case whose program states neither (reasons).
    std::string skip, expect_fail;
};

// A shared object a case caused to be made (P5.2 `fixture`), and how long making it took.
struct fixture_info {
    std::string name;
    double ms = 0;
};

struct failure_info {
    std::string file, message, expected, actual;
    int line = 0;
    bool fatal = false;
};

struct metric_info {
    std::string name, unit, better = "lower";
    double value = 0;
    std::vector<double> samples;
};

// One case's result. An L0 program is one case whose identifier is the program's name.
struct case_result {
    std::string program, id, kind = "unit";
    std::string status;  // pass fail skip xfail xpass invalid timeout infra flaky
    double ms = 0;
    std::string reason;
    std::vector<failure_info> failures;
    std::vector<metric_info> metrics;
    std::vector<std::string> artifacts, tags, clauses;
    std::vector<fixture_info> fixtures;
    bool quarantined = false;
    int attempts = 1;
    std::string diagnosis;  // the store's file of its diagnostic run's output, when it had one
};

struct program_result {
    std::string name;
    std::vector<std::string> argv;
    std::string cwd;
    int level = 0;
    std::string status;  // pass fail timeout infra
    int exit = 0;
    double ms = 0;
    bool timed_out = false;
    std::string out_tail, err_tail;  // the last part of each output
    std::string error;               // why it did not run, for an infra status
    std::string origin = "config";   // as program_spec's: how the program is named again
    // When it started, in milliseconds after the run began, and what it held: the run's timeline.
    double started_ms = 0;
    std::vector<std::string> resources;
};

struct run_record {
    std::string id;
    std::string started, finished;  // ISO 8601, UTC
    std::string status;             // pass fail infra cancelled
    std::string system, arch, machine, commit, set, selection;
    double ms = 0;
    int jobs = 1;
    std::vector<program_result> programs;
    std::vector<case_result> cases;
    std::map<std::string, int> counts;
};

json to_json(const case_info& c);
json to_json(const failure_info& f);
json to_json(const metric_info& m);
json to_json(const case_result& c);
json to_json(const program_result& p);
json to_json(const run_record& r);  // without programs and cases (run.json)

case_info case_info_from(const json& j);
case_result case_result_from(const json& j);
program_result program_result_from(const json& j);
run_record run_record_from(const json& j);

// A status that fails a run.
bool failing(std::string_view status);
// The counts of a run's cases by status.
std::map<std::string, int> count(const std::vector<case_result>& cases);
// Now, as ISO 8601 in UTC with milliseconds.
std::string now_iso();
// A new run identifier: the time, sortable, and a random part.
std::string new_run_id();

}  // namespace mcpplibs::mtest::engine
