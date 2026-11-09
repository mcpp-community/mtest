// mcpplibs.mtest: cases, expectations, fixtures and attributes (spec/protocol.md).
//
// A case is a name and a body. Declared in `main`, it runs where it is declared;
// declared in a `suite`, it runs when the suite does. Under a runner (MTEST_PROTOCOL)
// a declaration in `list` mode only records the case, and in `run` mode the case
// runs when it is selected; every result is an event of the protocol. With no
// runner, the program prints one line per case and a summary.
export module mcpplibs.mtest;

import std;

export namespace mcpplibs::mtest {

// ---- attributes ------------------------------------------------------------

enum class kind { unit, e2e, cts, bench };

// HOW CLOSELY A CASE'S USE OF THE MACHINE IS MEASURED (P10). `basic' (a runner's default):
// processor time per case and the program's largest resident set; `detailed': also the case's own
// largest resident set and growth, and the bytes it read and wrote; `trace': also processor and
// memory over time, sampled. Each is a metric of the case (P6.6) with its precision.
enum class monitor_level { off, basic, detailed, trace };

// What a runner does when a case fails: it runs the case once more, alone, with `env` added to its
// environment (a verbose log, a tracer's switch) and within `limit` (zero: the case's own), and
// keeps that run's output beside the result. The second run is a record, never a verdict.
struct diagnose {
    std::vector<std::pair<std::string, std::string>> env;
    std::chrono::milliseconds limit{0};
};

// A bound on a metric the case reports (P6.5): its name, and the most and the least it may be.
struct metric_bound {
    std::string metric;
    std::optional<double> max, min;
};

struct attributes {
    std::optional<mtest::kind> kind;
    std::vector<std::string> tags;
    std::vector<std::string> clauses;  // specification clauses the case covers (P6.4)
    std::vector<std::string>
        resources;  // `serial:<group>`, `machine`, `gpu`, `network`, `display` (P7.1)
    std::vector<std::string> artifacts;  // `target:<name>`, `file:<path>`, `program:<name>` (P7.2)
    std::optional<std::string> expected_failure;  // the reason the case is expected to fail
    std::optional<std::string> skip;              // the reason the case does not run
    std::optional<std::string> gap_due;           // a known gap's date (YYYY-MM-DD); see known_gap
    std::optional<std::chrono::milliseconds> limit;  // how long the case may run (timeout)
    bool isolated = false;                           // runs in a process of its own (isolate)
    std::optional<int> retries;                      // runs again after failing (retries)
    std::optional<mtest::diagnose> diagnosis;        // what a runner does after it fails (diagnose)
    std::vector<metric_bound> bounds;                // what its metrics may be (at_most, at_least)
    std::optional<mtest::monitor_level> monitoring;  // how closely its use is measured (monitor)
    std::vector<std::string> uses;                   // the run fixtures it uses (P11)
};

// Each is written in brackets after the case's name, "x"_test[tag{"slow"}][resource{"machine"}],
// or after a suite's name, E2E cli{"cli", resource{"serial:home"}, [] { ... }}, where every case
// of the suite has it.
struct tag {
    std::string name;
};
struct clause {
    std::string id;
};
struct resource {
    std::string name;
};
struct artifact {
    std::string name;
};
struct expect_failure {
    std::string reason;
};
struct skip_if {
    bool condition;
    std::string reason;
};
// A known gap of an implementation (a conformance case it does not meet yet): the case is
// expected to fail until `due` (YYYY-MM-DD); from that day on it is judged as any case, so a
// gap cannot outlive its plan.
struct known_gap {
    std::string reason;
    std::string due;
};
// How long the case may run; a runner that starts a process per case ends it then (P3.3).
struct timeout {
    std::chrono::milliseconds limit;
};
// The case runs in a process of its own: what it leaves in its process reaches no other case.
struct isolate {};
// A failing case runs again, alone, up to `count` times; one that then passes is flaky, which the
// runner records and does not count as failed.
struct retries {
    int count;
};
// The metric `metric` of the case is at most `value`, or at least it, in the metric's unit: an
// expectation the case's end checks (P6.5), and that a runner and `mtest analyze trend` read:
//   Bench startup{"startup", at_most{"ready", 15}, [] { metric("ready", seconds, "s"); }};
struct at_most {
    std::string metric;
    double value;
};
struct at_least {
    std::string metric;
    double value;
};
// The case is measured at least this closely: monitor{monitor_level::detailed}.
struct monitor {
    mtest::monitor_level level = mtest::monitor_level::detailed;
};
// The case uses the run fixture `fixture' (P11): a runner makes one for the run before the first
// case that uses it, and gives its endpoint to them (fixture_endpoint).
struct uses {
    std::string fixture;
};

// Each attribute applied to a set of them. Tags, clauses, resources, artifacts and bounds add up,
// and a case within an isolated suite is isolated; a kind, a skip, an expected failure, a known
// gap, a time limit, retries and a diagnosis are the innermost one stated.
inline void apply(attributes& a, mtest::kind k) {
    a.kind = k;
}
inline void apply(attributes& a, tag t) {
    a.tags.push_back(std::move(t.name));
}
inline void apply(attributes& a, clause c) {
    a.clauses.push_back(std::move(c.id));
}
inline void apply(attributes& a, resource r) {
    a.resources.push_back(std::move(r.name));
}
inline void apply(attributes& a, artifact x) {
    a.artifacts.push_back(std::move(x.name));
}
inline void apply(attributes& a, expect_failure x) {
    a.expected_failure = std::move(x.reason);
}
inline void apply(attributes& a, skip_if s) {
    if (s.condition)
        a.skip = std::move(s.reason);
}
inline void apply(attributes& a, timeout t) {
    a.limit = t.limit;
}
inline void apply(attributes& a, isolate) {
    a.isolated = true;
}
inline void apply(attributes& a, retries r) {
    a.retries = r.count;
}
inline void apply(attributes& a, diagnose d) {
    a.diagnosis = std::move(d);
}
inline void apply(attributes& a, at_most b) {
    a.bounds.push_back({std::move(b.metric), b.value, std::nullopt});
}
inline void apply(attributes& a, at_least b) {
    a.bounds.push_back({std::move(b.metric), std::nullopt, b.value});
}
inline void apply(attributes& a, monitor m) {
    a.monitoring = m.level;
}
inline void apply(attributes& a, uses u) {
    a.uses.push_back(std::move(u.fixture));
}
void apply(attributes& a, known_gap g);

template <class A>
concept attribute = requires(attributes& set, A a) { apply(set, std::move(a)); };

// `inner` stated within `outer`: a case's own within its suite's, a nested case's within its
// case's.
attributes within(const attributes& outer, const attributes& inner);

// ---- expectations ----------------------------------------------------------

// The value of a comparison and the two sides as text, for a failure's report.
struct comparison {
    bool held = false;
    std::string expression;  // "a == b"
    std::string expected;
    std::string actual;
    explicit operator bool() const noexcept {
        return held;
    }
};

namespace detail {
template <class T>
std::string show(const T& value) {
    if constexpr (std::is_same_v<std::remove_cvref_t<T>, bool>)
        return value ? "true" : "false";
    else if constexpr (std::is_convertible_v<const T&, std::string_view>)
        return std::format("\"{}\"", std::string_view(value));
    else if constexpr (std::formattable<T, char>)
        return std::format("{}", value);
    else if constexpr (requires { std::to_underlying(value); })
        return std::format("{}", std::to_underlying(value));
    else
        return std::format("<{} bytes>", sizeof(T));
}
template <class A, class B>
comparison compare(bool held, std::string_view op, const A& a, const B& b) {
    comparison c;
    c.held = held;
    if (!held) {
        c.actual = show(a);
        c.expected = show(b);
        c.expression = std::format("{} {} {}", c.actual, op, c.expected);
    }
    return c;
}
}  // namespace detail

template <class A, class B>
comparison eq(const A& a, const B& b) {
    return detail::compare(a == b, "==", a, b);
}
template <class A, class B>
comparison ne(const A& a, const B& b) {
    return detail::compare(a != b, "!=", a, b);
}
template <class A, class B>
comparison lt(const A& a, const B& b) {
    return detail::compare(a < b, "<", a, b);
}
template <class A, class B>
comparison le(const A& a, const B& b) {
    return detail::compare(a <= b, "<=", a, b);
}
template <class A, class B>
comparison gt(const A& a, const B& b) {
    return detail::compare(a > b, ">", a, b);
}
template <class A, class B>
comparison ge(const A& a, const B& b) {
    return detail::compare(a >= b, ">=", a, b);
}
// |a - b| <= tolerance.
inline comparison near(double a, double b, double tolerance) {
    auto c = detail::compare(std::abs(a - b) <= tolerance, "~", a, b);
    if (!c.held)
        c.expression += std::format(" (tolerance {})", tolerance);
    return c;
}
// `text` contains `part`.
comparison contains(std::string_view text, std::string_view part);
// `body` throws an exception of type E (any exception for E = void).
template <class E = void, class F>
comparison throws(F&& body) {
    comparison c;
    try {
        std::invoke(body);
        c.expression = "no exception was thrown";
    } catch (const std::conditional_t<std::is_void_v<E>, std::exception, E>&) {
        c.held = true;
    } catch (...) {
        c.held = std::is_void_v<E>;
        if (!c.held)
            c.expression = "another exception was thrown";
    }
    return c;
}
template <class F>
comparison nothrow(F&& body) {
    comparison c;
    try {
        std::invoke(body);
        c.held = true;
    } catch (const std::exception& e) {
        c.expression = std::format("threw: {}", e.what());
    } catch (...) {
        c.expression = "threw an exception that is not a std::exception";
    }
    return c;
}

// A condition whose failure ends the case.
struct fatal_condition {
    comparison value;
};
inline fatal_condition fatal(bool held) {
    comparison c;
    c.held = held;
    return {std::move(c)};
}
inline fatal_condition fatal(comparison c) {
    return {std::move(c)};
}

// The report of one expectation; a message is appended with <<, and is formed only when it failed.
class expectation {
public:
    expectation(comparison value, bool is_fatal, std::source_location where);
    expectation(const expectation&) = delete;
    expectation& operator=(const expectation&) = delete;
    ~expectation() noexcept(false);

