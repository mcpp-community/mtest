// mcpplibs.mtest: cases, expectations, fixtures and attributes (spec/protocol.md).
//
// A case is a name and a body. Declared in `main`, it runs where it is declared;
// declared in a `suite`, it runs when the suite does. Under a runner (MTEST_PROTOCOL)
// a declaration in `list` mode only records the case, and in `run` mode the case
// runs when it is selected; every result is an event of the protocol. With no
// runner, the program prints one line per case and a summary.
export module mcpplibs.mtest;

import std;
import mcpplibs.os;

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

// ---- where a case runs ------------------------------------------------------

// The platforms and architectures of mcpplibs.os, and its two questions (0.8.0): whether this build
// can run on a platform, a constant, and the platform it runs on now, `host()`, a constant on a
// native build and known when an ok image is loaded. Code that compiles only for one platform is
// written in `on<P>([&](auto) { ... })`, whose body a build for another platform discards:
//
//   "reads the registry"_test[only{platform::windows}] = [] {
//       on<platform::windows>([&](auto) { expect(eq(registry_value(), 1)); });
//   };
using platform = mcpplibs::os::platform;
using architecture = mcpplibs::os::architecture;
using mcpplibs::os::can_run_on;
using mcpplibs::os::host;
using mcpplibs::os::on;

// A platform or an architecture, by mcpplibs.os's name for it ("windows", "aarch64").
struct place {
    std::string word;
    bool architecture_word = false;
    place(platform p) : word(mcpplibs::os::name(p)) {}
    place(architecture a) : word(mcpplibs::os::name(a)), architecture_word(true) {}
};

// WHAT THIS MACHINE CAN DO (0.8.0), declared once in code beside the tests that need it, by the
// tests of mtest.toml's [capabilities] (docs/config.md), and answered by the program itself, once
// per process, whichever runner starts it:
//
//   export const capability gcc{"gcc", {.os = {platform::linux}, .program = {"g++"}}};
//   export const capability pack{"pack", {.all = {gcc}, .program = {"patchelf"}}};
//   "links statically"_test[gcc] = [] { ... };
//
// A case that requires a capability this machine lacks is skipped, with the reason
// `missing capability: <name> (<the test that failed>)`; it carries the tag `requires:<name>`. A
// capability declared in a module interface is made before the units that import it, so suites at
// namespace scope may state it.
class capability;

struct probe {
    std::vector<platform> os;          // the system is one of them
    std::vector<architecture> arch;    // the architecture is one of them
    std::vector<std::string> program;  // each is found on PATH
    std::vector<std::string> path;     // some file matches one (`*`, `?`, `${NAME}`)
    std::vector<std::string> command;  // runs and exits 0 within 30 s
    std::vector<std::pair<std::string, std::string>> env;  // equals; "*": set and not empty
    std::vector<capability> all;                           // each is present
    std::vector<capability> none;                          // none is present
    std::function<bool()> test;                            // what the words above cannot state
};

struct capability_answer {
    bool present = false;
    std::string why;  // the test that decided it, in a person's words
};

