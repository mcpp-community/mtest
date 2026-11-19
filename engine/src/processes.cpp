module mcpplibs.mtest.engine.processes;

import std;
import mcpplibs.os;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

std::optional<std::uint64_t> number(std::string_view text, int base = 10) {
    std::uint64_t v = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), v, base);
    if (ec != std::errc{} || end != text.data() + text.size())
        return std::nullopt;
    return v;
}

// A file of /proc without its trailing line ends; empty where it cannot be read, as the kernel's
// stack of a process is to all but the superuser.
std::string proc_text(const std::filesystem::path& file) {
    auto text = os::fs::read(file);
    if (!text)
        return {};
    while (!text->empty() && (text->back() == '\n' || text->back() == ' '))
        text->pop_back();
    return *text;
}

// /proc/<id>/stat: `<id> (<name>) <state> <parent> <group> ...`, user and system time the 12th and
// 13th fields after the name, in ticks of USER_HZ (100), as mcpplibs.os reads a thread's.
struct stat_line {
    std::uint64_t id = 0, parent = 0, group = 0;
    std::string name;
    char state = '?';
    double cpu_ms = 0;
};

std::optional<stat_line> read_stat(const std::filesystem::path& file) {
    const auto text = proc_text(file);
    const auto open = text.find('(');
    const auto close = text.rfind(')');
    if (open == std::string::npos || close == std::string::npos || close < open ||
        close + 2 >= text.size() || open < 2)
        return std::nullopt;
    std::vector<std::string_view> fields;
    for (auto part : std::views::split(std::string_view(text).substr(close + 2), ' '))
        fields.emplace_back(part.begin(), part.end());
    const auto id = number(std::string_view(text).substr(0, open - 1));
    if (fields.size() < 13 || fields[0].empty() || !id)
        return std::nullopt;
    stat_line s;
    s.id = *id;
    s.name = text.substr(open + 1, close - open - 1);
    s.state = fields[0].front();
    s.parent = number(fields[1]).value_or(0);
    s.group = number(fields[2]).value_or(0);
    s.cpu_ms = double(number(fields[11]).value_or(0) + number(fields[12]).value_or(0)) * 10;
    return s;
}

// The kernel's letters for a state (proc(5)), as a person reads them.
std::string state_word(char state) {
    switch (state) {
        case 'R':
            return "running";
        case 'S':
            return "sleeping";
        case 'D':
            return "waiting (uninterruptible)";
        case 'T':
            return "stopped";
        case 't':
            return "traced";
        case 'Z':
            return "zombie";
        case 'X':
            return "dead";
        case 'I':
            return "idle";
        case 'P':
            return "parked";
        default:
            return std::string(1, state);
    }
}

// The kernel function a thread sleeps in; `0` while it runs, or where the kernel does not say.
std::string waiting_in(const std::filesystem::path& dir) {
    auto w = proc_text(dir / "wchan");
    return w == "0" ? std::string{} : w;
}

// What SIGTERM, the request to end on Linux (openkal's kal_process_terminate), does to a process:
// its disposition, which /proc/<id>/status states as masks of the signals ignored and caught.
std::string request_of(const std::filesystem::path& dir) {
    const auto status = proc_text(dir / "status");
    const auto mask = [&](std::string_view key) -> std::uint64_t {
        for (auto part : std::views::split(std::string_view(status), '\n')) {
            std::string_view line(part.begin(), part.end());
            if (!line.starts_with(key))
                continue;
            line.remove_prefix(key.size());
            while (!line.empty() && (line.front() == ':' || line.front() == '\t'))
                line.remove_prefix(1);
            return number(line, 16).value_or(0);
        }
        return 0;
    };
    constexpr std::uint64_t term = std::uint64_t{1} << (15 - 1);
    if (status.empty())
        return {};
    return mask("SigIgn") & term ? "ignored" : mask("SigCgt") & term ? "caught" : "ends";
}

