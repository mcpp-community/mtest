// The runtime of mcpplibs.mtest: the program's cases, the runner's requests
// (spec/protocol.md P2) and the two reports, the events and the terminal's.
module mcpplibs.mtest;

import std;
import mcpplibs.os;

namespace mcpplibs::mtest {

namespace os = mcpplibs::os;

// P2.3: a runner that reads this program's file finds the protocol here.
constinit const char protocol_marker[] = "mtest-protocol=1";

namespace {

using clock = std::chrono::steady_clock;

struct fatal_signal {};
struct skip_signal {
    std::string reason;
};

struct running_case {
    std::string id;
    bool scope = false;  // a named suite: a prefix of identifiers, not a case
    bool failed = false;
    std::optional<std::filesystem::path> dir;
};

struct state {
    std::recursive_mutex lock;
    const clock::time_point began = clock::now();
    bool protocol = false;
    bool listing = false;
    std::optional<std::set<std::string>> selection;
    std::set<std::string> seen;
    std::ofstream events;
    std::filesystem::path tmp_root;
    std::uint64_t seed = 0;
    bool seed_given = false;
    std::vector<running_case> stack;
    std::vector<std::pair<std::string, std::function<void()>>> suites;
    std::size_t suites_run = 0;
    std::map<std::string, int> counts;
    std::set<std::string> clauses;
    bool any_failed = false;
    bool program_written = false;
    bool done = false;

