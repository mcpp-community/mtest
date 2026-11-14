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
    // The bytes of each output the outcome keeps; what is past them is read and dropped, so the
    // program never waits on a full pipe (0.8.5).
    std::size_t out_limit = std::size_t{256} << 20;
    std::size_t err_limit = std::size_t{1} << 20;
};

struct outcome {
    int status = -1;  // the program's exit code, or the shell's 128 + signal
    bool started = false;
    bool timed_out = false;
    // An output longer than its limit (`command::out_limit`, `err_limit`) is kept to the limit,
    // and says so; `held_open`: a process the program started still held an output open after it
    // ended (0.8.5).
    bool out_truncated = false;
    bool err_truncated = false;
    bool held_open = false;
    std::string out, err;
    std::chrono::milliseconds duration{0};
    std::string line;  // the command as a POSIX shell line, to repeat it by hand (0.7.0)
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

// --- Matchers (0.7.0). Each is a comparison: a failure reports both sides, and one about an
// outcome also the command line that repeats it and the tails of both outputs.

// The command as one line for a POSIX shell: each word single-quoted where it needs it, the
// environment it sets first, a working directory as a leading `cd`.
std::string shell_line(const command& c);
// `text` contains `needle`.
comparison contains(std::string_view text, std::string_view needle);
// Some part of `text` matches the ECMAScript regular expression `pattern`.
comparison matches(std::string_view text, std::string_view pattern);
// The program started, ended in time, with status 0.
comparison succeeded(const outcome& r);
// The program started, ended in time, with status `status`.
comparison exited(const outcome& r, int status);

// --- An interactive session (0.7.0, M4): a program whose input is written while its output is
// read, by expecting a pattern and sending a reply. Both streams go through one pipe, in the
// order written; no terminal is involved, so the same code drives the program on every system.
// A serial console, a debugger, a read-eval-print loop and a login are driven this way.
class session {
public:
    // Starts the command; `c.input` is sent first. A program that did not start leaves a session
    // whose `started()` is false and whose transcript says why.
    static session start(command c);

    session() = default;
    session(session&&) noexcept;
    session& operator=(session&&) noexcept;
    session(const session&) = delete;
    session& operator=(const session&) = delete;
    ~session();  // ends the program if it still runs

    bool started() const noexcept;
    // Waits up to `timeout` for `pattern` (ECMAScript) in what was written since the last match,
    // and consumes up to the end of the match. Returns what was consumed; nothing when the time
    // ran out or the output ended first, which `timed_out()` and `ended()` then tell apart.
    std::optional<std::string> expect(std::string_view pattern,
                                      std::chrono::milliseconds timeout = std::chrono::seconds{30});
    void send(std::string_view bytes);
    void send_line(std::string_view line);  // `line` and "\n"
    void close_input();
    // The rest of the output, and the status, within `timeout`; the program is ended after it.
    outcome finish(std::chrono::milliseconds timeout = std::chrono::seconds{60});

    bool timed_out() const noexcept;
    bool ended() const noexcept;
    // Everything the program wrote so far, and what was sent, marked: a failure's record.
    const std::string& transcript() const noexcept;
    // What was written since the last match, not yet consumed.
    std::string_view pending() const noexcept;
    // `expect(pattern)` held, as a comparison whose failure carries the pending output.
    comparison saw(std::string_view pattern,
                   std::chrono::milliseconds timeout = std::chrono::seconds{30});
private:
    struct state;
    std::unique_ptr<state> s_;
};

}  // namespace mcpplibs::mtest::e2e