class capability {
public:
    capability(std::string name, probe tests);
    const std::string& name() const noexcept;
    // Asked once per process; every copy shares the answer.
    const capability_answer& answer() const;
    explicit operator bool() const {
        return answer().present;
    }
private:
    struct state;
    std::shared_ptr<state> state_;
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
    // Where it runs (only, except): the platforms and the architectures it is restricted to, none
    // stated meaning every one, and the words of either on which it does not run.
    std::optional<std::vector<std::string>> only_platforms, only_architectures;
    std::vector<std::string> excepted;
    std::vector<capability> capabilities;  // what it requires of the machine
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

// The case runs only on the platforms and architectures named: on one of the platforms, when any is
// named, and on one of the architectures, when any is. Stated on a suite and on its case, both
// hold. Elsewhere it is skipped: `only on windows (this host: linux x86_64)`.
struct only {
    std::vector<place> places;
    only(std::initializer_list<place> p) : places(p) {}
};
// The case does not run on any platform or architecture named: `not on macos (this host: ...)`.
struct except {
    std::vector<place> places;
    except(std::initializer_list<place> p) : places(p) {}
};

// Each attribute applied to a set of them. Tags, clauses, resources, artifacts, bounds, the
// platforms not run on and the capabilities required add up, the platforms and architectures a
// case runs only on narrow, and a case within an isolated suite is isolated; a kind, a skip, an
// expected failure, a known gap, a time limit, retries and a diagnosis are the innermost one
// stated.
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
void apply(attributes& a, only o);
void apply(attributes& a, except e);
void apply(attributes& a, capability c);
// Why a case with these attributes does not run on this machine: its own skip, else a platform or
// an architecture it does not run on, else a capability the machine lacks.
std::optional<std::string> skip_reason(const attributes& a);

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
// THE COMMON KINDS ARE SHOWN AND COMPARED BY THE LIBRARY ITSELF (0.11.0). A file's first use of
// std::format makes GCC load and instantiate its machinery, 0.5 s of a file of one case; the
// library's own functions are compiled once, in the library.
std::string show_text(std::string_view text);
std::string show_signed(long long value);
std::string show_unsigned(unsigned long long value);
std::string show_floating(double value, bool single);
template <class T>
inline constexpr bool is_character = std::is_same_v<T, char> || std::is_same_v<T, wchar_t>
    || std::is_same_v<T, char8_t> || std::is_same_v<T, char16_t> || std::is_same_v<T, char32_t>
    || std::is_same_v<T, signed char> || std::is_same_v<T, unsigned char>;
template <class T>
std::string show(const T& value) {
    using U = std::remove_cvref_t<T>;
    if constexpr (std::is_same_v<U, bool>)
        return value ? "true" : "false";
    else if constexpr (std::is_same_v<U, float> || std::is_same_v<U, double>)
        return show_floating(value, std::is_same_v<U, float>);
    else if constexpr (std::is_integral_v<U> && !is_character<U> && std::is_signed_v<U>)
        return show_signed(value);
    else if constexpr (std::is_integral_v<U> && !is_character<U>)
        return show_unsigned(value);
    else if constexpr (std::is_convertible_v<const T&, std::string_view>)
        return show_text(std::string_view(value));
    else if constexpr (std::formattable<T, char>)
        return std::format("{}", value);
    else if constexpr (requires { std::to_underlying(value); })
        return show_signed(static_cast<long long>(std::to_underlying(value)));
    else
        return "<" + show_unsigned(sizeof(T)) + " bytes>";
}
// The text of a failed comparison: "a op b".
void describe(comparison& c, const char* op);
template <class A, class B>
comparison compare(bool held, const char* op, const A& a, const B& b) {
    comparison c;
    c.held = held;
    if (!held) {
        c.actual = show(a);
        c.expected = show(b);
        describe(c, op);
    }
    return c;
}

// A number or a truth value, compared in the library (0.11.0): integers by their values whatever
// their signedness (std::cmp_equal), a floating value with another number as double.
struct operand {
    enum class of : unsigned char { signed_integer, unsigned_integer, floating, single, boolean } kind;
    long long s = 0;
    unsigned long long u = 0;
    double d = 0;
};
enum class relation : unsigned char { eq, ne, lt, le, gt, ge };
comparison relate(const operand& a, relation r, const operand& b);
template <class T>
inline constexpr bool is_operand = std::is_same_v<T, bool> || std::is_same_v<T, float>
    || std::is_same_v<T, double> || (std::is_integral_v<T> && !is_character<T>);
template <class T>
operand operand_of(const T& v) {
    if constexpr (std::is_same_v<T, bool>)
        return {operand::of::boolean, v ? 1 : 0, 0, 0};
    else if constexpr (std::is_same_v<T, float>)
        return {operand::of::single, 0, 0, v};
    else if constexpr (std::is_same_v<T, double>)
        return {operand::of::floating, 0, 0, v};
    else if constexpr (std::is_signed_v<T>)
        return {operand::of::signed_integer, static_cast<long long>(v), 0, 0};
    else
        return {operand::of::unsigned_integer, 0, static_cast<unsigned long long>(v), 0};
}
template <class A, class B>
inline constexpr bool both_operands = is_operand<std::remove_cvref_t<A>> && is_operand<std::remove_cvref_t<B>>;
}  // namespace detail