    state() {
        const auto protocol_version = os::env::get("MTEST_PROTOCOL");
        const auto report = os::env::get("MTEST_REPORT");
        protocol = protocol_version && !protocol_version->empty() && report && !report->empty();
        if (protocol) {
            events.open(os::fs::from_utf8(*report), std::ios::app | std::ios::binary);
            listing = os::env::get("MTEST_MODE").value_or("run") == "list";
            if (auto select = os::env::get("MTEST_SELECT"); select && !select->empty()) {
                std::set<std::string> ids;
                if (auto text = os::fs::read(os::fs::from_utf8(*select))) {
                    std::string_view rest = *text;
                    while (!rest.empty()) {
                        auto nl = rest.find('\n');
                        auto line = rest.substr(0, nl);
                        if (line.ends_with('\r'))
                            line.remove_suffix(1);
                        if (!line.empty())
                            ids.emplace(line);
                        rest =
                            nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
                    }
                }
                selection = std::move(ids);
            }
        }
        if (auto t = os::env::get("MTEST_TMPDIR"); t && !t->empty())
            tmp_root = os::fs::from_utf8(*t);
        if (auto s = os::env::get("MTEST_SEED"); s && !s->empty()) {
            std::uint64_t v = 0;
            auto [p, ec] = std::from_chars(s->data(), s->data() + s->size(), v);
            if (ec == std::errc{} && p == s->data() + s->size()) {
                seed = v;
                seed_given = true;
            }
        }
        if (!seed_given)
            seed = (std::uint64_t(std::random_device{}()) << 32) ^ std::random_device{}();
    }
};

state& the() {
    static state s;
    return s;
}

// JSON string escaping for the events (RFC 8259 §7).
std::string json_string(std::string_view text) {
    std::string out;
    out.reserve(text.size() + 2);
    out += '"';
    for (unsigned char c : text) {
        switch (c) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (c < 0x20)
                    out += std::format("\\u{:04x}", unsigned(c));
                else
                    out += char(c);
        }
    }
    out += '"';
    return out;
}

std::string list_of(const std::vector<std::string>& items) {
    std::string out = "[";
    for (std::size_t i = 0; i < items.size(); ++i)
        out += (i ? "," : "") + json_string(items[i]);
    return out + "]";
}

double since_start() {
    return std::chrono::duration<double, std::milli>(clock::now() - the().began).count();
}

void write_event(const std::string& body) {
    auto& s = the();
    if (!s.protocol || !s.events)
        return;
    s.events << "{" << body << "}\n";
    s.events.flush();
}

void write_program() {
    auto& s = the();
    if (s.program_written)
        return;
    s.program_written = true;
    // Keeps the marker in a link that discards what nothing reaches.
    [[maybe_unused]] volatile char keep = protocol_marker[0];
    write_event(std::format(
        R"("e":"program","protocol":1,"level":3,"framework":"mcpplibs.mtest","version":"0.1.0","isolation":["process","case"])"));
}

std::string_view kind_name(kind k) {
    switch (k) {
        case kind::unit:
            return "unit";
        case kind::e2e:
            return "e2e";
        case kind::cts:
            return "cts";
        case kind::bench:
            return "bench";
    }
    return "unit";
}

std::string full_id(std::string_view name) {
    auto& s = the();
    return s.stack.empty() ? std::string(name) : s.stack.back().id + "/" + std::string(name);
}

// A case runs when it is selected, or is an ancestor or a descendant of a selected case (P4).
bool selected(const std::string& id) {
    auto& s = the();
    if (!s.selection)
        return true;
    for (const auto& want : *s.selection) {
        if (want == id)
            return true;
        if (want.starts_with(id) && want.size() > id.size() && want[id.size()] == '/')
            return true;
        if (id.starts_with(want) && id.size() > want.size() && id[want.size()] == '/')
            return true;
    }
    return false;
}

std::string sanitized(std::string_view id) {
    std::string out;
    for (char c : id)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    if (out.size() > 80)
        out.resize(80);
    return out;
}

void say(std::string_view line) {
    std::println("{}", line);
    std::cout.flush();
}

void record_failure(std::string_view message, const std::source_location& where,
                    const comparison* value, bool is_fatal) {
    auto& s = the();
    std::lock_guard g(s.lock);
    const bool in_case = !s.stack.empty() && !s.stack.back().scope;
    const std::string id = in_case ? s.stack.back().id : std::string{};
    if (in_case)
        s.stack.back().failed = true;
    else
        s.any_failed = true;
    const std::string file = where.file_name();
    std::string text = std::string(message);
    if (value && !value->expression.empty())
        text = text.empty() ? value->expression : value->expression + ": " + text;
    if (text.empty())
        text = "the expectation did not hold";
    say(std::format("    {}:{}: {}", file, where.line(), text));
    std::string ev =
        std::format(R"("e":"failure","id":{},"file":{},"line":{},"message":{})", json_string(id),
                    json_string(file), where.line(), json_string(text));
    if (value && !value->expected.empty())
        ev += ",\"expected\":" + json_string(value->expected) +
              ",\"actual\":" + json_string(value->actual);
    if (is_fatal)
        ev += ",\"fatal\":true";
    write_event(ev);
}

void run_suites() {
    auto& s = the();
    while (s.suites_run < s.suites.size()) {
        auto [name, f] = s.suites[s.suites_run++];
        if (!name.empty())
            s.stack.push_back({full_id(name), true});
        try {
            f();
        } catch (const std::exception& e) {
            record_failure(std::format("an exception escaped the suite: {}", e.what()),
                           std::source_location::current(), nullptr, false);
        } catch (...) {
            record_failure("an exception escaped the suite", std::source_location::current(),
                           nullptr, false);
        }
        if (!name.empty())
            s.stack.pop_back();
    }
}

}  // namespace

// ---- expectations ------------------------------------------------------------

comparison contains(std::string_view text, std::string_view part) {
    comparison c;
    c.held = text.find(part) != std::string_view::npos;
    if (!c.held) {
        c.actual = detail::show(text);
        c.expected = detail::show(part);
        c.expression =
            std::format("{} does not contain {}",
                        c.actual.size() > 200 ? c.actual.substr(0, 200) + "..." : c.actual,
                        c.expected);
    }
    return c;
}

expectation::expectation(comparison value, bool is_fatal, std::source_location where)
    : held_(value.held), fatal_(is_fatal), value_(std::move(value)), where_(where) {}

expectation::~expectation() noexcept(false) {
    if (held_)
        return;
    record_failure(message_, where_, &value_, fatal_);
    if (fatal_ && std::uncaught_exceptions() == 0)
        throw fatal_signal{};
}

expectation expect(bool held, std::source_location where) {
    comparison c;
    c.held = held;
    return expectation(std::move(c), false, where);
}
expectation expect(comparison value, std::source_location where) {
    return expectation(std::move(value), false, where);
}
expectation expect(fatal_condition value, std::source_location where) {
    return expectation(std::move(value.value), true, where);
}

