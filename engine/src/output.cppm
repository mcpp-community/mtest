// A run as people and other tools read it: the terminal's summary, JUnit XML, a static HTML page,
// and the explanation of one failure (mtest design §6.1, §6.3): where it failed, why, and how to
// repeat it here.
export module mcpplibs.mtest.engine.output;

import std;
export import mcpplibs.mtest.engine.store;

export namespace mcpplibs::mtest::engine {

std::string summary_text(const run_record& r, bool verbose = false);
std::string junit_xml(const run_record& r);
std::string html_page(const run_record& r);

// The explanation of a case of a run: `case_ref` is `program::id`, an `id`, or a glob of either.
std::expected<json, std::string> explain(const store& s, const run_record& r,
                                         std::string_view case_ref);

// The command that repeats one case of a program as the engine ran it: by the program's name when
// the project names it (mtest.toml, a host), by its command when the command line gave it.
std::string reproduce_command(const program_result& program, std::string_view id);
std::string reproduce_command(std::string_view program, std::string_view id);
// `program::id`, or the program alone for the one case of an L0 program.
std::string case_label(std::string_view program, std::string_view id);

}  // namespace mcpplibs::mtest::engine
