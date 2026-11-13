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
struct each_options {
    std::size_t least = 12;                                          // calls measured, at least
    std::size_t most = 100'000;                                      // and at most
    std::chrono::nanoseconds time = std::chrono::milliseconds{400};  // measured for this long
    std::size_t warmup = 3;  // calls before the first measured one
};

// One call: what the body may say of it.
class lap {
public:
    using clock = std::chrono::steady_clock;
    // The time from `pause` to `resume` is not this call's.
    void pause() noexcept {
        if (!paused_)
            paused_ = true, paused_at_ = clock::now();
    }
    void resume() noexcept {
        if (paused_)
            paused_ = false, out_ += clock::now() - paused_at_;
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
private:
    friend struct lap_access;
    bool paused_ = false, warmup_ = false;
    clock::time_point paused_at_{};
    clock::duration out_{};
    std::uint64_t items_ = 0;
    std::size_t index_ = 0;
};

// What `each` does with a lap (not for a test).
struct lap_access {
    static void warmup(lap& l, bool on) noexcept {
        l.warmup_ = on;
    }
    static void begin(lap& l) noexcept {
        l.paused_ = false, l.out_ = {};
    }
    static lap::clock::duration end(lap& l) noexcept {
        l.resume();
        ++l.index_;
        return l.out_;
    }
    static std::uint64_t items(const lap& l) noexcept {
        return l.items_;
    }
};

struct distribution {
    std::vector<double> samples;  // seconds a call, in the order measured
    double min = 0, mean = 0, p50 = 0, p95 = 0, p99 = 0, max = 0;
    std::uint64_t items = 0;  // a call's, as the last call that said so stated it
    double per_item = 0;      // mean / items; 0 when no call stated any
};

// The value `q` of the way (0 to 1) through `sorted`, between the two nearest by position.
double percentile(std::span<const double> sorted, double q);
distribution distribute(std::vector<double> samples, std::uint64_t items = 0);
// The metrics of a distribution (see `each`), in seconds; `per_item` when a call stated items.
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
    for (std::size_t i = 0; i < o.warmup; ++i)
        call();
    lap_access::warmup(l, false);
    std::vector<double> samples;
    samples.reserve(std::min<std::size_t>(o.most, 4096));
    clock::duration total{};
    while (samples.size() < o.most && (samples.size() < o.least || total < o.time)) {
        const auto spent = call();
        total += spent;
        samples.push_back(std::chrono::duration<double>(spent).count());
    }
    auto d = distribute(std::move(samples), lap_access::items(l));
    record(name, d);
    return d;
}

}  // namespace mcpplibs::mtest::bench
