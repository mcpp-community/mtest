// mcpplibs.mtest.bench: measurements as a case's metric (spec/protocol.md P6).
//
//   "parse"_test[kind::bench] = [] { bench::measure("parse", [] { parse(text); }); };
//
// Each sample is the time of one iteration in seconds, over as many iterations as
// fill `sample_time`. The samples are reported; whether they pass is the runner's
// policy (P6.2), never the program's.
export module mcpplibs.mtest.bench;

import std;
export import mcpplibs.mtest;

export namespace mcpplibs::mtest::bench {

struct options {
    int samples = 20;
    int warmup = 2;
    std::chrono::nanoseconds sample_time = std::chrono::milliseconds{10};
};

struct summary {
    std::vector<double> samples;  // seconds per iteration
    std::size_t iterations = 0;   // per sample
    double median = 0, min = 0, max = 0;
    double mad = 0;  // median absolute deviation
};

// Keeps `value` from being optimised away.
template <class T>
void keep(const T& value) {
    static const volatile void* sink;
    sink = &value;
    std::atomic_signal_fence(std::memory_order_seq_cst);
}

summary summarize(std::vector<double> samples, std::size_t iterations);
// Reports `s` as the metric `name` of the current case.
void record(std::string_view name, const summary& s);

template <class F>
requires std::invocable<F&>
summary measure(std::string_view name, F&& f, options o = {}) {
    using clock = std::chrono::steady_clock;
    std::size_t iterations = 1;
    for (;;) {
        const auto t0 = clock::now();
        for (std::size_t i = 0; i < iterations; ++i)
            std::invoke(f);
        if (clock::now() - t0 >= o.sample_time || iterations >= (std::size_t{1} << 30))
            break;
        iterations *= 2;
    }
    for (int i = 0; i < o.warmup; ++i)
        for (std::size_t k = 0; k < iterations; ++k)
            std::invoke(f);
    std::vector<double> samples;
    samples.reserve(static_cast<std::size_t>(o.samples));
    for (int i = 0; i < o.samples; ++i) {
        const auto t0 = clock::now();
        for (std::size_t k = 0; k < iterations; ++k)
            std::invoke(f);
        samples.push_back(std::chrono::duration<double>(clock::now() - t0).count() /
                          double(iterations));
    }
    auto s = summarize(std::move(samples), iterations);
    record(name, s);
    return s;
}

// EACH CALL MEASURED BY ITSELF. `measure` times batches and reports what one call costs on the
// whole; `each` times every call, for work whose calls differ and whose slow ones are the point --
// a frame, a request, a step of a simulation -- and reports where the calls lie: the median as
// `name`, and `name.p95`, `name.p99`, `name.max`, `name.mean`, each a metric a bound can name:
//   Bench frame{"frame", at_most{"draw.p95", 0.016}, [] {
//       bench::each("draw", [&](bench::lap& l) {
//           l.pause(); scene.advance(); l.resume();   // not part of what is measured
//           scene.draw();
//           l.items(scene.nodes());                   // what this call worked through
//       });
//   }};
// A call may also say what it was made of, so a slow call explains itself:
//   l.part("encode", spent)   a part of the call the body timed (or `auto t = l.time("encode");`,
//   to
//                             the end of `t`): `draw.encode`, with `.p95`, `.p99`, `.max`, `.mean`
//   l.value("glyphs", n)      a quantity of this call: `draw.glyphs` its mean, `draw.glyphs.max`
//   l.say("zoom 0.43")        words about this call
// and with `slowest = N` the N slowest calls are stated with all of it, in the case's output:
//   draw: call 412 took 9.100 ms: encode 7.900 ms, glyphs 24570 -- zoom 0.43
struct each_options {
    std::size_t least = 12;                                          // calls measured, at least
    std::size_t most = 100'000;                                      // and at most
    std::chrono::nanoseconds time = std::chrono::milliseconds{400};  // measured for this long
    std::size_t warmup = 3;   // calls before the first measured one
    std::size_t slowest = 0;  // the slowest calls stated one by one, this many
    // What one call may take (a frame's 8.33 ms). Stated: `name.over` is how many calls took more,
    // `name.over.share` their part of all, and those calls are the ones stated one by one where
    // `slowest` is 0 (the eight slowest of them).
    std::chrono::nanoseconds limit{0};
};

// HEAP ALLOCATIONS A CALL. The library replaces no `operator new`; a program that counts replaces
// it and says so from there:
//   void* operator new(std::size_t n) { bench::allocated(n); return std::malloc(n); }
// `each` then reports `name.allocations` (the mean a call) and `name.allocations.max`, and
// `name.allocated` (bytes, the mean) -- less what a call paused for;
// `at_most{"draw.allocations.max", 0}` is a frame that allocates nothing. A program that never says
// so reports none of them.
void allocated(std::size_t bytes) noexcept;
struct allocation_count {
    std::uint64_t count = 0, bytes = 0;
};
allocation_count allocations() noexcept;

// One call: what the body may say of it.
class lap {
public:
    using clock = std::chrono::steady_clock;
    // The time from `pause` to `resume` is not this call's.
    void pause() noexcept {
        if (!paused_)
            paused_ = true, paused_at_ = clock::now(), paused_with_ = allocations();
    }
    void resume() noexcept {
        if (paused_) {
            const allocation_count now = allocations();
            out_count_ += now.count - paused_with_.count;
            out_bytes_ += now.bytes - paused_with_.bytes;
            paused_ = false, out_ += clock::now() - paused_at_;
        }
    }
    // What this call worked through (nodes, cells, bytes): `name.per_item` is the mean time of one.
    void items(std::uint64_t n) noexcept {
        items_ = n;
    }
    // Which call this is, from 0, the warm-up's counted; and whether it is one of the warm-up's.
    std::size_t index() const noexcept {
        return index_;
    }
    bool warmup() const noexcept {
        return warmup_;
    }
    // A part of this call, timed by the body. Said again in the same call: added to.
    void part(std::string_view name, clock::duration spent);
    // The part `name` is the time from here to the end of what this returns.
    class timed {
    public:
        timed(lap& of, std::string_view name) : of_(of), name_(name), from_(clock::now()) {}
        timed(const timed&) = delete;
        timed& operator=(const timed&) = delete;
        ~timed() {
            of_.part(name_, clock::now() - from_);
        }
    private:
        lap& of_;
        std::string name_;
        clock::time_point from_;
    };
    [[nodiscard]] timed time(std::string_view name) {
        return timed{*this, name};
    }
    // A quantity of this call (what it drew, what it sent). Said again in the same call: replaced.
    void value(std::string_view name, double v);
    // This call is the last: what is measured has run out (a recorded session's last frame).
    void last() noexcept {
        last_ = true;
    }
    // Words about this call, stated with it when it is among the slowest.
    void say(std::string text) {
        now_.said = std::move(text);
    }
private:
    friend struct lap_access;
    bool paused_ = false, warmup_ = false, last_ = false;
    clock::time_point paused_at_{};
    clock::duration out_{};
    allocation_count paused_with_{}, began_with_{};
    std::uint64_t out_count_ = 0, out_bytes_ = 0;
    std::uint64_t items_ = 0;
    std::size_t index_ = 0;
    // What the calls said: a column a name, a row a measured call (not a number where a call did
    // not say).
    struct row {
        std::vector<double> parts, values;
        std::string said;
    };
    std::uint64_t now_count_ = 0, now_bytes_ = 0;  // the call that ended allocated
    std::vector<std::string> part_names_, value_names_;
    row now_;
    std::vector<row> rows_;
};

// What `each` does with a lap (not for a test).
struct lap_access {
    static void warmup(lap& l, bool on) noexcept {
        l.warmup_ = on;
    }
    static void begin(lap& l) noexcept {
        l.paused_ = false, l.out_ = {}, l.out_count_ = l.out_bytes_ = 0;
        l.began_with_ = allocations();
    }
    static lap::clock::duration end(lap& l) noexcept {
        l.resume();
        ++l.index_;
        const allocation_count now = allocations();
        l.now_count_ = now.count - l.began_with_.count - l.out_count_;
        l.now_bytes_ = now.bytes - l.began_with_.bytes - l.out_bytes_;
        return l.out_;
    }
    // The call that ended was a measured one: what it said is kept, in the order of the samples.
    static void keep(lap& l);
    // What it said is dropped (a warm-up call's).
    static void drop(lap& l);
    static bool last(const lap& l) noexcept {
        return l.last_;
    }
    // What the measured calls said.
    struct view {
        std::span<const std::string> parts, values;
        std::span<const lap::row> rows;
    };
    static view look(const lap& l) noexcept {
        return {l.part_names_, l.value_names_, l.rows_};
    }
    static std::uint64_t items(const lap& l) noexcept {
        return l.items_;
    }
};

// Where one part (seconds) or one quantity lies over the calls that said it.
struct quantity {
    std::string name;
    std::size_t calls = 0;  // that said it
    double min = 0, mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
};

// One call, with what it said.
struct slow_call {
    std::size_t index = 0;  // among the measured calls, from 0
    double seconds = 0;
    std::vector<std::pair<std::string, double>> parts, values;
    std::string said;
};

struct distribution {
    std::vector<double> samples;  // seconds a call, in the order measured
    double min = 0, mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
    std::uint64_t items = 0;              // a call's, as the last call that said so stated it
    double per_item = 0;                  // mean / items; 0 when no call stated any
    std::vector<quantity> parts, values;  // what the calls said (lap::part, lap::value)
    std::vector<slow_call> slowest;       // the slowest first (each_options::slowest)
    double limit = 0;      // seconds a call may take (each_options::limit); 0: none stated
    std::size_t over = 0;  // the calls that took more
};

// The value `q` of the way (0 to 1) through `sorted`, between the two nearest by position.
double percentile(std::span<const double> sorted, double q);
distribution distribute(std::vector<double> samples, std::uint64_t items = 0);
// The same, with what the calls of `l` said and the `slowest` of them one by one.
distribution distribute(std::vector<double> samples, const lap& l, std::size_t slowest,
                        double limit = 0);
// The metrics of a distribution (see `each`), in seconds; `per_item` when a call stated items; a
// part's as the calls' (`name.<part>`, `.p95`, `.p99`, `.max`, `.mean`), a quantity's mean and
// `.max`; the slowest calls a line each in the case's output.
void record(std::string_view name, const distribution& d);

template <class F>
requires std::invocable<F&, lap&> || std::invocable<F&>
distribution each(std::string_view name, F&& f, each_options o = {}) {
    using clock = lap::clock;
    lap l;
    const auto call = [&] {
        lap_access::begin(l);
        const auto t0 = clock::now();
        if constexpr (std::invocable<F&, lap&>)
            std::invoke(f, l);
        else
            std::invoke(f);
        const auto t1 = clock::now();
        return std::max(clock::duration{}, t1 - t0 - lap_access::end(l));
    };
    lap_access::warmup(l, true);
    for (std::size_t i = 0; i < o.warmup && !lap_access::last(l); ++i) {
        call();
        lap_access::drop(l);
    }
    lap_access::warmup(l, false);
    std::vector<double> samples;
    samples.reserve(std::min<std::size_t>(o.most, 4096));
    clock::duration total{};
    while (!lap_access::last(l) && samples.size() < o.most &&
           (samples.size() < o.least || total < o.time)) {
        const auto spent = call();
        total += spent;
        samples.push_back(std::chrono::duration<double>(spent).count());
        lap_access::keep(l);
    }
    auto d = distribute(std::move(samples), l, o.slowest,
                        std::chrono::duration<double>(o.limit).count());
    record(name, d);
    return d;
}

}  // namespace mcpplibs::mtest::bench