// Its arguments, NUL-separated; a command past 4 KiB keeps its first arguments and says so, so
// that a compiler's line of thousands of words does not become the record.
std::vector<std::string> command_of(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    const auto text = os::fs::read(dir / "cmdline");
    if (!text)
        return out;
    constexpr std::size_t keep = 4096;
    std::size_t kept = 0;
    for (auto part : std::views::split(std::string_view(*text), '\0')) {
        std::string_view word(part.begin(), part.end());
        if (kept + word.size() > keep) {
            out.push_back(std::format("... ({} bytes in all)", text->size()));
            break;
        }
        kept += word.size();
        out.emplace_back(word);
    }
    while (!out.empty() && out.back().empty())
        out.pop_back();
    return out;
}

std::vector<thread_info> threads_of(const std::filesystem::path& dir) {
    std::vector<thread_info> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir / "task", ec), end; !ec && it != end;
         it.increment(ec)) {
        if (auto s = read_stat(it->path() / "stat"))
            out.push_back(
                {s->id, s->name, state_word(s->state), waiting_in(it->path()), s->cpu_ms});
    }
    std::ranges::sort(out, {}, &thread_info::id);
    // One thread is the process itself, which its own fields already state.
    if (out.size() < 2)
        out.clear();
    return out;
}

process_info described(const stat_line& s) {
    const std::filesystem::path dir = std::format("/proc/{}", s.id);
    process_info p;
    p.id = s.id;
    p.parent = s.parent;
    p.group = s.group;
    p.name = s.name;
    p.state = state_word(s.state);
    p.cpu_ms = s.cpu_ms;
    p.command = command_of(dir);
    p.waiting = waiting_in(dir);
    p.request = request_of(dir);
    p.threads = threads_of(dir);
    p.stack = proc_text(dir / "stack");
    return p;
}

// Every process /proc lists, by its stat line.
std::vector<stat_line> process_table() {
    std::vector<stat_line> out;
    std::error_code ec;
    for (std::filesystem::directory_iterator it("/proc", ec), end; !ec && it != end;
         it.increment(ec)) {
        if (!number(os::fs::utf8(it->path().filename())))
            continue;
        if (auto s = read_stat(it->path() / "stat"))
            out.push_back(std::move(*s));
    }
    return out;
}

// The program alone, as mcpplibs.os describes it: its identifier and its processor time.
processes_seen alone(std::uint64_t program) {
    processes_seen r{"program", {}};
    auto d = os::process::describe(program);
    if (!d || d->ended)
        return r;
    process_info p;
    p.id = program;
    p.cpu_ms = double(d->cpu_ns) / 1e6;
    r.processes.push_back(std::move(p));
    return r;
}

}  // namespace

processes_seen processes_of(std::uint64_t program) {
    if (os::host() != os::platform::linux || program == 0)
        return alone(program);
    const auto table = process_table();
    if (table.empty())
        return alone(program);
    std::map<std::uint64_t, const stat_line*> by_id;
    std::multimap<std::uint64_t, std::uint64_t> children;
    for (const auto& s : table) {
        by_id[s.id] = &s;
        children.emplace(s.parent, s.id);
    }
    // The unit is the group the program leads: a process it started stays there after the program
    // has ended and its children are given to another parent. Each is walked with what descends
    // from it, which a process that formed a group of its own does too; the program's own first.
    std::vector<std::uint64_t> pending;
    for (const auto& s : table)
        if (s.group == program && s.id != program)
            pending.push_back(s.id);
    std::ranges::reverse(pending);
    pending.push_back(program);
    processes_seen r{"tree", {}};
    std::set<std::uint64_t> taken;
    while (!pending.empty()) {
        const auto id = pending.back();
        pending.pop_back();
        const auto found = by_id.find(id);
        if (found == by_id.end() || !taken.insert(id).second)
            continue;
        r.processes.push_back(described(*found->second));
        const auto [first, last] = children.equal_range(id);
        std::vector<std::uint64_t> under;
        for (auto c = first; c != last; ++c)
            under.push_back(c->second);
        pending.insert(pending.end(), under.rbegin(), under.rend());
    }
    return r;
}

}  // namespace mcpplibs::mtest::engine
