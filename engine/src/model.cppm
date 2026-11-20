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
    // At its timeout it is asked to end, and what still runs `grace` later is ended (0.10.0); the
    // default is the grace of mcpplibs.os's bounded run, which the engine gave through 0.9.0. With
    // `timeout_record`, its processes are recorded at the timeout and after the grace.
    std::chrono::milliseconds grace{std::chrono::seconds{2}};
    bool timeout_record = true;
    std::string isolation = "process";  // "process" or "case"
    std::string origin = "config";      // "given" (the command line), "config", "discover", "host"
    // The kind of an L0 program's one case and of a case that states none: stated in mtest.toml,
    // else by the program's directory (docs/layout.md), else unit. Empty until `discover` gives it.
    std::string kind;
    // Whether the engine may divide its cases among several processes of it (`[run] split`).
    bool split = true;
    // The exit status by which a program that does not speak the protocol says its one case did
    // not apply here (`skip_exit`, P1.1); negative: it states none, and every status but 0 fails.
    int skip_exit = -1;
    // When a host built it, in milliseconds after the run began, and for how long (a build edge of
    // the run's graph); negative: not built by this run.
    double build_started_ms = -1, build_ms = 0;
};

// What a program used of the machine (P10): its own report (`usage` event), or what the
// engine measured of its process.
struct usage_info {
    double user_ms = 0, sys_ms = 0;
    std::optional<double> peak_bytes, read_bytes, written_bytes;
    std::string source;  // "program" (its own report) or "engine" (measured from outside)
};

// A run fixture a program serves (P11), and a pool of a resource with a capacity (P7.3).
struct component_info {
    std::string name;     // a component's name, or empty for a fixture's own entry
    std::string fixture;  // a run fixture it serves
    std::vector<std::pair<std::string, int>> pools;
};

// A limit on a metric a case states (P6.5).
struct bound_info {
    std::string metric;
    std::optional<double> max, min;
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
    std::vector<bound_info> bounds;
    std::vector<bound_info>
        stated_bounds;  // mtest.toml's, which the engine judges (the program judges its own)
    std::vector<std::string> uses;  // run fixtures (P11)
    // Where it runs (0.8.0): the platforms and architectures it runs only on, and those it does
    // not run on; the capabilities its program answered, and why the program lists it skipped.
    std::vector<std::string> only, except;
    std::map<std::string, bool> capabilities;
    std::string reason;
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
    std::string precision;  // exact, process, approx (P6.6); empty: as the case stated it
    double value = 0;
    std::vector<double> samples;
};

// A step of a case (P5.6, 0.11.0): a command an end-to-end case ran.
struct step_info {
    std::string name;
    double t = 0, ms = 0;
    std::optional<double> cpu_ms;
    std::optional<int> status;
    std::string build;  // the record of the build the step was, when it was one
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
    std::vector<std::string> only, except;  // where it runs, as listed (0.8.0)
    std::vector<fixture_info> fixtures;
    std::vector<bound_info> bounds;  // as listed, for `analyze trend`
    bool quarantined = false;
    int attempts = 1;
    std::string diagnosis;  // the store's file of its diagnostic run's output, when it had one
    // When it began, in milliseconds after the run began (its program's start and its `begin`'s
    // `t`); negative when not known.
    double started_ms = -1;
    // Its processor time: the `cpu.user` and `cpu.sys` metrics it reported (P6.6), else the time
    // its program used while it ran alone in it; negative when not known (0.11.0).
    double cpu_ms = -1;
    std::vector<step_info> steps;       // P5.6 (0.11.0)
    std::vector<std::string> exercised;  // the items of the product's surface it used, sorted
};

// A thread of a process, as the engine saw it at a timeout (0.10.0).
struct thread_info {
    std::uint64_t id = 0;
    std::string name, state;
    std::string waiting;  // the kernel function it sleeps in (Linux: wchan); empty: not told
    double cpu_ms = 0;
};

