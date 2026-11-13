module mcpplibs.mtest.e2e;

import std;
import mcpplibs.os;

namespace mcpplibs::mtest::e2e {

namespace os = mcpplibs::os;

namespace {
constexpr std::string_view protocol_variables[] = {"MTEST_PROTOCOL",  "MTEST_MODE",
                                                   "MTEST_REPORT",    "MTEST_SELECT",
                                                   "MTEST_ISOLATION", "MTEST_TMPDIR",
                                                   "MTEST_SEED"};
}

outcome run(command c) {
    outcome r;
    if (c.argv.empty())
        return r;
    // An ok image (`.oke`) is started by a loader: this program's own when it is an image, else
    // `okrun`.
    if (c.argv.front().ends_with(".oke")) {
        auto loader = os::env::loader();
        if (!loader)
            loader = os::env::find_executable("okrun");
        if (loader)
            c.argv.insert(c.argv.begin(), os::fs::utf8(*loader));
    }
    r.line = shell_line(c);
    os::process::spawn_options o;
    o.program = os::fs::from_utf8(c.argv.front());
    o.args.assign(c.argv.begin() + 1, c.argv.end());
    o.cwd = c.cwd;
    for (auto v : protocol_variables)
        o.env.emplace_back(std::string(v), std::nullopt);
    for (auto& kv : c.env)
        o.env.push_back(std::move(kv));
    o.in = c.input.empty() ? os::process::stdio::null : os::process::stdio::pipe;
    os::process::bounds limits;
    limits.hard = c.timeout;
    auto done = os::process::run(std::move(o), limits, c.input);
    if (!done) {
        r.err = std::format("{} did not start: {}", c.argv.front(), done.error().message());
        return r;
    }
    r.started = true;
    r.status = done->status.shell_code();
    r.timed_out = done->timed_out;
    r.out = std::move(done->out);
    r.err = std::move(done->err);
    r.duration = done->duration;
    return r;
}

std::string program(std::string_view name) {
    std::string key = "MCPP_BIN_";
    for (char ch : name)
        key += std::isalnum(static_cast<unsigned char>(ch))
                   ? char(std::toupper(static_cast<unsigned char>(ch)))
                   : '_';
    if (auto v = os::env::get(key); v && !v->empty())
        return *v;
    if (auto p = os::env::find_executable(name))
        return os::fs::utf8(*p);
    return std::string(name);
}

std::filesystem::path
tree(std::initializer_list<std::pair<std::string_view, std::string_view>> files) {
    static int trees = 0;
    const auto root = temp_dir() / std::format("tree-{}", ++trees);
    for (const auto& [path, content] : files) {
        const auto file = root / os::fs::from_utf8(path);
        std::filesystem::create_directories(file.parent_path());
        if (auto w = os::fs::write(file, content); !w)
            fail(std::format("could not write {}: {}", os::fs::utf8(file), w.error().message()));
    }
    std::filesystem::create_directories(root);
    return root;
}

comparison golden(const std::filesystem::path& file, std::string_view text) {
    comparison c;
    if (os::env::get("MTEST_GOLDEN").value_or("") == "update") {
        std::filesystem::create_directories(file.parent_path());
        c.held = os::fs::write(file, text).has_value();
        if (!c.held)
            c.expression = std::format("could not write {}", os::fs::utf8(file));
        return c;
    }
    auto have = os::fs::read(file);
    if (!have) {
        c.expression =
            std::format("no golden file {} (MTEST_GOLDEN=update writes it)", os::fs::utf8(file));
        return c;
    }
    c.held = *have == text;
    if (!c.held) {
        std::size_t at = 0;
        while (at < have->size() && at < text.size() && (*have)[at] == text[at])
            ++at;
        const auto line = 1 + std::count(have->begin(), have->begin() + std::ptrdiff_t(at), '\n');
        c.expected = have->substr(at > 40 ? at - 40 : 0, 120);
        c.actual = std::string(text.substr(at > 40 ? at - 40 : 0, 120));
        c.expression = std::format("differs from {} at line {} (MTEST_GOLDEN=update rewrites it)",
                                   os::fs::utf8(file), line);
    }
    return c;
}

namespace {

std::string shell_word(std::string_view word) {
    const bool plain = !word.empty() && std::ranges::all_of(word, [](char ch) {
        return std::isalnum(static_cast<unsigned char>(ch)) ||
               std::string_view("@%+=:,./_-").contains(ch);
    });
    if (plain)
        return std::string(word);
    std::string q = "'";
    for (char ch : word)
        q += ch == '\'' ? std::string("'\\''") : std::string(1, ch);
    return q + "'";
}

std::string tail(std::string_view text, std::size_t n = 2000) {
    return std::string(text.size() > n ? text.substr(text.size() - n) : text);
}

comparison about(const outcome& r, bool held, std::string expression, std::string expected) {
    comparison c;
    c.held = held;
    c.expression = std::move(expression);
    c.expected = std::move(expected);
    if (!r.started)
        c.actual = std::format("did not start: {}", r.err);
    else if (r.timed_out)
        c.actual = std::format("timed out after {} ms", r.duration.count());
    else
        c.actual = std::format("status {}", r.status);
    if (!held)
        c.actual += std::
            format("\n--- command ---\n{}\n--- stdout (tail) ---\n{}\n--- stderr (tail) ---\n{}",
                   r.line, tail(r.out), tail(r.err));
    return c;
}

}  // namespace

std::string shell_line(const command& c) {
    std::string line;
    if (!c.cwd.empty())
        line += std::format("cd {} && ", shell_word(os::fs::utf8(c.cwd)));
    for (const auto& [k, v] : c.env)
        line += v ? std::format("{}={} ", k, shell_word(*v)) : std::format("unset {}; ", k);
    for (std::size_t i = 0; i < c.argv.size(); ++i)
        line += (i ? " " : "") + shell_word(c.argv[i]);
    if (!c.input.empty())
        line +=
            " <<'MTEST_INPUT'\n" + c.input + (c.input.ends_with('\n') ? "" : "\n") + "MTEST_INPUT";
    return line;
}

comparison contains(std::string_view text, std::string_view needle) {
    comparison c;
    c.held = text.contains(needle);
    c.expression = "contains";
    c.expected = std::format("text containing \"{}\"", needle);
    c.actual = tail(text);
    return c;
}

comparison matches(std::string_view text, std::string_view pattern) {
    comparison c;
    c.expression = "matches";
    c.expected = std::format("text matching /{}/", pattern);
    c.actual = tail(text);
    try {
        const std::regex rx(pattern.begin(), pattern.end(), std::regex::ECMAScript);
        c.held = std::regex_search(text.begin(), text.end(), rx);
    } catch (const std::regex_error& e) {
        c.expected = std::format("a valid pattern /{}/ ({})", pattern, e.what());
    }
    return c;
}

comparison succeeded(const outcome& r) {
    return about(r, bool(r), "succeeded", "status 0");
}

comparison exited(const outcome& r, int status) {
    return about(r, r.started && !r.timed_out && r.status == status, "exited",
                 std::format("status {}", status));
}

// --- session -----------------------------------------------------------------------------------

struct session::state {
    os::process::process proc;
    command cmd;
    std::string line;
    std::string buffer;      // written, not yet consumed
    std::string transcript;  // everything, with what was sent marked
    bool started = false, timed_out = false, ended = false;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();

