// The runtime of mcpplibs.mtest: the program's cases, the runner's requests
// (spec/protocol.md P2) and the two reports, the events and the terminal's.
module mcpplibs.mtest;

import std;
import mcpplibs.os;
import nlohmann.json;

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
    bool scope = false;        // a suite: a prefix of identifiers and attributes, not a case
    attributes attrs;          // what the cases within it have
    bool failed = false;       // an expectation of its own did not hold
    bool case_failed = false;  // a case it declared failed
    std::optional<std::filesystem::path> dir;
};

struct declared_suite {
    std::string name;
    std::source_location where;
    attributes attrs;
    std::function<void()> body;
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
    std::vector<declared_suite> suites;
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

// An event of the report (P5): an object whose fields keep the order they are written in.
using event = nlohmann::ordered_json;

double since_start() {
    return std::chrono::duration<double, std::milli>(clock::now() - the().began).count();
}

void write_event(const event& e) {
    auto& s = the();
    if (!s.protocol || !s.events)
        return;
    // Text that is not UTF-8 (a program's output) is written with the replacement character.
    s.events << e.dump(-1, ' ', false, event::error_handler_t::replace) << "\n";
    s.events.flush();
}

void write_program() {
    auto& s = the();
    if (s.program_written)
        return;
    s.program_written = true;
    // Keeps the marker in a link that discards what nothing reaches.
    [[maybe_unused]] volatile char keep = protocol_marker[0];
    write_event(
        {{"e", "program"},
         {"protocol", 1},
         {"level", 3},
         {"framework", "mcpplibs.mtest"},
         {"version", "0.3.0"},
         {"isolation", {"process", "case"}},
         {"guide", "https://github.com/mcpp-community/mtest/blob/main/lib/docs/writing-tests.md"}});
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
    return s.stack.empty() || s.stack.back().id.empty()
               ? std::string(name)
               : s.stack.back().id + "/" + std::string(name);
}

// What a case declared here has besides its own attributes: its suite's and its enclosing cases'.
attributes inherited() {
    auto& s = the();
    return s.stack.empty() ? attributes{} : s.stack.back().attrs;
}

// Whether a case is reported: it is selected, or declared inside a selected case (P4.1). A case
// that runs only because it declares a selected one is the scope of that case (P3.4).
bool reported(const std::string& id) {
    auto& s = the();
    if (!s.selection)
        return true;
    for (const auto& want : *s.selection)
        if (want == id ||
            (id.starts_with(want) && id.size() > want.size() && id[want.size()] == '/'))
            return true;
    return false;
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
    event ev = {{"e", "failure"},
                {"id", id},
                {"file", file},
                {"line", where.line()},
                {"message", text}};
    if (value && !value->expected.empty()) {
        ev["expected"] = value->expected;
        ev["actual"] = value->actual;
    }
    if (is_fatal)
        ev["fatal"] = true;
    write_event(ev);
}

void run_suites() {
    auto& s = the();
    while (s.suites_run < s.suites.size()) {
        auto [name, where, attrs, f] = s.suites[s.suites_run++];
        if (!name.empty()) {
            // A named suite is a case: listed with its attributes, run when it is selected.
            test(name, std::move(attrs)) = body(std::move(f), where);
            continue;
        }
        s.stack.push_back({s.stack.empty() ? std::string{} : s.stack.back().id, true,
                           within(inherited(), attrs)});
        try {
            f();
        } catch (const std::exception& e) {
            record_failure(std::format("an exception escaped the suite: {}", e.what()),
                           std::source_location::current(), nullptr, false);
        } catch (...) {
            record_failure("an exception escaped the suite", std::source_location::current(),
                           nullptr, false);
        }
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
    const attributes attrs = within(inherited(), attrs_);
    s.seen.insert(id);
    s.clauses.insert(attrs.clauses.begin(), attrs.clauses.end());
    const std::string file = b.where.file_name();
    if (s.listing) {
        event ev = {{"e", "case"},
                    {"id", id},
                    {"name", name_},
                    {"file", file},
                    {"line", b.where.line()}};
        // A kind no attribute states is the runner's to give (the program's, by its directory).
        if (attrs.kind)
            ev["kind"] = kind_name(*attrs.kind);
        ev["tags"] = attrs.tags;
        ev["clauses"] = attrs.clauses;
        ev["resources"] = attrs.resources;
        ev["artifacts"] = attrs.artifacts;
        if (attrs.limit)
            ev["timeout"] = std::chrono::duration<double>(*attrs.limit).count();
        if (attrs.isolated)
            ev["isolation"] = "case";
        if (attrs.retries)
            ev["retries"] = *attrs.retries;
        if (attrs.diagnosis) {
            event d = {{"env", event::object()}};
            for (const auto& [k, v] : attrs.diagnosis->env)
                d["env"][k] = v;
            if (attrs.diagnosis->limit.count() > 0)
                d["timeout"] = std::chrono::duration<double>(attrs.diagnosis->limit).count();
            ev["diagnose"] = d;
        }
        if (attrs.skip)
            ev["expect"] = "skip";
        else if (attrs.expected_failure)
            ev["expect"] = "fail";
        write_event(ev);
        return;
    }
    if (!selected(id))
        return;
    auto finish = [&](std::string_view status, double ms, std::string_view reason) {
        s.counts[std::string(status)]++;
        if (status == "fail" || status == "xpass")
            s.any_failed = true;
        event ev = {{"e", "end"}, {"id", id}, {"status", status}, {"ms", ms}};
        if (!reason.empty())
            ev["reason"] = reason;
        write_event(ev);
        const char* mark = status == "pass"    ? "  ok  "
                           : status == "skip"  ? "  skip"
                           : status == "xfail" ? "  xfail"
                                               : "  FAIL";
        say(std::format("{} {}{}{}", mark, id, reason.empty() ? "" : " -- ", reason));
    };
    const bool own = reported(id);
    if (attrs.skip) {
        // A skipped scope skips the selected cases it declares.
        std::vector<std::string> skipped{id};
        if (!own) {
            skipped.clear();
            for (const auto& want : *s.selection)
                if (want.starts_with(id + "/")) {
                    skipped.push_back(want);
                    s.seen.insert(want);
                }
        }
        for (const auto& one : skipped) {
            write_event({{"e", "begin"}, {"id", one}, {"t", since_start()}});
            event ev = {{"e", "end"},
                        {"id", one},
                        {"status", "skip"},
                        {"ms", 0},
                        {"reason", *attrs.skip}};
            s.counts["skip"]++;
            write_event(ev);
            say(std::format("  skip {} -- {}", one, *attrs.skip));
        }
        return;
    }
    // P3.4: a case declared inside a case is not listed; its begin states what it is.
    event begun = {{"e", "begin"}, {"id", id}, {"t", since_start()}};
    if (attrs.kind)
        begun["kind"] = kind_name(*attrs.kind);
    if (!attrs.tags.empty())
        begun["tags"] = attrs.tags;
    if (!attrs.clauses.empty())
        begun["clauses"] = attrs.clauses;
    if (own)
        write_event(begun);
    s.stack.push_back({id, false, attrs});
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
    if (!own) {
        // A scope is reported only when a statement of its own failed.
        if (!done.failed)
            return;
        write_event(begun);
    }
    if (status != "skip")
        status = done.failed || done.case_failed ? "fail" : "pass";
    if (attrs.expected_failure) {
        if (status == "fail") {
            status = "xfail";
            reason = *attrs.expected_failure;
        } else if (status == "pass") {
            status = "xpass";
            reason = "expected to fail: " + *attrs.expected_failure;
        }
    }
    if (status == "fail" && !s.stack.empty() && !s.stack.back().scope)
        s.stack.back().case_failed = true;
    finish(status, ms, reason);
}

void Suite::add(std::string name, std::source_location where, attributes attrs,
                std::function<void()> f) {
    the().suites.push_back({std::move(name), where, std::move(attrs), std::move(f)});
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
            write_event({{"e", "missing"}, {"id", want}});
            say(std::format("  FAIL {} -- no such case", want));
        }
    }
    const bool failed = s.any_failed || missing;
    write_event({{"e", "done"}, {"status", failed ? "fail" : "pass"}, {"counts", s.counts}});
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
        // Without a runner's directory, the case's own is named for mtest in the system's.
        auto dir =
            s.tmp_root.empty()
                ? std::filesystem::temp_directory_path() /
                      std::format("mtest-{}-{:x}", sanitized(c.id), std::random_device{}())
                : s.tmp_root / std::format("{}-{:x}", sanitized(c.id), std::random_device{}());
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
    write_event({{"e", "artifact"},
                 {"id", current_case()},
                 {"path", os::fs::generic_utf8(file)},
                 {"kind", kind}});
}