// A process of a program's unit, as the engine saw it at a timeout (0.10.0). What the system does
// not tell, or does not let the engine read, is empty.
struct process_info {
    std::uint64_t id = 0, parent = 0, group = 0;
    std::string name;
    std::vector<std::string> command;
    std::string state;    // running, sleeping, waiting (uninterruptible), stopped, zombie, ...
    std::string waiting;  // as its first thread's
    std::string request;  // what the request to end does to it: ends, caught, ignored
    double cpu_ms = 0;
    std::vector<thread_info> threads;  // when it has more than one
    std::string stack;                 // the kernel's stack of its first thread, where readable
};

// How a program past its timeout was ended (0.10.0): its processes were recorded, it was asked to
// end, and what still ran after its grace was recorded and ended.
struct timeout_info {
    double limit_ms = 0, grace_ms = 0;
    std::string ended;  // "requested": it ended within its grace; "terminated": ended after it
    // "tree": the program, the processes it started and the rest of its unit (Linux); "program":
    // the program alone, where the system lists no other process to the engine; empty: not recorded
    // (`timeout_record = false`).
    std::string seen;
    std::vector<process_info> processes;  // at the timeout, before the request
    std::vector<process_info> remaining;  // after the grace, before the unit was ended
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
    std::optional<timeout_info> timeout;  // how it was ended at its timeout, and its processes
    std::string out_tail, err_tail;       // the last part of each output
    std::string error;                    // why it did not run, for an infra status
    std::string origin = "config";        // as program_spec's: how the program is named again
    // When it started, in milliseconds after the run began, and what it held: the run's timeline.
    double started_ms = 0;
    std::vector<std::string> resources;
    int worker = -1;                             // the engine's worker that ran it
    std::vector<std::string> select;             // the cases this process ran; empty: all
    std::vector<std::string> fixtures;           // the run fixtures its cases used
    double build_started_ms = -1, build_ms = 0;  // the build edge, as program_spec's
    std::optional<usage_info> usage;
};

// A run fixture as it was served in a run: a node of the run's graph.
struct fixture_run {
    std::string name, program;
    double started_ms = 0, ready_ms = 0, ended_ms = 0;
    std::string status;  // ready, failed
};

struct run_record {
    std::string id;
    std::string started, finished;  // ISO 8601, UTC
    std::string status;             // pass fail infra cancelled
    // `machine`: the machine's class (P12.4, 0.11.0), the processor's model, else
    // `<arch>/<processors>`; durations are compared within a class.
    std::string system, arch, machine, commit, set, selection;
    int processors = 0;
    std::uint64_t memory = 0;
    double ms = 0;
    int jobs = 1;
    std::vector<program_result> programs;
    std::vector<case_result> cases;
    std::vector<fixture_run> fixtures;
    std::map<std::string, int> counts;
    // Runs of one group are parts of one whole (the jobs of a CI run, MTEST_GROUP), reported
    // together.
    std::string group;
    std::string monitor = "basic";  // the level the run measured at (P10)
    // The machine among the hosts of CI (MTEST_HOST, else the system), and how it answered each
    // declared capability (0.7.0): what `mtest ci coverage` and `mtest ci plan` read.
    std::string host;
    std::map<std::string, bool> capabilities;
    // The run whose results this one carries for the cases it did not run again (`--resume`).
    std::string resumed;
};

// This machine's class (P12.4): the processor's model as the system names it, else the
// architecture and the number of processors.
std::string machine_class();

// JSON as text, a byte that is not UTF-8 (a program's output in a Windows code page) written as
// U+FFFD rather than refused.
std::string dumped(const json& j, int indent = -1);
json to_json(const case_info& c);
json to_json(const std::vector<bound_info>& bounds);
std::vector<bound_info> bounds_from(const json& j);
json to_json(const failure_info& f);
json to_json(const metric_info& m);
json to_json(const case_result& c);
json to_json(const timeout_info& t);
json to_json(const program_result& p);
json to_json(const run_record& r);  // without programs and cases (run.json)
json to_json(const fixture_run& f);
fixture_run fixture_run_from(const json& j);

case_info case_info_from(const json& j);
step_info step_from(const json& j);
case_result case_result_from(const json& j);
timeout_info timeout_info_from(const json& j);
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