void skip(std::string reason) {
    throw skip_signal{std::move(reason)};
}

void fail(std::string message, std::source_location where) {
    record_failure(message, where, nullptr, true);
    throw fatal_signal{};
}

// ---- cases --------------------------------------------------------------------

void test::operator=(body b) {
    auto& s = the();
    std::unique_lock g(s.lock);
    write_program();
    const std::string id = full_id(name_);
    s.seen.insert(id);
    s.clauses.insert(attrs_.clauses.begin(), attrs_.clauses.end());
    const std::string file = b.where.file_name();
    if (s.listing) {
        std::string ev = std::format(
            R"("e":"case","id":{},"name":{},"file":{},"line":{},"kind":{},"tags":{},"clauses":{},"resources":{},"artifacts":{})",
            json_string(id), json_string(name_), json_string(file), b.where.line(),
            json_string(kind_name(attrs_.kind.value_or(kind::unit))), list_of(attrs_.tags),
            list_of(attrs_.clauses), list_of(attrs_.resources), list_of(attrs_.artifacts));
        if (attrs_.skip)
            ev += R"(,"expect":"skip")";
        else if (attrs_.expected_failure)
            ev += R"(,"expect":"fail")";
        write_event(ev);
        return;
    }
    if (!selected(id))
        return;
    auto finish = [&](std::string_view status, double ms, std::string_view reason) {
        s.counts[std::string(status)]++;
        if (status == "fail" || status == "xpass")
            s.any_failed = true;
        std::string ev = std::format(R"("e":"end","id":{},"status":"{}","ms":{:.3f})",
                                     json_string(id), status, ms);
        if (!reason.empty())
            ev += ",\"reason\":" + json_string(reason);
        write_event(ev);
        const char* mark = status == "pass"    ? "  ok  "
                           : status == "skip"  ? "  skip"
                           : status == "xfail" ? "  xfail"
                                               : "  FAIL";
        say(std::format("{} {}{}{}", mark, id, reason.empty() ? "" : " -- ", reason));
    };
    if (attrs_.skip) {
        write_event(
            std::format(R"("e":"begin","id":{},"t":{:.3f})", json_string(id), since_start()));
        finish("skip", 0, *attrs_.skip);
        return;
    }
    write_event(std::format(R"("e":"begin","id":{},"t":{:.3f})", json_string(id), since_start()));
    s.stack.push_back({id});
    const auto started = clock::now();
    std::string status = "pass";
    std::string reason;
    g.unlock();
    try {
        b.run();
    } catch (const fatal_signal&) {
    } catch (const skip_signal& sk) {
        status = "skip";
        reason = sk.reason;
    } catch (const std::exception& e) {
        record_failure(std::format("an exception escaped the case: {}", e.what()), b.where, nullptr,
                       false);
    } catch (...) {
        record_failure("an exception that is not a std::exception escaped the case", b.where,
                       nullptr, false);
    }
    g.lock();
    const double ms = std::chrono::duration<double, std::milli>(clock::now() - started).count();
    running_case done = std::move(s.stack.back());
    s.stack.pop_back();
    if (done.dir) {
        std::error_code ec;
        std::filesystem::remove_all(*done.dir, ec);
    }
    if (status != "skip")
        status = done.failed ? "fail" : "pass";
    if (attrs_.expected_failure) {
        if (status == "fail") {
            status = "xfail";
            reason = *attrs_.expected_failure;
        } else if (status == "pass") {
            status = "xpass";
            reason = "expected to fail: " + *attrs_.expected_failure;
        }
    }
    if (status == "fail" && !s.stack.empty() && !s.stack.back().scope)
        s.stack.back().failed = true;
    finish(status, ms, reason);
}

suite::suite(std::string name, std::function<void()> f, std::source_location) {
    the().suites.emplace_back(std::move(name), std::move(f));
}