void metric(std::string_view name, double value, std::string_view unit,
            std::span<const double> samples, std::string_view better) {
    event ev = {{"e", "metric"},  {"id", current_case()}, {"name", name},
                {"value", value}, {"unit", unit},         {"better", better}};
    if (!samples.empty())
        ev["samples"] = std::vector<double>(samples.begin(), samples.end());
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

void apply(attributes& a, known_gap g) {
    // Today, in UTC, as YYYY-MM-DD: the gap holds while today is not past its date.
    const auto today = std::chrono::year_month_day(
        std::chrono::floor<std::chrono::days>(std::chrono::system_clock::now()));
    const auto stamp = std::format("{:%Y-%m-%d}", std::chrono::sys_days(today));
    if (stamp <= g.due)
        a.expected_failure = std::format("a known gap until {}: {}", g.due, g.reason);
    a.gap_due = g.due;
}

attributes within(const attributes& outer, const attributes& inner) {
    attributes a = outer;
    auto add = [](std::vector<std::string>& to, const std::vector<std::string>& from) {
        for (const auto& x : from)
            if (std::ranges::find(to, x) == to.end())
                to.push_back(x);
    };
    add(a.tags, inner.tags);
    add(a.clauses, inner.clauses);
    add(a.resources, inner.resources);
    add(a.artifacts, inner.artifacts);
    if (inner.kind)
        a.kind = inner.kind;
    if (inner.expected_failure)
        a.expected_failure = inner.expected_failure;
    if (inner.skip)
        a.skip = inner.skip;
    if (inner.gap_due)
        a.gap_due = inner.gap_due;
    if (inner.limit)
        a.limit = inner.limit;
    a.isolated = a.isolated || inner.isolated;
    if (inner.retries)
        a.retries = inner.retries;
    if (inner.diagnosis)
        a.diagnosis = inner.diagnosis;
    return a;
}

void detail::made_shared(std::string_view name, double ms) {
    write_event({{"e", "fixture"}, {"id", current_case()}, {"name", name}, {"ms", ms}});
}

std::vector<std::string> self_command() {
    return os::env::self_command();
}

}  // namespace mcpplibs::mtest
