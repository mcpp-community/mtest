// mcpplibs.mtest.e2e: programs under test, the trees they run in, and golden files.
//
//   "prints its version"_test[kind::e2e][artifact{ "target:app" }] = [] {
//       auto r = e2e::run({ .argv = { e2e::program("app"), "--version" } });
//       expect(eq(r.status, 0));
//       expect(e2e::golden("tests/golden/version.txt", r.out));
//   };
//
// A program started here never receives the protocol's variables (P2.2), and an ok image (`.oke`)
// is started by its loader.
export module mcpplibs.mtest.e2e;

import std;
export import mcpplibs.mtest;

export namespace mcpplibs::mtest::e2e {

struct command {
    std::vector<std::string> argv;  // the program, then its arguments
    std::filesystem::path cwd;      // empty: this program's working directory
    std::vector<std::pair<std::string, std::optional<std::string>>>
        env;  // a value sets, nothing removes
    std::string input;
    std::chrono::milliseconds timeout{std::chrono::seconds{60}};
};

struct outcome {
    int status = -1;  // the program's exit code, or the shell's 128 + signal
    bool started = false;
    bool timed_out = false;
    std::string out, err;
    std::chrono::milliseconds duration{0};
    explicit operator bool() const noexcept {
        return started && !timed_out && status == 0;
    }
};

// Runs the command to its end or its timeout. When the case fails afterwards, the
// outputs are kept as the case's artifacts.
outcome run(command c);

// The path of program `name` of the package under test (`MCPP_BIN_<NAME>`, mcpp docs/08),
// else the one PATH names; the name itself when neither does.
std::string program(std::string_view name);

// A directory in the case's temporary directory holding `files` (path, content).
std::filesystem::path
tree(std::initializer_list<std::pair<std::string_view, std::string_view>> files);

// `text` equals the golden file's content. With MTEST_GOLDEN=update the file is
// written instead, and the comparison holds.
comparison golden(const std::filesystem::path& file, std::string_view text);

}  // namespace mcpplibs::mtest::e2e
