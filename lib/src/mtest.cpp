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
    std::map<std::string, std::pair<double, std::string>> metrics;  // name -> value, unit
    std::size_t declared = 0;         // the cases it declared that ran
    std::size_t declared_failed = 0;  // of those, the ones that failed
    // Its failures as a person reads them, printed under its line when it ends.
    std::vector<std::string> failures;
    std::uint64_t held_before = 0;  // the checks held in the program when it began
    bool stopped = false;           // a fatal expectation ended it
    // SPEC-017 O8: what it writes, kept while it runs under a runner and stated as `output`
    // events when it ends; a case it declares keeps its own, and this one's resumes after it.
    std::optional<os::stdio::capture> out_capture, err_capture;
    std::string out_text, err_text;
    bool capturing = false;
};

// The expectations that held, in the whole program: a case's share is the difference over its
// run. Counted without the lock, as a passing check is the common one.
std::atomic<std::uint64_t> g_held{0};

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
    std::vector<std::string> patterns;  // a person's, with `*`
    std::set<std::string> patterns_met;
    bool alone = false;  // the selection is a person's, given as the program's arguments
    std::set<std::string> seen;
    std::ofstream events;
    std::filesystem::path tmp_root;
    std::uint64_t seed = 0;
    bool seed_given = false;
    std::vector<running_case> stack;
    std::vector<declared_suite> suites;
    std::size_t suites_run = 0;
    std::map<std::string, int> counts;
    // Of `counts`, the cases that declared cases: the console's tally is of the cases that check.
    std::map<std::string, int> container_counts;
    std::set<std::string> clauses;
    bool any_failed = false;
    bool program_written = false;
    bool done = false;
    // P10: how closely a runner asked for use to be measured; a case may ask for more.
    mtest::monitor_level monitor = mtest::monitor_level::off;
    // P11: the run fixtures and components this program declares.
    std::map<std::string, std::function<void(serving&)>> run_fixtures;
    // The children this program's files declare, by name.
    std::map<std::string, std::function<int(std::vector<std::string>)>, std::less<>> children;
    struct component_decl {
        std::string name;
        std::vector<resource_pool> pools;
    };
    std::vector<component_decl> components;

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
        if (auto m = os::env::get("MTEST_MONITOR"); m && !m->empty())
            monitor = *m == "trace"      ? mtest::monitor_level::trace
                      : *m == "detailed" ? mtest::monitor_level::detailed
                      : *m == "basic"    ? mtest::monitor_level::basic
                                         : mtest::monitor_level::off;
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

// ---- use of the machine (P10) -------------------------------------------------

// The largest resident set of this process since its last reset, where the kernel keeps one that
// can be reset (Linux: /proc/self/status VmHWM, reset by writing 5 to /proc/self/clear_refs).
std::optional<std::uint64_t> resident_peak() {
    auto text = os::fs::read(std::filesystem::path("/proc/self/status"));
    if (!text)
        return std::nullopt;
    const auto at = text->find("VmHWM:");
    if (at == std::string::npos)
        return std::nullopt;
    std::uint64_t kb = 0;
    std::string_view rest = std::string_view(*text).substr(at + 6);
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
        rest.remove_prefix(1);
    std::from_chars(rest.data(), rest.data() + rest.size(), kb);
    return kb * 1024;
}
bool reset_resident_peak() {
    return bool(os::fs::write(std::filesystem::path("/proc/self/clear_refs"), "5"));
}

// The resident set now, where the kernel tells it (Linux: /proc/self/status VmRSS).
std::optional<std::uint64_t> resident_now() {
    auto text = os::fs::read(std::filesystem::path("/proc/self/status"));
    if (!text)
        return std::nullopt;
    const auto at = text->find("VmRSS:");
    if (at == std::string::npos)
        return std::nullopt;
    std::uint64_t kb = 0;
    std::string_view rest = std::string_view(*text).substr(at + 6);
    while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
        rest.remove_prefix(1);
    std::from_chars(rest.data(), rest.data() + rest.size(), kb);
    return kb * 1024;
}

// `trace`: the processor's load and the resident set over the case, sampled every `every`.
class sampler {
public:
    explicit sampler(std::chrono::milliseconds every)
        : thread_([this, every](std::stop_token stop) {
              auto last = os::process::resources(os::process::of::self);
              auto then = clock::now();
              while (!stop.stop_requested()) {
                  std::this_thread::sleep_for(every);
                  auto now = os::process::resources(os::process::of::self);
                  const auto t = clock::now();
                  const double wall = std::chrono::duration<double, std::milli>(t - then).count();
                  std::lock_guard g(m_);
                  if (now && last && wall > 0)
                      load_.push_back(std::chrono::duration<double, std::milli>(
                                          (now->user + now->system) - (last->user + last->system))
                                          .count() /
                                      wall);
                  if (auto rss = resident_now())
                      rss_.push_back(double(*rss));
                  last = now;
                  then = t;
              }
          }) {}
    std::pair<std::vector<double>, std::vector<double>> stop() {
        thread_.request_stop();
        thread_.join();
        std::lock_guard g(m_);
        return {load_, rss_};
    }
private:
    std::mutex m_;
    std::vector<double> load_, rss_;
    std::jthread thread_;
};

