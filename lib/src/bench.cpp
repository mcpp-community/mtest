module mcpplibs.mtest.bench;

import std;

namespace mcpplibs::mtest::bench {

namespace {
double median_of(std::vector<double> v) {
    if (v.empty())
        return 0;
    std::ranges::sort(v);
    const auto n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}
}  // namespace

summary summarize(std::vector<double> samples, std::size_t iterations) {
    summary s;
    s.iterations = iterations;
    if (!samples.empty()) {
        s.median = median_of(samples);
        s.min = *std::ranges::min_element(samples);
        s.max = *std::ranges::max_element(samples);
        std::vector<double> dev;
        for (double x : samples)
            dev.push_back(std::abs(x - s.median));
        s.mad = median_of(std::move(dev));
    }
    s.samples = std::move(samples);
    return s;
}

void record(std::string_view name, const summary& s) {
    metric(name, s.median, "s", s.samples, "lower");
}

}  // namespace mcpplibs::mtest::bench
