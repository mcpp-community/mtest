// Running a plan: units in parallel by their resources (spec/protocol.md P7.1) and their history,
// the longest first; retries for the cases a project marks unstable; quarantine; failure packs;
// progress.
export module mcpplibs.mtest.engine.schedule;

import std;
export import mcpplibs.mtest.engine.select;

export namespace mcpplibs::mtest::engine {

struct execute_options {
    int jobs = 0;  // 0: the machine's processors
    std::optional<std::uint64_t> seed;
    std::vector<std::string> run_under;  // a prefix of every program's command (a tracer)
    std::vector<std::pair<std::string, std::string>> env;  // given to every program
    bool fail_fast = false;                                // nothing more starts after a failure
    int retries = -1;                                      // -1: the configuration's
    std::string set, selection;  // what the run was asked for, for its record
    std::string id;              // empty: a new identifier
    // Called for each event of the run, from the threads that run it: run/started, case/finished,
    // program/finished, run/finished (the service streams them; docs/service.md).
    std::function<void(const json&)> on_event;
};

run_record execute(const config& c, const store& s, const plan& p, const execute_options& o);

}  // namespace mcpplibs::mtest::engine