std::chrono::milliseconds sample_every() {
    if (auto v = os::env::get("MTEST_MONITOR_SAMPLE"); v && !v->empty()) {
        int ms = 0;
        if (std::from_chars(v->data(), v->data() + v->size(), ms).ec == std::errc{} && ms > 0)
            return std::chrono::milliseconds(ms);
    }
    return std::chrono::milliseconds(50);
}

struct use_mark {
    std::optional<os::process::usage> at;
    bool peak_reset = false;
    clock::time_point when = clock::now();
    std::shared_ptr<sampler> samples;
};

use_mark mark_use(mtest::monitor_level level) {
    use_mark m;
    if (level == mtest::monitor_level::off)
        return m;
    if (auto u = os::process::resources(os::process::of::self))
        m.at = *u;
    if (level >= mtest::monitor_level::detailed)
        m.peak_reset = reset_resident_peak();
    if (level == mtest::monitor_level::trace)
        m.samples = std::make_shared<sampler>(sample_every());
    return m;
}

void write_event(const event& e);

// The case's use since `m`, as metrics of the case with their precision (P6.6).
void report_use(std::string_view id, const use_mark& m, mtest::monitor_level level) {
    if (level == mtest::monitor_level::off || !m.at)
        return;
    auto now = os::process::resources(os::process::of::self);
    if (!now)
        return;
    auto put = [&](std::string_view name, double value, std::string_view unit,
                   std::string_view precision) {
        write_event({{"e", "metric"},
                     {"id", id},
                     {"name", name},
                     {"value", value},
                     {"unit", unit},
                     {"better", "lower"},
                     {"precision", precision}});
    };
    auto ms = [](std::chrono::nanoseconds d) {
        return std::chrono::duration<double, std::milli>(d).count();
    };
    if (m.samples) {
        auto [load, rss] = m.samples->stop();
        auto series = [&](std::string_view name, const std::vector<double>& v,
                          std::string_view unit) {
            if (v.empty())
                return;
            event ev = {{"e", "metric"},     {"id", id},
                        {"name", name},      {"value", *std::ranges::max_element(v)},
                        {"unit", unit},      {"samples", v},
                        {"better", "lower"}, {"precision", "sampled"}};
            write_event(ev);
        };
        series("cpu.load", load, "1");
        series("mem.rss", rss, "B");
    }
    put("cpu.user", ms(now->user - m.at->user), "ms", "exact");
    put("cpu.sys", ms(now->system - m.at->system), "ms", "exact");
    if (level < mtest::monitor_level::detailed)
        return;
    if (m.peak_reset) {
        if (auto peak = resident_peak())
            put("mem.peak", double(*peak), "B", "exact");
    } else if (now->peak_memory) {
        put("mem.peak", double(*now->peak_memory), "B", "process");
    }
    if (now->peak_memory && m.at->peak_memory)
        put("mem.delta", double(*now->peak_memory) - double(*m.at->peak_memory), "B", "process");
    if (now->read_bytes && m.at->read_bytes)
        put("io.read", double(*now->read_bytes - *m.at->read_bytes), "B", "exact");
    if (now->written_bytes && m.at->written_bytes)
        put("io.write", double(*now->written_bytes - *m.at->written_bytes), "B", "exact");
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
    const char* volatile keep = protocol_marker;
    (void)keep;
    write_event(
        {{"e", "program"},
         {"protocol", 1},
         {"level", 3},
         {"framework", "mcpplibs.mtest"},
         {"version", "0.8.4"},
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

// Whether an identifier is what a pattern says: `*` is any run of characters, the rest itself.
// `begins`: whether an identifier that begins so could be.
bool matches(std::string_view pattern, std::string_view id, bool begins = false) {
    std::size_t at = 0, in = 0, star = std::string_view::npos, resumed = 0;
    while (in < id.size()) {
        if (at < pattern.size() && pattern[at] == '*') {
            star = at++;
            resumed = in;
        } else if (at < pattern.size() && pattern[at] == id[in]) {
            ++at;
            ++in;
        } else if (star != std::string_view::npos) {
            at = star + 1;
            in = ++resumed;
        } else {
            return false;
        }
    }
    if (begins)
        return true;
    while (at < pattern.size() && pattern[at] == '*')
        ++at;
    return at == pattern.size();
}

// The pattern a case answers, itself or by a case it is declared in; met from then on.
bool answers_pattern(const std::string& id) {
    auto& s = the();
    bool any = false;
    for (const auto& pattern : s.patterns) {
        bool is = matches(pattern, id);
        for (auto slash = id.find('/'); !is && slash != std::string::npos;
             slash = id.find('/', slash + 1))
            is = matches(pattern, std::string_view(id).substr(0, slash));
        if (is) {
            s.patterns_met.insert(pattern);
            any = true;
        }
    }
    return any;
}

// Whether a case declared inside this one could answer a pattern.
bool may_declare_pattern(const std::string& id) {
    return std::ranges::any_of(the().patterns, [&](const std::string& pattern) {
        return matches(pattern, id + "/", true);
    });
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
    return answers_pattern(id);
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
    return answers_pattern(id) || may_declare_pattern(id);
}

std::string sanitized(std::string_view id) {
    std::string out;
    for (char c : id)
        out += (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') ? c : '_';
    if (out.size() > 80)
        out.resize(80);
    return out;
}

// SPEC-017 O8: a case's own output. What the streams hold is written first, so that what the
// case writes is all that the capture keeps.
void flush_streams() {
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);
}

void begin_capture(running_case& c) {
    flush_streams();
    if (auto o = os::stdio::capture::begin(os::stdio::stream::output))
        c.out_capture.emplace(std::move(*o));
    if (auto e = os::stdio::capture::begin(os::stdio::stream::error))
        c.err_capture.emplace(std::move(*e));
}

// The capture ended and its text kept; the text is written to the stream it was taken from, so
// a person at the terminal reads what the case wrote where it wrote it.
void end_capture(running_case& c) {
    flush_streams();
    if (c.err_capture) {
        auto t = c.err_capture->text();
        c.err_capture.reset();
        if (!t.empty()) {
            (void)os::stdio::write_error(t);
            c.err_text += t;
        }
    }
    if (c.out_capture) {
        auto t = c.out_capture->text();
        c.out_capture.reset();
        if (!t.empty()) {
            (void)os::stdio::write_output(t);
            (void)os::stdio::flush_output();
            c.out_text += t;
        }
    }
}

void say(std::string_view line) {
    std::println("{}", line);
    std::cout.flush();
}

// The statement at `line` of `file` and the lines it continues on, as one line: what a check
// without operands states. Empty when the source is not readable here.
std::string statement_at(const std::string& file, std::uint_least32_t line) {
    std::ifstream in(os::fs::from_utf8(file), std::ios::binary);
    if (!in || line == 0)
        return {};
    std::string text, one;
    for (std::uint_least32_t n = 1; n < line && std::getline(in, one); ++n) {}
    int depth = 0;
    for (int taken = 0; taken < 8 && std::getline(in, one); ++taken) {
        if (one.ends_with('\r'))
            one.pop_back();
        const auto first = one.find_first_not_of(" \t");
        if (first == std::string::npos)
            continue;
        if (!text.empty())
            text += ' ';
        text += one.substr(first);
        for (char c : one)
            depth += c == '(' ? 1 : c == ')' ? -1 : 0;
        if (depth <= 0 && text.find(';') != std::string::npos)
            break;
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.pop_back();
    if (text.size() > 240)
        text = text.substr(0, 237) + "...";
    return text;
}

// A file under the working directory by its name there; any other by its own.
std::string shown_path(const std::string& file) {
    std::error_code ec;
    const auto p = os::fs::from_utf8(file);
    if (!p.is_absolute())
        return file;
    const auto rel = std::filesystem::proximate(p, std::filesystem::current_path(ec), ec);
    if (ec || rel.empty() || rel.begin()->string() == "..")
        return file;
    return os::fs::generic_utf8(rel);
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
    const bool compared = value && !value->expected.empty();
    std::string text = std::string(message);
    if (value && !value->expression.empty())
        text = text.empty() ? value->expression : value->expression + ": " + text;
    if (text.empty())
        text = "the expectation did not hold";
    // As a person reads it: where, the statement, the two sides or that it did not hold, the
    // message the check added, and whether the case went on.
    const std::string source = value ? statement_at(file, where.line()) : std::string{};
    std::string block = std::format("{}:{}", shown_path(file), where.line());
    if (!source.empty())
        block += "\n    " + source;
    if (compared) {
        block +=
            std::format("\n    expected  {}\n    actual    {}", value->expected, value->actual);
        if (!message.empty())
            block += std::format("\n    note      {}", message);
    } else if (value) {
        block += source.empty() || !message.empty() || !value->expression.empty()
                     ? "\n    " + text
                     : std::string("\n    did not hold");
    } else {
        block += "\n    " + text;
    }
    if (is_fatal) {
        block += "\n    fatal: the rest of the case did not run";
        if (in_case)
            s.stack.back().stopped = true;
    }
    if (in_case)
        s.stack.back().failures.push_back(std::move(block));
    else {
        std::string shown;
        for (std::string_view rest = block; !rest.empty();) {
            const auto nl = rest.find('\n');
            shown += "    " + std::string(rest.substr(0, nl)) + "\n";
            rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        }
        shown.pop_back();
        say(shown);
    }
    event ev = {{"e", "failure"},
                {"id", id},
                {"file", file},
                {"line", where.line()},
                {"message", text}};
    if (!source.empty())
        ev["source"] = source;
    if (compared) {
        ev["expected"] = value->expected;
        ev["actual"] = value->actual;
    }
    if (is_fatal)
        ev["fatal"] = true;
    write_event(ev);
}

event bounds_json(const std::vector<metric_bound>& bounds) {
    event out = event::array();
    for (const auto& b : bounds) {
        event one = {{"metric", b.metric}};
        if (b.max)
            one["max"] = *b.max;
        if (b.min)
            one["min"] = *b.min;
        out.push_back(std::move(one));
    }
    return out;
}

// P6.5: each bound of the case on the metrics it reported, as an expectation at its end.
void check_bounds(const std::vector<metric_bound>& bounds, const std::source_location& where) {
    auto& s = the();
    const auto& reported = s.stack.back().metrics;
    for (const auto& b : bounds) {
        const auto it = reported.find(b.metric);
        if (it == reported.end()) {
            record_failure(std::format("the case reported no metric `{}`, which it bounds",
                                       b.metric),
                           where, nullptr, false);
            continue;
        }
        const auto [value, unit] = it->second;
        // A unit of 1 (a ratio, a count) is not written.
        const auto in = [&](double v) {
            return unit.empty() || unit == "1" ? std::format("{:g}", v)
                                               : std::format("{:g} {}", v, unit);
        };
        if (b.max && value > *b.max)
            record_failure(std::format("metric {} = {}, over its bound {} (at_most)", b.metric,
                                       in(value), in(*b.max)),
                           where, nullptr, false);
        if (b.min && value < *b.min)
            record_failure(std::format("metric {} = {}, under its bound {} (at_least)", b.metric,
                                       in(value), in(*b.min)),
                           where, nullptr, false);
    }
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
    if (held_) {
        g_held.fetch_add(1, std::memory_order_relaxed);
        return;
    }
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
    attributes attrs = within(inherited(), attrs_);
    attrs.skip = skip_reason(attrs);
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
        if (attrs.only_platforms || attrs.only_architectures) {
            auto words = attrs.only_platforms.value_or(std::vector<std::string>{});
            for (const auto& w : attrs.only_architectures.value_or(std::vector<std::string>{}))
                words.push_back(w);
            ev["only"] = words;
        }
        if (!attrs.excepted.empty())
            ev["except"] = attrs.excepted;
        if (!attrs.capabilities.empty()) {
            event answered = event::object();
            for (const auto& c : attrs.capabilities)
                answered[c.name()] = c.answer().present;
            ev["capabilities"] = answered;
        }
        if (attrs.limit)
            ev["timeout"] = std::chrono::duration<double>(*attrs.limit).count();
        if (!attrs.bounds.empty())
            ev["bounds"] = bounds_json(attrs.bounds);
        if (attrs.isolated)
            ev["isolation"] = "case";
        if (!attrs.uses.empty())
            ev["uses"] = attrs.uses;
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
        if (attrs.skip) {
            ev["expect"] = "skip";
            ev["reason"] = *attrs.skip;
        } else if (attrs.expected_failure)
            ev["expect"] = "fail";
        write_event(ev);
        return;
    }
    if (!selected(id))
        return;
    auto finish = [&](std::string_view status, double ms, std::string_view reason,
                      const running_case* done = nullptr) {
        s.counts[std::string(status)]++;
        const bool container = done && done->declared > 0;
        if (container)
            s.container_counts[std::string(status)]++;
        if (status == "fail" || status == "xpass")
            s.any_failed = true;
        event ev = {{"e", "end"}, {"id", id}, {"status", status}, {"ms", ms}};
        if (!reason.empty())
            ev["reason"] = reason;
        // SPEC-017 O3: of the checks the case ran, those that held and those that did not.
        if (done && status != "skip")
            ev["checks"] = {{"held", g_held.load(std::memory_order_relaxed) - done->held_before},
                            {"failed", done->failures.size()}};
        write_event(ev);
        const char* mark = status == "pass"    ? "  ok  "
                           : status == "skip"  ? "  skip"
                           : status == "xfail" ? "  xfail"
                                               : "  FAIL";
        // What it amounts to: of a case that declared cases, how many of them failed; of one that
        // checks, how many of its checks failed, and whether a fatal one ended it.
        std::string tally;
        if (done && status != "skip") {
            if (container && done->declared_failed > 0)
                tally = std::format("{} of {} cases failed", done->declared_failed, done->declared);
            const std::size_t failed = done->failures.size();
            if (failed > 0) {
                const auto held = g_held.load(std::memory_order_relaxed) - done->held_before;
                if (!tally.empty())
                    tally += "; ";
                tally += std::format("{} of {} checks failed", failed, failed + held);
                if (done->stopped)
                    tally += ", stopped";
            }
        }
        say(std::format("{} {}{}{}{}", mark, id, reason.empty() ? "" : " -- ", reason,
                        tally.empty() ? "" : "  (" + tally + ")"));
        if (done)
            for (const auto& block : done->failures) {
                std::string shown;
                for (std::string_view rest = block; !rest.empty();) {
                    const auto nl = rest.find('\n');
                    shown += "       " + std::string(rest.substr(0, nl)) + "\n";
                    rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
                }
                shown.pop_back();
                say(shown);
            }
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
            // What a pattern would have found in it is not declared: the scope stands for it.
            for (const auto& pattern : s.patterns)
                if (matches(pattern, id + "/", true)) {
                    s.patterns_met.insert(pattern);
                    if (!std::ranges::contains(skipped, id))
                        skipped.push_back(id);
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
    if (!attrs.bounds.empty())
        begun["bounds"] = bounds_json(attrs.bounds);
    if (own)
        write_event(begun);
    if (!s.stack.empty() && !s.stack.back().scope)
        ++s.stack.back().declared;
    const bool capturing = s.protocol && !s.listing && own;
    if (capturing && !s.stack.empty() && s.stack.back().capturing)
        end_capture(s.stack.back());  // the declaring case's output so far is its own
    s.stack.push_back({id, false, attrs});
    s.stack.back().held_before = g_held.load(std::memory_order_relaxed);
    if (capturing) {
        s.stack.back().capturing = true;
        begin_capture(s.stack.back());
    }
    const auto started = clock::now();
    std::string status = "pass";
    std::string reason;
    const auto level = std::max(s.monitor, attrs.monitoring.value_or(mtest::monitor_level::off));
    const auto use = own ? mark_use(level) : use_mark{};
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
    // A case that declared cases is judged by them; one that declared none, by its own metrics.
    if (status != "skip" && s.stack.back().declared == 0)
        check_bounds(attrs.bounds, b.where);
    const double ms = std::chrono::duration<double, std::milli>(clock::now() - started).count();
    if (own)
        report_use(id, use, level);
    if (capturing)
        end_capture(s.stack.back());
    running_case done = std::move(s.stack.back());
    s.stack.pop_back();
    if (capturing) {
        if (!done.out_text.empty())
            write_event({{"e", "output"}, {"id", id}, {"stream", "out"}, {"text", done.out_text}});
        if (!done.err_text.empty())
            write_event({{"e", "output"}, {"id", id}, {"stream", "err"}, {"text", done.err_text}});
    }
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
    if (status == "fail" && !s.stack.empty() && !s.stack.back().scope) {
        s.stack.back().case_failed = true;
        ++s.stack.back().declared_failed;
    }
    finish(status, ms, reason, &done);
    // The declaring case's capture resumes, after this case's own lines.
    if (capturing && !s.stack.empty() && s.stack.back().capturing)
        begin_capture(s.stack.back());
}

void Suite::add(std::string name, std::source_location where, attributes attrs,
                std::function<void()> f) {
    the().suites.push_back({std::move(name), where, std::move(attrs), std::move(f)});
}

namespace {
// MTEST_SERVE: this program serves one of its run fixtures for a runner (P11.3) and runs no case.
std::optional<int> serve_if_asked() {
    auto name = os::env::get("MTEST_SERVE");
    if (!name || name->empty())
        return std::nullopt;
    auto& s = the();
    auto it = s.run_fixtures.find(*name);
    if (it == s.run_fixtures.end()) {
        std::println(std::cerr, "mtest: this program serves no run fixture named {}", *name);
        return 2;
    }
    serving sv;
    try {
        it->second(sv);
    } catch (const std::exception& e) {
        std::println(std::cerr, "mtest: serving {} failed: {}", *name, e.what());
        return 1;
    }
    return 0;
}

void list_components() {
    auto& s = the();
    for (const auto& c : s.components) {
        event ev = {{"e", "component"}, {"name", c.name}};
        auto pools = event::array();
        for (const auto& p : c.pools)
            pools.push_back({{"name", p.name}, {"capacity", p.capacity}});
        ev["pools"] = pools;
        write_event(ev);
    }
    for (const auto& [name, f] : s.run_fixtures)
        write_event({{"e", "component"}, {"fixture", name}, {"scope", "run"}});
}

void report_program_use() {
    auto& s = the();
    if (s.listing || s.monitor == mtest::monitor_level::off)
        return;
    auto u = os::process::resources(os::process::of::self);
    if (!u)
        return;
    event ev = {{"e", "usage"},
                {"user_ms", std::chrono::duration<double, std::milli>(u->user).count()},
                {"sys_ms", std::chrono::duration<double, std::milli>(u->system).count()}};
    if (u->peak_memory)
        ev["peak_bytes"] = *u->peak_memory;
    if (u->read_bytes)
        ev["read_bytes"] = *u->read_bytes;
    if (u->written_bytes)
        ev["written_bytes"] = *u->written_bytes;
    write_event(ev);
}
}  // namespace

void serving::ready(std::string_view endpoint) {
    std::cout.flush();
    (void)os::stdio::write_output(std::format("mtest-ready {}\n", endpoint));
    (void)os::stdio::flush_output();
}

void serving::stopped() {
    // The runner ends a fixture by closing its input.
    for (;;) {
        auto chunk = os::stdio::read_input();
        if (!chunk || chunk->empty())
            return;
    }
}

run_fixture::run_fixture(std::string_view name, std::function<void(serving&)> serve,
                         std::source_location) {
    the().run_fixtures[std::string(name)] = std::move(serve);
}

Component::Component(std::string_view name, std::vector<resource_pool> pools,
                     std::source_location) {
    the().components.push_back({std::string(name), std::move(pools)});
}

std::string fixture_endpoint(std::string_view name) {
    std::string key = "MTEST_FIXTURE_";
    for (char c : name)
        key += std::isalnum(static_cast<unsigned char>(c))
                   ? char(std::toupper(static_cast<unsigned char>(c)))
                   : '_';
    return os::env::get(key).value_or("");
}

int report() {
    auto& s = the();
    if (auto served = serve_if_asked())
        return *served;
    run_suites();
    std::lock_guard g(s.lock);
    if (s.done)
        return s.any_failed ? 1 : 0;
    s.done = true;
    write_program();
    if (s.listing)
        list_components();
    report_program_use();
    bool missing = false;
    if (!s.listing && s.selection) {
        for (const auto& want : *s.selection) {
            if (s.seen.contains(want))
                continue;
            // A person may name a group of cases by the part of their names before a `/`.
            if (s.alone && std::ranges::any_of(s.seen, [&](const std::string& id) {
                    return id.size() > want.size() && id.starts_with(want) &&
                           id[want.size()] == '/';
                }))
                continue;
            missing = true;
            write_event({{"e", "missing"}, {"id", want}});
            say(std::format("  FAIL {} -- no such case", want));
        }
    }
    if (!s.listing)
        for (const auto& pattern : s.patterns)
            if (!s.patterns_met.contains(pattern)) {
                missing = true;
                say(std::format("  FAIL {} -- no such case", pattern));
            }
    const bool failed = s.any_failed || missing;
    write_event({{"e", "done"}, {"status", failed ? "fail" : "pass"}, {"counts", s.counts}});
    if (!s.listing) {
        // The cases that check; a case that declared cases is told by them.
        auto n = [&](const char* k) {
            auto it = s.counts.find(k);
            auto c = s.container_counts.find(k);
            return (it == s.counts.end() ? 0 : it->second) -
                   (c == s.container_counts.end() ? 0 : c->second);
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
    // A copy started as a child does what its file declares, and nothing else.
    if (argc >= 3 && std::string_view(argv[1]) == "--mtest-child") {
        auto it = s.children.find(std::string_view(argv[2]));
        if (it == s.children.end()) {
            std::println(std::cerr, "mtest: this program declares no child named {}", argv[2]);
            return 2;
        }
        s.done = true;
        return it->second(std::vector<std::string>(argv + 3, argv + argc));
    }
    std::vector<std::string> wanted;
    bool list = false;
    for (int i = 1; i < argc; ++i) {
        std::string_view a = argv[i];
        if (a == "--list")
            list = true;
        else if (!a.starts_with("--"))
            wanted.emplace_back(a);
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
    if (!wanted.empty() && !s.protocol) {
        // A pattern is met as the cases declare themselves (a case inside a case is not listed);
        // the rest are identifiers and names.
        std::set<std::string> ids;
        for (auto& want : wanted)
            if (want.contains('*'))
                s.patterns.push_back(std::move(want));
            else
                ids.insert(std::move(want));
        s.selection = std::move(ids);
        s.alone = true;
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
    {
        auto& s = the();
        std::lock_guard g(s.lock);
        if (!s.stack.empty() && !s.stack.back().scope)
            s.stack.back().metrics[std::string(name)] = {value, std::string(unit)};
    }
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

// ---- where a case runs, and what the machine can do (0.8.0) ------------------

namespace {

bool holds_word(const std::vector<std::string>& words, std::string_view w) {
    return std::ranges::find(words, w) != words.end();
}

std::string joined(const std::vector<std::string>& words) {
    std::string out;
    for (const auto& w : words)
        out += (out.empty() ? "" : ", ") + w;
    return out;
}

std::string this_host() {
    return std::format("{} {}", os::name(os::host()), os::name(os::ARCH));
}

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

// Whether `name` matches `pattern`, where `*` is any run of characters and `?` any one.
bool glob_match(std::string_view pattern, std::string_view name) {
    if (pattern.empty())
        return name.empty();
    if (pattern.front() == '*')
        for (std::size_t skip = 0; skip <= name.size(); ++skip)
            if (glob_match(pattern.substr(1), name.substr(skip)))
                return true;
    if (pattern.front() == '*')
        return false;
    return !name.empty() && (pattern.front() == '?' || pattern.front() == name.front()) &&
           glob_match(pattern.substr(1), name.substr(1));
}

// Whether some file matches `pattern`: a path whose components may hold `*` and `?`.
bool some_file(const std::string& pattern) {
    const auto p = os::fs::from_utf8(pattern);
    std::vector<std::filesystem::path> at{p.root_path()};
    for (const auto& part : p.relative_path()) {
        const auto word = os::fs::utf8(part);
        std::vector<std::filesystem::path> next;
        for (const auto& dir : at) {
            std::error_code ec;
            if (!word.contains('*') && !word.contains('?')) {
                if (std::filesystem::exists(dir / part, ec))
                    next.push_back(dir / part);
                continue;
            }
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

capability_answer evaluate(const probe& t) {
    const std::string system(os::name(os::host()));
    const std::string arch(os::name(os::ARCH));
    if (!t.os.empty() && std::ranges::find(t.os, os::host()) == t.os.end())
        return {false, std::format("the system is {}", system)};
    if (!t.arch.empty() && std::ranges::find(t.arch, os::ARCH) == t.arch.end())
        return {false, std::format("the architecture is {}", arch)};
    for (const auto& [k, want] : t.env) {
        const auto have = os::env::get(k);
        if (want == "*" ? (!have || have->empty()) : have != want)
            return {false, std::format("{} is {}", k, have ? std::format("`{}`", *have) : "unset")};
    }
    for (const auto& c : t.all)
        if (const auto& a = c.answer(); !a.present)
            return {false, std::format("`{}` is absent ({})", c.name(), a.why)};
    for (const auto& c : t.none)
        if (c.answer().present)
            return {false, std::format("`{}` is present", c.name())};
    for (const auto& program : t.program)
        if (!os::env::find_executable(expand(program)))
            return {false, std::format("no {} on PATH", program)};
    if (!t.path.empty() &&
        std::ranges::none_of(t.path, [](const auto& g) { return some_file(expand(g)); }))
        return {false, std::format("no file matches {}", expand(t.path.front()))};
    // A system that starts no programs (the Web) has none of the functions that start one, and a
    // program that reaches them does not link there. So the question is answered where the target
    // is known: asked for a command, such a system says it starts none, and nothing of starting one
    // is in the program.
    if constexpr (os::is_emscripten) {
        if (!t.command.empty())
            return {false, std::format("{} cannot be started: this system starts no programs",
                                       t.command.front())};
    } else if (!t.command.empty()) {
        os::process::spawn_options o;
        o.program = os::fs::from_utf8(expand(t.command.front()));
        for (std::size_t i = 1; i < t.command.size(); ++i)
            o.args.push_back(expand(t.command[i]));
        o.in = os::process::stdio::null;
        o.out = os::process::stdio::null;
        o.err = os::process::stdio::null;
        os::process::bounds limits;
        limits.hard = std::chrono::seconds{30};
        auto r = os::process::run(std::move(o), limits);
        if (!r)
            return {false, std::format("{} did not start", t.command.front())};
        if (r->timed_out || !r->status.success())
            return {false,
                    std::format("{} ended with {}", t.command.front(),
                                r->timed_out ? std::string("a timeout")
                                             : std::format("status {}", r->status.shell_code()))};
    }
    if (t.test && !t.test())
        return {false, "its test does not hold"};
    return {true, "every test holds"};
}

// Only the words both name, when both name some.
void narrow(std::optional<std::vector<std::string>>& to, const std::vector<std::string>& words) {
    if (!to)
        to = words;
    else
        std::erase_if(*to, [&](const std::string& w) { return !holds_word(words, w); });
}

void restrict_to(attributes& a, const std::vector<place>& places) {
    std::vector<std::string> platforms, architectures;
    for (const auto& p : places)
        (p.architecture_word ? architectures : platforms).push_back(p.word);
    if (!platforms.empty())
        narrow(a.only_platforms, platforms);
    if (!architectures.empty())
        narrow(a.only_architectures, architectures);
}

void add_capability(attributes& a, const capability& c) {
    const auto tag = "requires:" + c.name();
    if (!holds_word(a.tags, tag))
        a.tags.push_back(tag);
    if (std::ranges::none_of(a.capabilities,
                             [&](const capability& x) { return x.name() == c.name(); }))
        a.capabilities.push_back(c);
}

}  // namespace

struct capability::state {
    std::string name;
    probe tests;
    std::once_flag once;
    capability_answer answer;
};

capability::capability(std::string name, probe tests) : state_(std::make_shared<state>()) {
    state_->name = std::move(name);
    state_->tests = std::move(tests);
}

const std::string& capability::name() const noexcept {
    return state_->name;
}

const capability_answer& capability::answer() const {
    std::call_once(state_->once, [this] { state_->answer = evaluate(state_->tests); });
    return state_->answer;
}

void apply(attributes& a, only o) {
    restrict_to(a, o.places);
}

void apply(attributes& a, except e) {
    for (const auto& p : e.places)
        if (!holds_word(a.excepted, p.word))
            a.excepted.push_back(p.word);
}

void apply(attributes& a, capability c) {
    add_capability(a, c);
}

std::optional<std::string> skip_reason(const attributes& a) {
    if (a.skip)
        return a.skip;
    const std::string system(os::name(os::host()));
    const std::string arch(os::name(os::ARCH));
    const auto outside = [](const auto& only, const std::string& w) {
        return only && !holds_word(*only, w);
    };
    if (outside(a.only_platforms, system) || outside(a.only_architectures, arch)) {
        std::vector<std::string> words = a.only_platforms.value_or(std::vector<std::string>{});
        for (const auto& w : a.only_architectures.value_or(std::vector<std::string>{}))
            words.push_back(w);
        return std::format("only on {} (this host: {})",
                           words.empty()
                               ? std::string("no place: its `only` statements exclude one another")
                               : joined(words),
                           this_host());
    }
    for (const auto& w : a.excepted)
        if (w == system || w == arch)
            return std::format("not on {} (this host: {})", w, this_host());
    for (const auto& c : a.capabilities)
        if (const auto& answer = c.answer(); !answer.present)
            return std::format("missing capability: {} ({})", c.name(), answer.why);
    return std::nullopt;
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
    add(a.uses, inner.uses);
    if (inner.monitoring)
        a.monitoring =
            std::max(a.monitoring.value_or(mtest::monitor_level::off), *inner.monitoring);
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
    a.bounds.insert(a.bounds.end(), inner.bounds.begin(), inner.bounds.end());
    if (inner.retries)
        a.retries = inner.retries;
    if (inner.diagnosis)
        a.diagnosis = inner.diagnosis;
    if (inner.only_platforms)
        narrow(a.only_platforms, *inner.only_platforms);
    if (inner.only_architectures)
        narrow(a.only_architectures, *inner.only_architectures);
    add(a.excepted, inner.excepted);
    for (const auto& c : inner.capabilities)
        add_capability(a, c);
    return a;
}

void detail::made_shared(std::string_view name, double ms) {
    write_event({{"e", "fixture"}, {"id", current_case()}, {"name", name}, {"ms", ms}});
}

std::vector<std::string> self_command() {
    return os::env::self_command();
}

child::child(std::string_view name, std::function<int(std::vector<std::string> args)> body) {
    auto& s = the();
    std::lock_guard g(s.lock);
    if (!s.children.emplace(std::string(name), std::move(body)).second)
        throw std::logic_error(std::format("mtest: two children are named {}", name));
}

std::vector<std::string> child_command(std::string_view name, std::span<const std::string> args) {
    auto argv = self_command();
    argv.emplace_back("--mtest-child");
    argv.emplace_back(name);
    argv.insert(argv.end(), args.begin(), args.end());
    return argv;
}

}  // namespace mcpplibs::mtest
