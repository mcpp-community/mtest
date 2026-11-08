// Running one program, by the protocol when it speaks it (spec/protocol.md) and by its exit status
// when it does not, and reading what it reported into results.
export module mcpplibs.mtest.engine.exec;

import std;
export import mcpplibs.mtest.engine.model;

export namespace mcpplibs::mtest::engine {

// The protocol's level a program declares by its marker (P2.3): the files of its command are read
// for `mtest-protocol=1`; 3 when found, 0 when not. A spec that states its level is not read.
int detect_level(const program_spec& spec);

struct run_request {
    std::filesystem::path work;  // a directory of this run of this program (report, selection, tmp)
    std::filesystem::path root;  // the project, for a relative cwd
    std::vector<std::string> select;  // case identifiers; empty: every case
    std::optional<std::uint64_t> seed;
    std::vector<std::pair<std::string, std::string>> env;  // added to the spec's
    std::vector<std::string> run_under;  // a prefix of the command (a tracer, a wrapper)
    // P10: how closely use is measured (off, basic, detailed, trace). A program that does not speak
    // the protocol is measured from a process of the engine's own that starts it (`mtest measure`),
    // where the system accounts a process's children (Linux, macOS).
    std::string monitor = "basic";
    int sample_ms = 50;  // `trace`
};

struct run_outcome {
    program_result program;
    std::vector<case_result> cases;
    std::string out, err;  // the whole outputs, for a failure pack
};

// Lists the cases of a program at level 1 or more (P3); an L0 program has one case, its own name.
std::expected<std::vector<case_info>, std::string>
list_program(const program_spec& spec, const std::filesystem::path& work,
             const std::filesystem::path& root, std::vector<component_info>* components = nullptr);

// `mtest measure --out FILE -- ARGV`: runs ARGV with this process's streams, writes what it used
// (its children's account) to FILE as JSON, and ends with its status.
int measure(const std::filesystem::path& out, std::vector<std::string> argv);
// Runs it once.
run_outcome run_program(const program_spec& spec, const run_request& request);

// The events of a report, read into the results of `program` (P5; P5.4 for a report a crash ended).
struct report_reading {
    std::vector<case_info> listed;
    std::vector<case_result> cases;
    int level = 0;
    bool done = false;
    std::vector<std::string> problems;       // lines that are not events, events out of order
    std::vector<component_info> components;  // P11, in a listing
    std::optional<usage_info> usage;         // P10, the program's own
};
report_reading read_report(std::string_view text, std::string_view program);

}  // namespace mcpplibs::mtest::engine