    // Reads what arrives within `budget`; false at the end of the output.
    bool pull(std::chrono::milliseconds budget) {
        if (ended)
            return false;
        auto got = proc.read_output_for(budget);
        if (!got) {
            ended = true;
            transcript += std::format("\n[mtest: reading failed: {}]\n", got.error().message());
            return false;
        }
        if (!*got)
            return true;  // the slice elapsed
        if ((*got)->empty()) {
            ended = true;
            return false;
        }
        buffer += **got;
        transcript += **got;
        return true;
    }
};

session session::start(command c) {
    session s;
    s.s_ = std::make_unique<state>();
    auto& st = *s.s_;
    st.line = shell_line(c);
    if (c.argv.empty()) {
        st.transcript = "[mtest: no command]\n";
        return s;
    }
    os::process::spawn_options o;
    o.program = os::fs::from_utf8(c.argv.front());
    o.args.assign(c.argv.begin() + 1, c.argv.end());
    o.cwd = c.cwd;
    for (auto v : protocol_variables)
        o.env.emplace_back(std::string(v), std::nullopt);
    for (auto& kv : c.env)
        o.env.push_back(kv);
    o.in = os::process::stdio::pipe;
    o.out = os::process::stdio::pipe;
    o.err = os::process::stdio::output;
    auto p = os::process::process::spawn(o);
    if (!p) {
        st.transcript =
            std::format("[mtest: {} did not start: {}]\n", c.argv.front(), p.error().message());
        return s;
    }
    st.proc = std::move(*p);
    st.started = true;
    st.cmd = std::move(c);
    if (!st.cmd.input.empty())
        s.send(st.cmd.input);
    return s;
}

session::session(session&&) noexcept = default;
session& session::operator=(session&&) noexcept = default;
session::~session() {
    if (s_ && s_->started && s_->proc.valid()) {
        s_->proc.kill();
        (void)s_->proc.wait();
    }
}

bool session::started() const noexcept {
    return s_ && s_->started;
}
bool session::timed_out() const noexcept {
    return s_ && s_->timed_out;
}
bool session::ended() const noexcept {
    return !s_ || s_->ended;
}
const std::string& session::transcript() const noexcept {
    static const std::string none;
    return s_ ? s_->transcript : none;
}
std::string_view session::pending() const noexcept {
    return s_ ? std::string_view(s_->buffer) : std::string_view();
}

std::optional<std::string> session::expect(std::string_view pattern,
                                           std::chrono::milliseconds timeout) {
    if (!started())
        return std::nullopt;
    auto& st = *s_;
    st.timed_out = false;
    std::regex rx;
    try {
        rx = std::regex(pattern.begin(), pattern.end(), std::regex::ECMAScript);
    } catch (const std::regex_error& e) {
        st.transcript += std::format("\n[mtest: invalid pattern /{}/: {}]\n", pattern, e.what());
        return std::nullopt;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        std::smatch m;
        if (std::regex_search(st.buffer, m, rx)) {
            const auto end = std::size_t(m.position(0) + m.length(0));
            std::string consumed = st.buffer.substr(0, end);
            st.buffer.erase(0, end);
            return consumed;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            st.timed_out = true;
            return std::nullopt;
        }
        const auto slice =
            std::min<std::chrono::milliseconds>(std::chrono::duration_cast<
                                                    std::chrono::milliseconds>(deadline - now),
                                                std::chrono::milliseconds{200});
        if (!st.pull(slice))
            return std::nullopt;
    }
}

void session::send(std::string_view bytes) {
    if (!started())
        return;
    s_->transcript += std::format("\n[sent] {}\n", bytes);
    if (auto w = s_->proc.write(bytes); !w)
        s_->transcript += std::format("[mtest: writing failed: {}]\n", w.error().message());
}

void session::send_line(std::string_view line) {
    send(std::string(line) + "\n");
}

void session::close_input() {
    if (started())
        s_->proc.close_input();
}

outcome session::finish(std::chrono::milliseconds timeout) {
    outcome r;
    if (!s_)
        return r;
    auto& st = *s_;
    r.line = st.line;
    r.started = st.started;
    if (!st.started) {
        r.err = st.transcript;
        return r;
    }
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!st.ended && std::chrono::steady_clock::now() < deadline)
        st.pull(std::chrono::milliseconds{200});
    auto status = st.proc.wait_for(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::max(deadline - std::chrono::steady_clock::now(),
                 std::chrono::steady_clock::duration{})));
    if (status && *status) {
        r.status = (*status)->shell_code();
    } else {
        r.timed_out = true;
        st.timed_out = true;
        st.proc.kill();
        (void)st.proc.wait();
    }
    r.out = st.transcript;
    r.duration = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - st.began);
    return r;
}

comparison session::saw(std::string_view pattern, std::chrono::milliseconds timeout) {
    comparison c;
    c.expression = "saw";
    c.expected = std::format("output matching /{}/ within {} ms", pattern, timeout.count());
    c.held = expect(pattern, timeout).has_value();
    if (!c.held)
        c.actual = std::format("{}; not yet consumed:\n{}\n--- command ---\n{}",
                               timed_out() ? "the time ran out"
                                           : (ended() ? "the output ended" : "did not start"),
                               tail(pending()), s_ ? s_->line : std::string());
    return c;
}

}  // namespace mcpplibs::mtest::e2e