template <class A, class B>
comparison eq(const A& a, const B& b) {
    if constexpr (detail::both_operands<A, B>)
        return detail::relate(detail::operand_of(a), detail::relation::eq, detail::operand_of(b));
    else
        return detail::compare(a == b, "==", a, b);
}
template <class A, class B>
comparison ne(const A& a, const B& b) {
    if constexpr (detail::both_operands<A, B>)
        return detail::relate(detail::operand_of(a), detail::relation::ne, detail::operand_of(b));
    else
        return detail::compare(a != b, "!=", a, b);
}
template <class A, class B>
comparison lt(const A& a, const B& b) {
    if constexpr (detail::both_operands<A, B>)
        return detail::relate(detail::operand_of(a), detail::relation::lt, detail::operand_of(b));
    else
        return detail::compare(a < b, "<", a, b);
}
template <class A, class B>
comparison le(const A& a, const B& b) {
    if constexpr (detail::both_operands<A, B>)
        return detail::relate(detail::operand_of(a), detail::relation::le, detail::operand_of(b));
    else
        return detail::compare(a <= b, "<=", a, b);
}
template <class A, class B>
comparison gt(const A& a, const B& b) {
    if constexpr (detail::both_operands<A, B>)
        return detail::relate(detail::operand_of(a), detail::relation::gt, detail::operand_of(b));
    else
        return detail::compare(a > b, ">", a, b);
}
template <class A, class B>
comparison ge(const A& a, const B& b) {
    if constexpr (detail::both_operands<A, B>)
        return detail::relate(detail::operand_of(a), detail::relation::ge, detail::operand_of(b));
    else
        return detail::compare(a >= b, ">=", a, b);
}
// |a - b| <= tolerance.
comparison near(double a, double b, double tolerance);
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

// A case's body, with the place it was written. The callable is the one the declaration names; a
// case runs while it is declared, so the body refers to it and does not own it (0.11.0: an owning
// std::function made every file instantiate it, a quarter of a second under GCC).
struct body {
    void (*call)(void*) = nullptr;
    void* object = nullptr;
    std::source_location where;
    // A body returns nothing: a case ends at its end, at `return;`, at skip() or at fail().
    template <class F>
    requires std::invocable<F&> && std::is_void_v<std::invoke_result_t<F&>>
    body(F&& f, std::source_location w = std::source_location::current())
        : call([](void* o) { (*static_cast<std::remove_reference_t<F>*>(o))(); }),
          object(const_cast<void*>(static_cast<const volatile void*>(std::addressof(f)))), where(w) {}
    void run() const {
        call(object);
    }
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

// A case declared inside another's body, by `"name"_test = [&] { ... };`. Its name and attributes
// are the library's (0.11.0): a file that declares a case makes no std::string of its own.
class test {
public:
    explicit test(std::string_view name);
    test(std::string_view name, attributes attrs);
    test(const char* name, std::size_t size);
    test(test&&) noexcept;
    test(const test&) = delete;
    test& operator=(const test&) = delete;
    ~test();
    template <attribute A>
    test& operator[](A a) {
        apply(attrs(), std::move(a));
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
            test one(std::string(name()) + "/" + label, attrs());
            auto& f = p.f;
            auto run = [&f, &item] { std::invoke(f, item); };
            one.operator=(body(run, p.where));
        }
    }
private:
    struct state;
    attributes& attrs();
    std::string_view name() const;
    state* state_ = nullptr;
};

