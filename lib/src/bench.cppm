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

}  // namespace mcpplibs::mtest::bench
