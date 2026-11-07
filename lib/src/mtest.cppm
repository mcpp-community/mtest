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
};

// Each is written in brackets after the case's name: "x"_test[tag("slow")][kind::e2e] = ...
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
    template <class F>
    requires std::invocable<F&>
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
    return {std::move(v.items), std::move(f), v.where};
}

class test {
public:
    explicit test(std::string_view name) : name_(name) {}
    test& operator[](tag t) {
        attrs_.tags.push_back(std::move(t.name));
        return *this;
    }
    test& operator[](mtest::kind k) {
        attrs_.kind = k;
        return *this;
    }
    test& operator[](clause c) {
        attrs_.clauses.push_back(std::move(c.id));
        return *this;
    }
    test& operator[](resource r) {
        attrs_.resources.push_back(std::move(r.name));
        return *this;
    }
    test& operator[](artifact a) {
        attrs_.artifacts.push_back(std::move(a.name));
        return *this;
    }
    test& operator[](expect_failure x) {
        attrs_.expected_failure = std::move(x.reason);
        return *this;
    }
    test& operator[](skip_if s) {
        if (s.condition)
            attrs_.skip = std::move(s.reason);
        return *this;
    }
    test& operator[](known_gap g);

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

// A group of cases declared when the program runs its suites: at namespace scope,
//   suite parsing = [] { "an empty line"_test = [] { ... }; };
// The suite's name prefixes its cases' identifiers ("parsing/an empty line").
class suite {
public:
    template <class F>
    requires std::invocable<F&>
    suite(F f, std::source_location where = std::source_location::current())
        : suite(std::string{}, std::function<void()>(std::move(f)), where) {}
    template <class F>
    requires std::invocable<F&>
    suite(std::string_view name, F f, std::source_location where = std::source_location::current())
        : suite(std::string(name), std::function<void()>(std::move(f)), where) {}
private:
    suite(std::string name, std::function<void()> f, std::source_location where);
};

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

}  // namespace mcpplibs::mtest