int report() {
    auto& s = the();
    run_suites();
    std::lock_guard g(s.lock);
    if (s.done)
        return s.any_failed ? 1 : 0;
    s.done = true;
    write_program();
    bool missing = false;
    if (!s.listing && s.selection) {
        for (const auto& want : *s.selection) {
            if (s.seen.contains(want))
                continue;
            missing = true;
            write_event(std::format(R"("e":"missing","id":{})", json_string(want)));
            say(std::format("  FAIL {} -- no such case", want));
        }
    }
    const bool failed = s.any_failed || missing;
    std::string counts = "{";
    bool first = true;
    for (const auto& [k, v] : s.counts) {
        counts += std::format("{}{}:{}", first ? "" : ",", json_string(k), v);
        first = false;
    }
    counts += "}";
    write_event(
        std::format(R"("e":"done","status":"{}","counts":{})", failed ? "fail" : "pass", counts));
    if (!s.listing) {
        auto n = [&](const char* k) {
            auto it = s.counts.find(k);
            return it == s.counts.end() ? 0 : it->second;
        };
        say(std::format("{} passed, {} failed, {} skipped{}{}", n("pass") + n("xfail"),
                        n("fail") + n("xpass"), n("skip"),
                        failed && !s.seed_given ? std::format(" (seed {})", s.seed) : "",
                        missing ? ", cases missing" : ""));
    }
    return s.listing ? 0 : (failed ? 1 : 0);
}

int run(int argc, char** argv) {
    auto& s = the();
    std::vector<std::string> prefixes;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--list")
            list = true;
        else if (!a.starts_with("--"))
            prefixes.emplace_back(a);
    }
    if (list && !s.protocol) {
        // The cases, by identifier, for a person: run the suites in list mode with no events.
        s.listing = true;
        run_suites();
        for (const auto& id : s.seen)
            say(id);
        s.done = true;
        return 0;
    }
    if (!prefixes.empty() && !s.protocol) {
        std::set<std::string> ids(prefixes.begin(), prefixes.end());
        s.selection = std::move(ids);
    }
    return report();
}

// ---- fixtures -------------------------------------------------------------------

std::filesystem::path temp_dir() {
    auto& s = the();
    std::lock_guard g(s.lock);
    if (s.stack.empty() || s.stack.back().scope)
        throw std::logic_error("mtest::temp_dir() outside a case");
    auto& c = s.stack.back();
    if (!c.dir) {
        auto root =
            s.tmp_root.empty() ? std::filesystem::temp_directory_path() / "mtest" : s.tmp_root;
        auto dir = root / std::format("{}-{:x}", sanitized(c.id), std::random_device{}());
        std::filesystem::create_directories(dir);
        c.dir = dir;
    }
    return *c.dir;
}

std::uint64_t seed() {
    return the().seed;
}

std::string current_case() {
    auto& s = the();
    std::lock_guard g(s.lock);
    return s.stack.empty() || s.stack.back().scope ? std::string{} : s.stack.back().id;
}

void produced(const std::filesystem::path& file, std::string_view kind) {
    write_event(std::format(R"("e":"artifact","id":{},"path":{},"kind":{})",
                            json_string(current_case()), json_string(os::fs::generic_utf8(file)),
                            json_string(kind)));
}

void metric(std::string_view name, double value, std::string_view unit,
            std::span<const double> samples, std::string_view better) {
    std::string ev =
        std::format(R"("e":"metric","id":{},"name":{},"value":{},"unit":{},"better":{})",
                    json_string(current_case()), json_string(name), value, json_string(unit),
                    json_string(better));
    if (!samples.empty()) {
        ev += ",\"samples\":[";
        for (std::size_t i = 0; i < samples.size(); ++i)
            ev += std::format("{}{}", i ? "," : "", samples[i]);
        ev += "]";
    }
    write_event(ev);
}

bool under_runner() {
    return the().protocol;
}

std::set<std::string> declared_clauses() {
    auto& s = the();
    std::lock_guard g(s.lock);
    return s.clauses;
}

test& test::operator[](known_gap g) {
    // Today, in UTC, as YYYY-MM-DD: the gap holds while today is not past its date.
    const auto today = std::chrono::year_month_day(
        std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()));
    const auto stamp = std::format("{:%Y-%m-%d}", std::chrono::sys_days(today));
    if (stamp <= g.due)
        attrs_.expected_failure = std::format("a known gap until {}: {}", g.due, g.reason);
    attrs_.gap_due = g.due;
    return *this;
}

std::vector<std::string> self_command() {
    return os::env::self_command();
}

}  // namespace mcpplibs::mtest