inline test operator""_test(const char* name, std::size_t size) {
    return test(name, size);
}

// A name and the place it is written: the first argument of a suite, converted where the suite is
// declared, so that the suite's case is reported at its declaration. A literal is kept as its
// characters, without std::string_view (0.11.0), so that a file declaring a suite loads no more of
// the standard library than the suite needs.
struct name_here {
    const char* data = nullptr;
    std::size_t size = 0;  // all ones: up to the terminating NUL
    std::source_location where;
    template <class S>
    requires std::is_array_v<S> || std::convertible_to<const S&, std::string_view>
    name_here(const S& n, std::source_location w = std::source_location::current()) : where(w) {
        if constexpr (std::is_array_v<S>) {
            data = n;
            size = static_cast<std::size_t>(-1);
        } else {
            const std::string_view v(n);
            data = v.data();
            size = v.size();
        }
    }
};

namespace detail {
std::string name_of(const char* data, std::size_t size);
void add_suite(std::string name, std::source_location where, attributes attrs, std::function<void()> f);
// A name (NUL-terminated when `size` is all ones), its place, its kind (-1: none) and its body.
void add_plain(const char* name, std::size_t size, std::source_location where, int k, void (*f)());
template <class... A>
void declare_suite(name_here name, int k, A&&... args) {
    attributes attrs;
    if (k >= 0)
        attrs.kind = static_cast<mtest::kind>(k);
    constexpr std::size_t n = sizeof...(A);
    auto all = std::forward_as_tuple(std::forward<A>(args)...);
    using body_type = std::remove_cvref_t<std::tuple_element_t<n - 1, std::tuple<A...>>>;
    static_assert(std::invocable<body_type&>, "a suite ends with its body: [] { ... }");
    static_assert(std::is_void_v<std::invoke_result_t<body_type&>>,
                  "a body returns nothing: a case ends at its end, at `return;`, at skip() or "
                  "at fail()");
    [&]<std::size_t... I>(std::index_sequence<I...>) {
        (apply(attrs, std::remove_cvref_t<std::tuple_element_t<I, std::tuple<A...>>>(std::get<I>(all))),
         ...);
    }(std::make_index_sequence<n - 1>{});
    add_suite(name_of(name.data, name.size), name.where, std::move(attrs),
              std::function<void()>(std::get<n - 1>(all)));
}
template <class S>
void declare_plain(const S& name, int k, void (*f)(), std::source_location where) {
    if constexpr (std::is_array_v<S>) {
        add_plain(name, static_cast<std::size_t>(-1), where, k, f);
    } else {
        const std::string_view n(name);
        add_plain(n.data(), n.size(), where, k, f);
    }
}
template <class F>
void declare_unnamed(F f, std::source_location where) {
    static_assert(std::is_void_v<std::invoke_result_t<F&>>,
                  "a body returns nothing: a case ends at its end, at `return;`, at skip() or "
                  "at fail()");
    add_suite({}, where, attributes{}, std::function<void()>(std::move(f)));
}
}  // namespace detail

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
namespace detail {
// A body that captures nothing, which a suite at namespace scope always is: the library keeps a
// pointer to it, and a file that declares a suite of a name and a body instantiates nothing else.
template <class F>
inline constexpr bool plain_body = std::is_convertible_v<F, void (*)()>;
template <class... A>
inline constexpr bool one_plain_body = sizeof...(A) == 1 && (plain_body<std::remove_cvref_t<A>> && ...);
// The arguments of a declaration with attributes: what the plain form's own calls pass (a place)
// is none of them, so the declaration's overloads are set aside by their constraints before a
// name's conversion is examined, which would load std::string_view into the file.
template <class... A>
inline constexpr bool declared_form = sizeof...(A) >= 1 && !one_plain_body<A...>
    && (!std::is_same_v<std::remove_cvref_t<A>, std::source_location> && ...);
}  // namespace detail