    template <class T>
    expectation& operator<<(const T& value) {
        if (!held_) {
            if constexpr (std::is_convertible_v<const T&, std::string_view>)
                message_ += std::string_view(value);
            else
                message_ += detail::show(value);
        }
        return *this;
    }
    explicit operator bool() const noexcept {
        return held_;
    }
private:
    bool held_;
    bool fatal_;
    comparison value_;
    std::source_location where_;
    std::string message_;
};

expectation expect(bool held, std::source_location where = std::source_location::current());
expectation expect(comparison value, std::source_location where = std::source_location::current());
expectation expect(fatal_condition value,
                   std::source_location where = std::source_location::current());

// Ends the current case as skipped.
[[noreturn]] void skip(std::string reason);
// Ends the current case as failed.
[[noreturn]] void fail(std::string message,
                       std::source_location where = std::source_location::current());

// ---- cases ------------------------------------------------------------------

// A case's body, with the place it was written.
struct body {
    std::function<void()> run;
    std::source_location where;
    // A body returns nothing: a case ends at its end, at `return;`, at skip() or at fail().
    template <class F>
    requires std::invocable<F&> && std::is_void_v<std::invoke_result_t<F&>>
    body(F f, std::source_location w = std::source_location::current())
        : run(std::move(f)), where(w) {}
};

// A case of several values: "x"_test = values{1, 2, 3} | [](int v) { ... };
template <class T>
struct values {
    std::vector<T> items;
    std::source_location where;
    values(std::initializer_list<T> list, std::source_location w = std::source_location::current())
        : items(list), where(w) {}
    explicit values(std::vector<T> list, std::source_location w = std::source_location::current())
        : items(std::move(list)), where(w) {}
};
template <class T, class F>
struct parameterized {
    std::vector<T> items;
    F f;
    std::source_location where;
};
template <class T, class F>
requires std::invocable<F&, const T&>
parameterized<T, F> operator|(values<T> v, F f) {
    static_assert(std::is_void_v<std::invoke_result_t<F&, const T&>>,
                  "a body returns nothing: a case ends at its end, at `return;`, at skip() or at "
                  "fail()");
    return {std::move(v.items), std::move(f), v.where};
}

class test {
public:
    explicit test(std::string_view name) : name_(name) {}
    test(std::string_view name, attributes attrs) : name_(name), attrs_(std::move(attrs)) {}
    template <attribute A>
    test& operator[](A a) {
        apply(attrs_, std::move(a));
        return *this;
    }

