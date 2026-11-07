module mcpplibs.mtest.engine.analysis;

import std;

namespace mcpplibs::mtest::engine {

namespace {

double median(std::vector<double> v) {
    if (v.empty())
        return 0;
    std::ranges::sort(v);
    const auto n = v.size();
    return n % 2 ? v[n / 2] : (v[n / 2 - 1] + v[n / 2]) / 2;
}

std::string key(const case_result& c) {
    return c.program + "::" + c.id;
}

// Numbers, hexadecimal words and paths written as `<n>`, `<x>` and `<path>`: messages that differ
// only there are one failure.
std::string shape(std::string_view message) {
    std::string out;
    std::size_t i = 0;
    while (i < message.size()) {
        const char ch = message[i];
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            while (i < message.size() &&
                   (std::isalnum(static_cast<unsigned char>(message[i])) || message[i] == '.'))
                ++i;
            out += "<n>";
        } else if (ch == '/' || (ch == '\\' && i + 1 < message.size())) {
            while (i < message.size() && !std::isspace(static_cast<unsigned char>(message[i])) &&
                   message[i] != ':' && message[i] != '\'' && message[i] != '"')
                ++i;
            out += "<path>";
        } else {
            out += ch;
            ++i;
        }
    }
    return out.size() > 200 ? out.substr(0, 200) : out;
}

}  // namespace

json hot(const run_record& r, std::size_t top) {
    std::vector<const case_result*> v;
    for (const auto& c : r.cases)
        v.push_back(&c);
    std::ranges::sort(v, std::greater{}, &case_result::ms);
    json items = json::array();
    double total = 0;
    for (const auto& c : r.cases)
        total += c.ms;
    for (std::size_t i = 0; i < v.size() && i < top; ++i)
        items.push_back({{"program", v[i]->program},
                         {"case", v[i]->id},
                         {"ms", v[i]->ms},
                         {"status", v[i]->status},
                         {"share", total > 0 ? v[i]->ms / total : 0}});
    std::string conclusion =
        v.empty() ? "the run has no case"
                  : std::format("the slowest case, {}, took {:.0f} ms, {:.0f}% of the cases' time",
                                v[0]->id, v[0]->ms, total > 0 ? 100 * v[0]->ms / total : 0);
    return {{"run", r.id}, {"unit", "ms"}, {"items", items}, {"conclusion", conclusion}};
}

json durations(const run_record& r) {
    std::map<std::string, double> by_kind, by_program;
    for (const auto& c : r.cases)
        by_kind[c.kind] += c.ms;
    double longest = 0;
    std::string longest_program;
    double program_total = 0;
    for (const auto& p : r.programs) {
        by_program[p.name] += p.ms;
        program_total += p.ms;
        if (p.ms > longest) {
            longest = p.ms;
            longest_program = p.name;
        }
    }
    json programs = json::array();
    std::vector<std::pair<std::string, double>> sorted(by_program.begin(), by_program.end());
    std::ranges::sort(sorted, std::greater{}, &std::pair<std::string, double>::second);
    for (const auto& [name, ms] : sorted)
        programs.push_back({{"program", name}, {"ms", ms}, {"of_wall", r.ms > 0 ? ms / r.ms : 0}});
    const double efficiency = r.ms > 0 && r.jobs > 0 ? program_total / (r.ms * r.jobs) : 0;
    return {{"run", r.id},
            {"unit", "ms"},
            {"wall", r.ms},
            {"jobs", r.jobs},
            {"by_kind", by_kind},
            {"programs", programs},
            {"critical_path", {{"program", longest_program}, {"ms", longest}}},
            {"parallel_efficiency", efficiency},
            {"conclusion", longest_program.empty()
                               ? std::string("no program ran")
                               : std::format("the wall clock was {:.1f} s; {} alone took {:.1f} s, "
                                             "so no schedule of these programs ends sooner; "
                                             "the {} jobs were busy {:.0f}% of the time",
                                             r.ms / 1000, longest_program, longest / 1000, r.jobs,
                                             100 * efficiency)}};
}

