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

// A time for a reader: 9.100 ms, 412.0 us, 1.250 s.
std::string readable(double seconds) {
    if (seconds >= 1)
        return std::format("{:.3f} s", seconds);
    if (seconds >= 1e-3)
        return std::format("{:.3f} ms", seconds * 1e3);
    if (seconds >= 1e-6)
        return std::format("{:.1f} us", seconds * 1e6);
    return std::format("{:.0f} ns", seconds * 1e9);
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
    // Run alone, a program shows what it measured; under a runner the run's record has it.
    if (!under_runner())
        std::println("{}: {} (from {} to {}; {} samples of {} calls)", name, readable(s.median),
                     readable(s.min), readable(s.max), s.samples.size(), s.iterations);
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

namespace {
constexpr double unsaid = std::numeric_limits<double>::quiet_NaN();

std::size_t column(std::vector<std::string>& names, std::string_view name) {
    for (std::size_t i = 0; i < names.size(); ++i)
        if (names[i] == name)
            return i;
    names.emplace_back(name);
    return names.size() - 1;
}

double& cell(std::vector<double>& cells, std::size_t at) {
    if (cells.size() <= at)
        cells.resize(at + 1, unsaid);
    return cells[at];
}

double said(const std::vector<double>& cells, std::size_t at) {
    return at < cells.size() ? cells[at] : unsaid;
}

template <class Rows, class Pick>
quantity over(std::string name, const Rows& rows, std::size_t at, Pick pick) {
    quantity q;
    q.name = std::move(name);
    std::vector<double> sorted;
    for (const auto& r : rows)
        if (const double v = said(pick(r), at); !std::isnan(v))
            sorted.push_back(v);
    q.calls = sorted.size();
    if (sorted.empty())
        return q;
    std::ranges::sort(sorted);
    q.min = sorted.front();
    q.max = sorted.back();
    q.p50 = percentile(sorted, 0.50);
    q.p95 = percentile(sorted, 0.95);
    q.p99 = percentile(sorted, 0.99);
    double sum = 0;
    for (double x : sorted)
        sum += x;
    q.mean = sum / double(sorted.size());
    return q;
}

}  // namespace

namespace {
// A thread's own: an allocation costs it two additions and touches no line another thread writes,
// so a program whose other threads allocate is not slowed by being counted.
thread_local constinit std::uint64_t allocation_calls = 0;
thread_local constinit std::uint64_t allocation_bytes = 0;
}  // namespace

void allocated(std::size_t bytes) noexcept {
    ++allocation_calls;
    allocation_bytes += bytes;
}

allocation_count allocations() noexcept {
    return {allocation_calls, allocation_bytes};
}

void lap::part(std::string_view name, clock::duration spent) {
    double& at = cell(now_.parts, column(part_names_, name));
    const double seconds = std::chrono::duration<double>(spent).count();
    at = std::isnan(at) ? seconds : at + seconds;
}

void lap::value(std::string_view name, double v) {
    cell(now_.values, column(value_names_, name)) = v;
}

void lap_access::keep(lap& l) {
    // A program that counts its allocations: each call's are quantities of it.
    if (allocations().count > 0) {
        l.value("allocations", double(l.now_count_));
        l.value("allocated", double(l.now_bytes_));
    }
    if (l.part_names_.empty() && l.value_names_.empty() && l.now_.said.empty() && l.rows_.empty())
        return;  // no call has said anything: nothing is kept
    l.rows_.push_back(std::move(l.now_));
    l.now_ = {};
}

void lap_access::drop(lap& l) {
    l.now_ = {};
}

distribution distribute(std::vector<double> samples, const lap& l, std::size_t slowest,
                        double limit) {
    const std::vector<double> in_order = samples;
    distribution d = distribute(std::move(samples), lap_access::items(l));
    const lap_access::view said_by = lap_access::look(l);
    // Rows begin at the first call that said anything: the calls before it said nothing.
    const std::size_t before = in_order.size() - std::min(in_order.size(), said_by.rows.size());
    for (std::size_t i = 0; i < said_by.parts.size(); ++i)
        d.parts.push_back(
            over(std::string{said_by.parts[i]}, said_by.rows, i,
                 [](const auto& r) -> const std::vector<double>& { return r.parts; }));
    for (std::size_t i = 0; i < said_by.values.size(); ++i)
        d.values.push_back(
            over(std::string{said_by.values[i]}, said_by.rows, i,
                 [](const auto& r) -> const std::vector<double>& { return r.values; }));
    std::vector<std::size_t> order(in_order.size());
    std::ranges::iota(order, std::size_t{0});
    d.limit = limit;
    if (limit > 0) {
        d.over = static_cast<std::size_t>(
            std::ranges::count_if(in_order, [&](double x) { return x > limit; }));
        if (slowest == 0)
            slowest = std::min<std::size_t>(d.over, 8);
    }
    const std::size_t stated = std::min(slowest, order.size());
    std::ranges::partial_sort(order, order.begin() + static_cast<std::ptrdiff_t>(stated),
                              [&](std::size_t a, std::size_t b) {
                                  return in_order[a] != in_order[b] ? in_order[a] > in_order[b]
                                                                    : a < b;
                              });
    for (std::size_t k = 0; k < stated; ++k) {
        slow_call c;
        c.index = order[k];
        c.seconds = in_order[c.index];
        if (c.index >= before) {
            const auto& r = said_by.rows[c.index - before];
            for (std::size_t i = 0; i < said_by.parts.size(); ++i)
                if (const double v = said(r.parts, i); !std::isnan(v))
                    c.parts.emplace_back(std::string{said_by.parts[i]}, v);
            for (std::size_t i = 0; i < said_by.values.size(); ++i)
                if (const double v = said(r.values, i); !std::isnan(v))
                    c.values.emplace_back(std::string{said_by.values[i]}, v);
            c.said = r.said;
        }
        d.slowest.push_back(std::move(c));
    }
    return d;
}

laps::laps(std::string_view name, each_options o) : name_(name), o_(o) {
    samples_.reserve(std::min<std::size_t>(o_.most, 4096));
}

bool laps::next() {
    const auto now = clock::now();
    if (running_) {
        // The call that was running ends here: what `next` does after is no call's.
        const auto spent = std::max(clock::duration{}, now - began_ - lap_access::end(*this));
        if (warmed_ < o_.warmup) {
            ++warmed_;
            lap_access::drop(*this);
        } else {
            total_ += spent;
            samples_.push_back(std::chrono::duration<double>(spent).count());
            lap_access::keep(*this);
        }
        running_ = false;
    }
    const bool warming = warmed_ < o_.warmup;
    const bool more =
        !lap_access::last(*this) && (warming || (samples_.size() < o_.most &&
                                                 (samples_.size() < o_.least || total_ < o_.time)));
    if (!more) {
        if (!done_) {
            done_ = true;
            result_ = distribute(std::move(samples_), *this, o_.slowest,
                                 std::chrono::duration<double>(o_.limit).count());
            record(name_, result_);
        }
        return false;
    }
    lap_access::warmup(*this, warming);
    lap_access::begin(*this);
    running_ = true;
    began_ = clock::now();
    return true;
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
    for (const quantity& q : d.parts) {
        const std::string part = base + "." + q.name;
        metric(part, q.p50, "s");
        metric(part + ".p95", q.p95, "s");
        metric(part + ".p99", q.p99, "s");
        metric(part + ".max", q.max, "s");
        metric(part + ".mean", q.mean, "s");
    }
    for (const quantity& q : d.values) {
        metric(base + "." + q.name, q.mean, "");
        metric(base + "." + q.name + ".max", q.max, "");
    }
    if (d.limit > 0) {
        metric(base + ".over", double(d.over), "");
        metric(base + ".over.share",
               d.samples.empty() ? 0 : double(d.over) / double(d.samples.size()), "");
    }
    if (!under_runner()) {
        std::string line = std::format("{}: {} (p95 {}, p99 {}, max {}; mean {}; {} calls", name,
                                       readable(d.p50), readable(d.p95), readable(d.p99),
                                       readable(d.max), readable(d.mean), d.samples.size());
        if (d.limit > 0)
            line += std::format("; {} over {}", d.over, readable(d.limit));
        line += ")";
        for (const quantity& q : d.parts)
            line += std::format("\n    {}: {} (p99 {}, max {})", q.name, readable(q.p50),
                                readable(q.p99), readable(q.max));
        for (const quantity& q : d.values)
            line += std::format("\n    {}: {:g} (max {:g})", q.name, q.mean, q.max);
        std::println("{}", line);
    }
    for (const slow_call& c : d.slowest) {
        std::string line = std::format("{}: call {} took {}", name, c.index, readable(c.seconds));
        const char* between = ": ";
        for (const auto& [part, seconds] : c.parts)
            line += std::format("{}{} {}", std::exchange(between, ", "), part, readable(seconds));
        for (const auto& [value, v] : c.values)
            line += std::format("{}{} {:g}", std::exchange(between, ", "), value, v);
        if (!c.said.empty())
            line += " -- " + c.said;
        std::println("{}", line);
    }
}

}  // namespace mcpplibs::mtest::bench
