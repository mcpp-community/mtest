module mcpplibs.mtest.engine.capability;

import std;
import mcpplibs.os;
import mcpplibs.mtest.engine.select;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

// `${NAME}` replaced by the environment's value (empty when unset).
std::string expand(std::string_view text) {
    std::string out;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '$' && i + 1 < text.size() && text[i + 1] == '{') {
            const auto end = text.find('}', i + 2);
            if (end != std::string_view::npos) {
                out += os::env::get(text.substr(i + 2, end - i - 2)).value_or("");
                i = end;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

// Whether some file matches `pattern`: an absolute path whose components may hold `*` and `?`.
bool some_file(const std::string& pattern) {
    const auto p = os::fs::from_utf8(pattern);
    std::vector<std::filesystem::path> at{p.root_path()};
    for (const auto& part : p.relative_path()) {
        const auto word = os::fs::utf8(part);
        std::vector<std::filesystem::path> next;
        for (const auto& dir : at) {
            if (!word.contains('*') && !word.contains('?')) {
                if (std::error_code ec; std::filesystem::exists(dir / part, ec))
                    next.push_back(dir / part);
                continue;
            }
            std::error_code ec;
            for (const auto& e : std::filesystem::directory_iterator(dir, ec))
                if (glob_match(word, os::fs::utf8(e.path().filename())))
                    next.push_back(e.path());
        }
        if (next.empty())
            return false;
        at = std::move(next);
    }
    return !at.empty();
}

capability_answer evaluate(const config& c, const capability_spec& s, int depth);

std::map<std::string, capability_answer, std::less<>>& memo() {
    static std::map<std::string, capability_answer, std::less<>> m;
    return m;
}

capability_answer answer_of(const config& c, std::string_view word, int depth) {
    auto& memo = ::mcpplibs::mtest::engine::memo();
    if (auto it = memo.find(word); it != memo.end())
        return it->second;
    auto spec = c.capabilities.words.find(std::string(word));
    capability_answer a;
    if (spec == c.capabilities.words.end())
        a = {false, "not declared in [capabilities]"};
    else if (depth > 16)
        a = {false, "its `all`/`none` words name one another in a cycle"};
    else
        a = evaluate(c, spec->second, depth + 1);
    memo.emplace(std::string(word), a);
    return a;
}

capability_answer evaluate(const config& c, const capability_spec& s, int depth) {
    const std::string system(os::name(os::host()));
    const std::string arch(os::name(os::ARCH));
    if (!s.systems.empty() && std::ranges::find(s.systems, system) == s.systems.end())
        return {false, std::format("the system is {}", system)};
    if (!s.archs.empty() && std::ranges::find(s.archs, arch) == s.archs.end())
        return {false, std::format("the architecture is {}", arch)};
    for (const auto& [k, want] : s.env) {
        const auto have = os::env::get(k);
        if (want == "*" ? (!have || have->empty()) : have != want)
            return {false, std::format("{} is {}", k, have ? std::format("`{}`", *have) : "unset")};
    }
    for (const auto& w : s.all)
        if (auto a = answer_of(c, w, depth); !a.present)
            return {false, std::format("`{}` is absent ({})", w, a.why)};
    for (const auto& w : s.none)
        if (answer_of(c, w, depth).present)
            return {false, std::format("`{}` is present", w)};
    for (const auto& prog : s.programs)
        if (!os::env::find_executable(expand(prog)))
            return {false, std::format("no {} on PATH", prog)};
    if (!s.paths.empty()) {
        bool any = false;
        for (const auto& g : s.paths)
            if (some_file(expand(g))) {
                any = true;
                break;
            }
        if (!any)
            return {false, std::format("no file matches {}", expand(s.paths.front()))};
    }
    if (!s.command.empty()) {
        os::process::spawn_options o;
        o.program = os::fs::from_utf8(expand(s.command.front()));
        for (std::size_t i = 1; i < s.command.size(); ++i)
            o.args.push_back(expand(s.command[i]));
        o.in = os::process::stdio::null;
        os::process::bounds limits;
        limits.hard = std::chrono::seconds{30};
        auto r = os::process::run(std::move(o), limits);
        if (!r)
            return {false, std::format("{} did not start", s.command.front())};
        if (r->timed_out || !r->status.success())
            return {false,
                    std::format("{} ended with {}", s.command.front(),
                                r->timed_out ? std::string("a timeout")
                                             : std::format("status {}", r->status.shell_code()))};
    }
    return {true, "every test holds"};
}

}  // namespace

std::optional<std::string> capability_of_tag(const config& c, std::string_view tag) {
    if (c.capabilities.words.empty() || c.capabilities.tag.empty() ||
        !tag.starts_with(c.capabilities.tag))
        return std::nullopt;
    return std::string(tag.substr(c.capabilities.tag.size()));
}

capability_answer ask_capability(const config& c, std::string_view word) {
    return answer_of(c, word, 0);
}

void forget_capabilities() {
    memo().clear();
}

std::map<std::string, capability_answer> machine_capabilities(const config& c) {
    std::map<std::string, capability_answer> out;
    for (const auto& [w, _] : c.capabilities.words)
        out[w] = ask_capability(c, w);
    return out;
}

std::string host_name() {
    if (auto h = os::env::get("MTEST_HOST"); h && !h->empty())
        return *h;
    return std::string(os::name(os::host()));
}

}  // namespace mcpplibs::mtest::engine