    // Declares the case: lists it, runs it, or passes over it, as the runner asks.
    void operator=(body b);
    template <class T, class F>
    void operator=(parameterized<T, F> p) {
        for (std::size_t i = 0; i < p.items.size(); ++i) {
            const auto& item = p.items[i];
            std::string label = [&] {
                if constexpr (std::formattable<T, char>)
                    return std::format("{}", item);
                else
                    return std::to_string(i);
            }();
            test one(std::format("{}/{}", name_, label));
            one.attrs_ = attrs_;
            auto& f = p.f;
            one.operator=(body([&f, &item] { std::invoke(f, item); }, p.where));
        }
    }
private:
    std::string name_;
    attributes attrs_;
};

inline test operator""_test(const char* name, std::size_t size) {
    return test(std::string_view(name, size));
}

// A name and the place it is written: the first argument of a suite, converted where the suite is
// declared, so that the suite's case is reported at its declaration.
struct name_here {
    std::string_view name;
    std::source_location where;
    template <class S>
    requires std::convertible_to<const S&, std::string_view>
    name_here(const S& n, std::source_location w = std::source_location::current())
        : name(n), where(w) {}
};

// A case declared by an object at namespace scope:
//
//   UTest parsing{"parsing", [] { expect(eq(parse(""), 0)); }};
//   E2E   cli{"cli", resource{"serial:home"}, timeout{std::chrono::minutes{2}}, [] {
//       "prints its version"_test = [] { ... };
//       "reads its configuration"_test = [] { ... };
//   }};
//
// Making the object records its name, its attributes and its body; nothing runs. A runner lists it
// as one case with those attributes, selects it, and schedules it by them (resources, isolation,
// time limit, retries, diagnosis); its body runs when it is selected. Cases the body declares are
// its own: their identifiers are under its name ("cli/prints its version"), they have its
// attributes and add their own, each can be selected alone, and the body's other statements --
// what its cases share -- run only when one of them is. So one object is a single test, or a group
// of tests, or both.
//
// A `Suite` states no kind: its case is of the kind its attributes state, else of the program's (a
// program under `tests/e2e/` is e2e, mtest docs/layout.md). UTest, CTS, E2E and Bench are of one
// kind each. A suite without a name is no case: its body runs when the program does, and declares
// cases of its own scope:
//   Suite all = [] { "a"_test = [] { ... }; };
class Suite {
public:
    template <class... A>
    requires(sizeof...(A) >= 1)
    Suite(name_here name, A&&... args) {
        declare(name, attributes{}, std::forward<A>(args)...);
    }
    template <class F>
    requires std::invocable<F&>
    Suite(F f, std::source_location where = std::source_location::current()) {
        static_assert(std::is_void_v<std::invoke_result_t<F&>>,
                      "a body returns nothing: a case ends at its end, at `return;`, at skip() or "
                      "at fail()");
        add({}, where, attributes{}, std::function<void()>(std::move(f)));
    }
protected:
    template <class... A>
    Suite(mtest::kind k, name_here name, A&&... args) {
        attributes base;
        base.kind = k;
        declare(name, std::move(base), std::forward<A>(args)...);
    }
private:
    template <class... A>
    void declare(name_here name, attributes attrs, A&&... args) {
        constexpr std::size_t n = sizeof...(A);
        auto all = std::forward_as_tuple(std::forward<A>(args)...);
        using body_type = std::remove_cvref_t<std::tuple_element_t<n - 1, std::tuple<A...>>>;
        static_assert(std::invocable<body_type&>, "a suite ends with its body: [] { ... }");
        static_assert(std::is_void_v<std::invoke_result_t<body_type&>>,
                      "a body returns nothing: a case ends at its end, at `return;`, at skip() or "
                      "at fail()");
        [&]<std::size_t... I>(std::index_sequence<I...>) {
            (apply(attrs, std::remove_cvref_t<std::tuple_element_t<I, std::tuple<A...>>>(
                              std::get<I>(all))),
             ...);
        }(std::make_index_sequence<n - 1>{});
        add(std::string(name.name), name.where, std::move(attrs),
            std::function<void()>(std::get<n - 1>(all)));
    }
    static void add(std::string name, std::source_location where, attributes attrs,
                    std::function<void()> f);
};

// Unit tests: a part of a program on its own.
class UTest : public Suite {
public:
    template <class... A>
    requires(sizeof...(A) >= 1)
    UTest(name_here name, A&&... args) : Suite(kind::unit, name, std::forward<A>(args)...) {}
};
// Conformance to a specification (P6.4): cases that name the clauses they cover.
class CTS : public Suite {
public:
    template <class... A>
    requires(sizeof...(A) >= 1)
    CTS(name_here name, A&&... args) : Suite(kind::cts, name, std::forward<A>(args)...) {}
};
// End-to-end tests: programs under test, run as their users run them (mcpplibs.mtest.e2e).
class E2E : public Suite {
public:
    template <class... A>
    requires(sizeof...(A) >= 1)
    E2E(name_here name, A&&... args) : Suite(kind::e2e, name, std::forward<A>(args)...) {}
};
// Measurements (mcpplibs.mtest.bench). A measurement means something only on a machine nothing
// else uses, so a Bench holds `machine` (P7.1).
class Bench : public Suite {
public:
    template <class... A>
    requires(sizeof...(A) >= 1)
    Bench(name_here name, A&&... args)
        : Suite(kind::bench, name, resource{"machine"}, std::forward<A>(args)...) {}
};

// The spelling of 0.1 and 0.2.
using suite = Suite;

// Runs every suite not yet run, writes the summary (the `done` event under a
// runner), and returns the program's exit status (P5.5).
int report();
// The same, for a `main` that receives its arguments: `--list` prints the cases'
// identifiers, and the remaining arguments select cases by identifier prefix.
int run(int argc, char** argv);

// ---- fixtures ---------------------------------------------------------------

// A directory of the current case, removed when the case ends (MTEST_TMPDIR when a runner gives
// one).
std::filesystem::path temp_dir();
// A suite's object that cases share, made when the first case that runs uses it and destroyed when
// the suite's body returns: nothing is made when the program lists its cases or runs none of them.
//
//   CTS lsp{"lsp", [] {
//       shared<server> s{[] { return server{payload()}; }};
//       "status"_test = [&] { expect(s->ready()); };
//   }};
//
// Its making is reported with the case that caused it (P5.2 `fixture`: the name, how long it took),
// so that a runner sees what a suite's cases share and what sharing saves.
namespace detail {
void made_shared(std::string_view name, double ms);
}
template <class T>
class shared {
public:
    explicit shared(std::string_view name = {})
    requires std::default_initializable<T>
        : name_(name), make_([] { return std::make_unique<T>(); }) {}
    template <class F>
    requires std::invocable<F&> && std::constructible_from<T, std::invoke_result_t<F&>>
    explicit shared(F make) : shared(std::string_view{}, std::move(make)) {}
    template <class F>
    requires std::invocable<F&> && std::constructible_from<T, std::invoke_result_t<F&>>
    shared(std::string_view name, F make)
        : name_(name),
          make_([make = std::move(make)]() mutable { return std::make_unique<T>(make()); }) {}
    shared(const shared&) = delete;
    shared& operator=(const shared&) = delete;

