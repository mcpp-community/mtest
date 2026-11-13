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

double percentile(std::span<const double> sorted, double q) {
    if (sorted.empty())
        return 0;
    const double at = std::clamp(q, 0.0, 1.0) * double(sorted.size() - 1);
    const auto lo = static_cast<std::size_t>(at);
    const auto hi = std::min(lo + 1, sorted.size() - 1);
    return sorted[lo] + (sorted[hi] - sorted[lo]) * (at - double(lo));
}

distribution distribute(std::vector<double> samples, std::uint64_t items) {
    distribution d;
    d.items = items;
    if (!samples.empty()) {
        std::vector<double> sorted = samples;
        std::ranges::sort(sorted);
        d.min = sorted.front();
        d.max = sorted.back();
        d.p50 = percentile(sorted, 0.50);
        d.p95 = percentile(sorted, 0.95);
        d.p99 = percentile(sorted, 0.99);
        double sum = 0;
        for (double x : sorted)
            sum += x;
        d.mean = sum / double(sorted.size());
        if (items)
            d.per_item = d.mean / double(items);
    }
    d.samples = std::move(samples);
    return d;
}

// The calls differ by their nature, so the samples are not stated with the metrics: the verdict on
// samples that spread (P6.2) is for a measurement meant to be one number.
void record(std::string_view name, const distribution& d) {
    const std::string base{name};
    metric(base, d.p50, "s");
    metric(base + ".p95", d.p95, "s");
    metric(base + ".p99", d.p99, "s");
    metric(base + ".max", d.max, "s");
    metric(base + ".mean", d.mean, "s");
    if (d.items)
        metric(base + ".per_item", d.per_item, "s");
}

}  // namespace mcpplibs::mtest::bench