class Suite {
public:
    template <class... A>
    requires detail::declared_form<A...>
    Suite(name_here name, A&&... args) {
        detail::declare_suite(name, -1, std::forward<A>(args)...);
    }
    template <class S, class F>
    requires detail::plain_body<F>
    Suite(const S& name, F f, std::source_location where = std::source_location::current()) {
        detail::declare_plain(name, -1, f, where);
    }
    template <class F>
    requires std::invocable<F&>
    Suite(F f, std::source_location where = std::source_location::current()) {
        detail::declare_unnamed(std::move(f), where);
    }
protected:
    template <class... A>
    requires detail::declared_form<A...>
    Suite(mtest::kind k, name_here name, A&&... args) {
        detail::declare_suite(name, static_cast<int>(k), std::forward<A>(args)...);
    }
    template <class S>
    Suite(mtest::kind k, const S& name, void (*f)(), std::source_location where) {
        detail::declare_plain(name, static_cast<int>(k), f, where);
    }
};

// Unit tests: a part of a program on its own.
class UTest : public Suite {
public:
    template <class... A>
    requires detail::declared_form<A...>
    UTest(name_here name, A&&... args) : Suite(kind::unit, name, std::forward<A>(args)...) {}
    template <class S, class F>
    requires detail::plain_body<F>
    UTest(const S& name, F f, std::source_location where = std::source_location::current())
        : Suite(kind::unit, name, static_cast<void (*)()>(f), where) {}
};
// Conformance to a specification (P6.4): cases that name the clauses they cover.
class CTS : public Suite {
public:
    template <class... A>
    requires detail::declared_form<A...>
    CTS(name_here name, A&&... args) : Suite(kind::cts, name, std::forward<A>(args)...) {}
    template <class S, class F>
    requires detail::plain_body<F>
    CTS(const S& name, F f, std::source_location where = std::source_location::current())
        : Suite(kind::cts, name, static_cast<void (*)()>(f), where) {}
};
// End-to-end tests: programs under test, run as their users run them (mcpplibs.mtest.e2e).
class E2E : public Suite {
public:
    template <class... A>
    requires detail::declared_form<A...>
    E2E(name_here name, A&&... args) : Suite(kind::e2e, name, std::forward<A>(args)...) {}
    template <class S, class F>
    requires detail::plain_body<F>
    E2E(const S& name, F f, std::source_location where = std::source_location::current())
        : Suite(kind::e2e, name, static_cast<void (*)()>(f), where) {}
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
// The same, for a `main` that receives its arguments, which a person gives a program run alone:
// `--list` prints the cases' identifiers; any other argument selects — a case by its identifier,
// the cases under a name (`suite`, `suite/group`: what lies before a `/`), or the cases a pattern
// with
// `*` (any run of characters) matches. A selection no case answers fails the run.
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

// A STEP OF THE RUNNING CASE (P5.6, 0.11.0): one command an end-to-end case ran, when it began, how
// long it took, the processor time of the programs it ran, their exit status, and the record of
// the build it was, when it was one. mcpplibs.mtest.e2e writes one for each command it runs; a kit
// that runs its commands another way writes its own. A step is a record and never a verdict.
struct step_record {
    std::string name;
    std::chrono::steady_clock::time_point began;
    std::chrono::nanoseconds took{0};
    std::optional<double> cpu_ms;
    std::optional<int> status;
    std::string build;
};
void step(const step_record& s);
// A check the running case makes at its end, before its verdict, once per `key` (0.11.0): a
// failure it records is the case's. mcpplibs.mtest.e2e checks the processes and the diagnostic
// lines of the programs a case ran this way.
void at_case_end(std::string_view key, std::function<void()> check);
// Items of the product's surface the running case used, in the product's own words (`command
// build`, `flag build --target`, `key build.sources`): the evidence a ledger of the surface reads
// (P5.6, 0.11.0).
void exercise(const std::vector<std::string>& items);
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
std::vector<std::string> child_command(std::string_view name,
                                       std::span<const std::string> args = {});

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