    T& operator*() {
        if (!value_) {
            const auto began = std::chrono::steady_clock::now();
            value_ = make_();
            detail::made_shared(name_, std::chrono::duration<double, std::milli>(
                                           std::chrono::steady_clock::now() - began)
                                           .count());
        }
        return *value_;
    }
    T* operator->() {
        return &**this;
    }
    // Whether a case has used it.
    bool made() const noexcept {
        return value_ != nullptr;
    }
private:
    std::string name_;
    std::function<std::unique_ptr<T>()> make_;
    std::unique_ptr<T> value_;
};

// The seed of anything random: MTEST_SEED, else one chosen at start and printed with a failure.
std::uint64_t seed();
// The identifier of the case that runs now; empty outside a case.
std::string current_case();
// A file the case produced, reported with the case (P5.2 `artifact`).
void produced(const std::filesystem::path& file, std::string_view kind = "file");
// A metric of the current case (P6). `samples` may be empty.
void metric(std::string_view name, double value, std::string_view unit,
            std::span<const double> samples = {}, std::string_view better = "lower");
// Whether a runner asked for events (MTEST_PROTOCOL and MTEST_REPORT).
bool under_runner();
// The clauses of every case declared so far (P6.4), for a coverage check (mcpplibs.mtest.cts).
std::set<std::string> declared_clauses();
// This program again, as the system names it, for a test that starts copies of itself.
std::vector<std::string> self_command();

// A COPY OF THIS PROGRAM THAT DOES SOMETHING ELSE. A test that starts copies of itself -- the
// child process under test, a producer its cases talk to -- declares what a copy does as a
// `child`, and starts one with `child_command`. The entry enters the child named by the copy's
// arguments before any case, so a program of many files keeps mtest's one `main` however many of
// them start copies. The body receives the arguments after the name and returns the copy's exit
// status; the copy runs no case and reports nothing.
class child {
public:
    child(std::string_view name, std::function<int(std::vector<std::string> args)> body);
};
// This program again, as the child `name`, given `args`.
std::vector<std::string> child_command(std::string_view name, std::span<const std::string> args = {});

// ---- components (P11) ---------------------------------------------------------

// What a run fixture's serving is given: `ready` publishes the endpoint the cases that use it
// receive (a directory, an address); `stopped` returns when the runner ends the fixture.
class serving {
public:
    void ready(std::string_view endpoint);
    void stopped();
};
// A RUN FIXTURE: one thing the whole run shares -- a sandboxed home, a server, an index -- served
// by any program that declares it, for the cases of every program that use it:
//
//   run_fixture index{"index", [](serving& s) {
//       auto dir = make_index();
//       s.ready(dir.string());
//       s.stopped();
//   }};
//   E2E fetch{"fetch", uses{"index"}, [] { auto root = fixture_endpoint("index"); ... }};
//
// A runner lists it (a `component` event), starts one program that declares it with MTEST_SERVE
// naming it before the first case that uses it, gives its endpoint as MTEST_FIXTURE_<NAME>, and
// ends it after the last: a node of the run's graph.
class run_fixture {
public:
    run_fixture(std::string_view name, std::function<void(serving&)> serve,
                std::source_location where = std::source_location::current());
};
// A resource of limited number: at most `capacity` cases hold it at once (P7.3), where P7.1's
// exclusive resources have one.
struct resource_pool {
    std::string name;
    int capacity = 1;
};
// A COMPONENT: a reusable part of tests, named, with the pools it brings. Its fixtures are its
// run_fixture objects and its case types are C++ types of its library; the component makes them
// known to a runner together (`component` event).
class Component {
public:
    Component(std::string_view name, std::vector<resource_pool> pools = {},
              std::source_location where = std::source_location::current());
};
// The endpoint of a run fixture the current case uses; empty outside a runner that serves it.
std::string fixture_endpoint(std::string_view name);

}  // namespace mcpplibs::mtest
