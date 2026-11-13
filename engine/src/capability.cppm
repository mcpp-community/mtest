// What this machine can do, in the words cases require (`[capabilities]`, docs/config.md; 0.7.0,
// design M1). A word is answered once per process: a probe that starts a program costs a start,
// and a run asks the same words for every case.
export module mcpplibs.mtest.engine.capability;

import std;
export import mcpplibs.mtest.engine.config;

export namespace mcpplibs::mtest::engine {

struct capability_answer {
    bool present = false;
    std::string why;  // the test that decided it, in a person's words
};

// The word `tag` names, when it is a capability tag of `c` (`requires:gcc` -> `gcc`).
std::optional<std::string> capability_of_tag(const config& c, std::string_view tag);
// How this machine answers `word`; a word `c` does not declare is absent, and says so.
capability_answer ask_capability(const config& c, std::string_view word);
// Forgets every answer: each command asks afresh, so that a long-running service (`mtest serve`)
// does not keep the answer of a declaration since edited or of a program since installed.
void forget_capabilities();
// Every declared word, answered.
std::map<std::string, capability_answer> machine_capabilities(const config& c);
// The name of this machine among the hosts of CI: MTEST_HOST, else the system's name.
std::string host_name();

}  // namespace mcpplibs::mtest::engine
