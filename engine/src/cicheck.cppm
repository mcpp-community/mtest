// Questions about the runs of all the jobs of a CI run, collected into one store (0.7.0):
// whether every case ran on some host, and how a selection divides among the hosts (design M1, M6).
export module mcpplibs.mtest.engine.cicheck;

import std;
export import mcpplibs.mtest.engine.select;

export namespace mcpplibs::mtest::engine {

struct ci_answer {
    int status = 0;  // 0: holds; 1: the check fails; 2: a usage error
    json data;
    std::string text;
};

// `mtest ci coverage`: each program of `programs` is judged against `runs` -- ran on some host;
// unaffected (absent from a run narrowed to a change); excused or owed ([ci.coverage]); else
// uncovered, which fails. A listed entry that ran or names no program fails too, and so does a
// program that did not pass on a host [ci.coverage] hosts requires. The capability words that no
// run's machine answered present are listed.
ci_answer ci_coverage(const config& c, const std::vector<program_spec>& programs,
                      const std::vector<run_record>& runs);

// `mtest ci plan`: the selection divided among `hosts` by each host's history -- its capability
// answers and the measured time of each case there -- into shards of [ci.plan] shard each.
// `format` "github" also writes `<host>=<[1..N]>` and `<host>_work=<bool>` to GITHUB_OUTPUT.
ci_answer ci_plan(const config& c, const plan& selected, const std::vector<run_record>& runs,
                  const std::vector<std::string>& hosts, std::string_view format);

}  // namespace mcpplibs::mtest::engine