json diff(const run_record& before, const run_record& after, double ratio) {
    std::map<std::string, const case_result*> a;
    for (const auto& c : before.cases)
        a[key(c)] = &c;
    json failing_now = json::array(), fixed = json::array(), slower = json::array(),
         faster = json::array(), added = json::array();
    for (const auto& c : after.cases) {
        auto it = a.find(key(c));
        if (it == a.end()) {
            added.push_back({{"program", c.program}, {"case", c.id}, {"status", c.status}});
            continue;
        }
        const auto& b = *it->second;
        if (failing(c.status) && !failing(b.status))
            failing_now.push_back({{"program", c.program}, {"case", c.id}, {"was", b.status}});
        if (!failing(c.status) && failing(b.status))
            fixed.push_back({{"program", c.program}, {"case", c.id}, {"was", b.status}});
        if (b.ms > 1 && c.ms / b.ms >= ratio)
            slower.push_back({{"program", c.program}, {"case", c.id}, {"ms", c.ms}, {"was", b.ms}});
        if (c.ms > 1 && b.ms / c.ms >= ratio)
            faster.push_back({{"program", c.program}, {"case", c.id}, {"ms", c.ms}, {"was", b.ms}});
        a.erase(it);
    }
    json removed = json::array();
    for (const auto& [k, c] : a)
        removed.push_back({{"program", c->program}, {"case", c->id}});
    return {{"before", before.id},
            {"after", after.id},
            {"newly_failing", failing_now},
            {"fixed", fixed},
            {"slower", slower},
            {"faster", faster},
            {"added", added},
            {"removed", removed},
            {"ratio", ratio},
            {"conclusion",
             std::format("{} newly failing, {} fixed, {} slower and {} faster by {}x",
                         failing_now.size(), fixed.size(), slower.size(), faster.size(), ratio)}};
}

json history(const store& s, std::string_view program, std::string_view id, std::size_t limit) {
    json items = json::array();
    auto all = runs(s);
    std::size_t fails = 0, seen = 0;
    for (std::size_t i = all.size(); i-- > 0 && items.size() < limit;) {
        auto r = load_run(s, all[i].id);
        if (!r)
            continue;
        for (const auto& c : r->cases) {
            if (c.id != id || (!program.empty() && c.program != program))
                continue;
            ++seen;
            if (failing(c.status))
                ++fails;
            items.push_back({{"run", r->id},
                             {"commit", r->commit},
                             {"status", c.status},
                             {"ms", c.ms},
                             {"reason", c.reason}});
        }
    }
    return {{"case", id},
            {"program", program},
            {"items", items},
            {"conclusion", seen == 0
                               ? std::string("no stored run has the case")
                               : std::format("failed in {} of its last {} runs", fails, seen)}};
}

json flaky(const store& s, std::size_t count) {
    auto all = runs(s);
    std::map<std::string, std::pair<int, int>> flips;  // key -> (changes, runs)
    std::map<std::string, std::string> last_status, last_commit;
    const std::size_t from = all.size() > count ? all.size() - count : 0;
    for (std::size_t i = from; i < all.size(); ++i) {
        auto r = load_run(s, all[i].id);
        if (!r)
            continue;
        for (const auto& c : r->cases) {
            const auto k = key(c);
            auto& f = flips[k];
            ++f.second;
            if (c.status == "flaky")
                ++f.first;
            else if (last_status.contains(k) && last_commit[k] == r->commit && !r->commit.empty() &&
                     failing(last_status[k]) != failing(c.status))
                ++f.first;
            last_status[k] = c.status;
            last_commit[k] = r->commit;
        }
    }
    json items = json::array();
    for (const auto& [k, f] : flips)
        if (f.first > 0)
            items.push_back({{"case", k},
                             {"changes", f.first},
                             {"runs", f.second},
                             {"rate", double(f.first) / f.second}});
    std::sort(items.begin(), items.end(), [](const json& a, const json& b) {
        return a["rate"].get<double>() > b["rate"].get<double>();
    });
    return {{"runs", count},
            {"items", items},
            {"conclusion",
             items.empty() ? std::string("no case changed its status without a change of commit")
                           : std::format("{} case(s) changed status at one commit; quarantine them "
                                         "or fix them",
                                         items.size())}};
}

json failures(const run_record& r) {
    std::map<std::string, std::vector<const case_result*>> groups;
    for (const auto& c : r.cases) {
        if (!failing(c.status))
            continue;
        const std::string msg = !c.failures.empty() ? c.failures.front().message : c.reason;
        groups[shape(msg)].push_back(&c);
    }
    std::vector<std::pair<std::string, std::vector<const case_result*>>> v(groups.begin(),
                                                                           groups.end());
    std::ranges::sort(v, [](const auto& a, const auto& b) {
        return a.second.size() > b.second.size();
    });
    json items = json::array();
    for (const auto& [shape_text, cases] : v) {
        json ids = json::array();
        for (const auto* c : cases)
            ids.push_back(c->program + "::" + c->id);
        items.push_back({{"message", shape_text}, {"count", cases.size()}, {"cases", ids}});
    }
    return {{"run", r.id},
            {"groups", items},
            {"conclusion",
             v.empty()
                 ? std::string("nothing failed")
                 : std::format("{} failing case(s) in {} group(s); the largest, {} case(s): {}",
                               r.counts.contains("fail") ? r.counts.at("fail") : 0, v.size(),
                               v.front().second.size(), v.front().first)}};
}

std::optional<run_record> bench_baseline(const store& s, const run_record& r,
                                         const bench_policy& p) {
    if (p.baseline != "last-pass")
        return load_run(s, p.baseline);
    // The runs before this one, in the index's order (the order they finished): two runs of one
    // second have identifiers in no time order.
    auto all = runs(s);
    std::size_t end = all.size();
    for (std::size_t i = 0; i < all.size(); ++i)
        if (all[i].id == r.id)
            end = i;
    for (std::size_t i = end; i-- > 0;) {
        auto b = load_run(s, all[i].id);
        if (!b)
            continue;
        const bool has_bench = std::ranges::any_of(b->cases, [](const case_result& c) {
            return !c.metrics.empty() && c.status == "pass";
        });
        if (has_bench)
            return b;
    }
    return std::nullopt;
}

std::vector<bench_verdict>
judge_bench(const run_record& r, const std::optional<run_record>& baseline, const bench_policy& p) {
    std::vector<bench_verdict> out;
    for (const auto& c : r.cases) {
        for (const auto& m : c.metrics) {
            bench_verdict v;
            v.program = c.program;
            v.id = c.id;
            v.metric = m.name;
            const double now = m.samples.empty() ? m.value : median(m.samples);
            if (!m.samples.empty() && now != 0) {
                std::vector<double> dev;
                for (double x : m.samples)
                    dev.push_back(std::abs(x - now) / std::abs(now));
                v.spread = median(dev);
            }
            const case_result* before = nullptr;
            if (baseline)
                for (const auto& bc : baseline->cases)
                    if (bc.program == c.program && bc.id == c.id)
                        before = &bc;
            const metric_info* bm = nullptr;
            if (before)
                for (const auto& x : before->metrics)
                    if (x.name == m.name)
                        bm = &x;
            if (c.status == "invalid") {
                v.verdict = "invalid";
                v.reason =
                    c.reason.empty() ? "the program found its measurement invalid" : c.reason;
            } else if (v.spread > p.spread) {
                v.verdict = "invalid";
                v.reason =
                    std::format("the samples spread {:.3f} from their median, beyond the bound {}",
                                v.spread, p.spread);
            } else if (!bm) {
                v.verdict = "invalid";
                v.reason = "no baseline run measured it";
            } else {
                const double then = bm->samples.empty() ? bm->value : median(bm->samples);
                v.ratio = then != 0 ? (m.better == "higher" ? then / now : now / then) : 0;
                v.verdict = v.ratio <= p.limit ? "pass" : "fail";
                v.reason =
                    std::format("{:.3f} times the baseline's ({:.6g} {} against {:.6g}), limit {}",
                                v.ratio, now, m.unit, then, p.limit);
            }
            v.repeatable = v.verdict == "invalid" ? p.repeat_invalid
                           : v.verdict == "fail"  ? p.repeat_valid
                                                  : false;
            out.push_back(std::move(v));
        }
    }
    return out;
}

json gate(const config& c, const store& s, const run_record& r, std::string_view set) {
    json reasons = json::array();
    for (const auto& cr : r.cases)
        if (failing(cr.status) && !cr.quarantined)
            reasons.push_back(std::format("{}::{} {}{}", cr.program, cr.id, cr.status,
                                          cr.reason.empty() ? "" : ": " + cr.reason));
    if (auto it = c.sets.find(std::string(set)); it != c.sets.end() && it->second.budget) {
        double total = 0;
        for (const auto& cr : r.cases)
            total += cr.ms;
        if (total > double(it->second.budget->count()))
            reasons.push_back(std::format("set {} took {:.1f} s, over its budget of {} -- move its "
                                          "slowest cases out (mtest hot)",
                                          set, total / 1000, *it->second.budget));
    }
    json bench = json::array();
    const bool judge = set == "release" || (c.sets.contains(std::string(set)) &&
                                            c.sets.at(std::string(set)).gate_bench);
    if (judge) {
        auto verdicts = judge_bench(r, bench_baseline(s, r, c.bench), c.bench);
        for (const auto& v : verdicts) {
            bench.push_back({{"program", v.program},
                             {"case", v.id},
                             {"metric", v.metric},
                             {"verdict", v.verdict},
                             {"ratio", v.ratio},
                             {"spread", v.spread},
                             {"reason", v.reason},
                             {"repeatable", v.repeatable}});
            if (v.verdict != "pass")
                reasons.push_back(std::format("bench {}::{} {}: {}{}", v.program, v.id, v.verdict,
                                              v.reason,
                                              v.repeatable ? " (may be measured again)"
                                                           : " (may not be measured again)"));
        }
    }
    const bool ok = reasons.empty() && r.status != "cancelled";
    return {{"run", r.id},
            {"set", set},
            {"pass", ok},
            {"reasons", reasons},
            {"bench", bench},
            {"conclusion", ok ? std::format("the {} gate passes for run {}", set, r.id)
                              : std::format("the {} gate fails: {}", set,
                                            reasons.empty() ? std::string("the run was cancelled")
                                                            : reasons.front().get<std::string>())}};
}

}  // namespace mcpplibs::mtest::engine
