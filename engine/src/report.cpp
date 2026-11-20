module mcpplibs.mtest.engine.report;

import std;
import mcpplibs.mtest.engine.select;
import mcpplibs.mtest.engine.analysis;
import mcpplibs.mtest.engine.dag;
import mcpplibs.mtest.engine.output;

namespace mcpplibs::mtest::engine {

// `analyze trend`: each metric of each case over the last `count` stored runs, oldest first, beside
// the bounds the case states (P6.5): its last value, its range and median, and how far the last is
// from its bound.
json trend(const store& s, std::size_t count, const std::vector<std::string>& cases) {
    struct series {
        std::string program, id, metric, unit;
        std::vector<double> values;
        std::vector<bound_info> bounds;
    };
    std::map<std::string, series> all;
    const auto stored = runs(s);
    const std::size_t from = stored.size() > count ? stored.size() - count : 0;
    auto take = [&](const case_result& c) {
        const bool wanted =
            cases.empty() || std::ranges::any_of(cases, [&](const std::string& g) {
                return glob_match(g, c.id) || glob_match(g, c.program + "::" + c.id);
            });
        if (!wanted)
            return;
        for (const auto& m : c.metrics) {
            auto& one = all[c.program + "::" + c.id + "::" + m.name];
            one.program = c.program;
            one.id = c.id;
            one.metric = m.name;
            one.unit = m.unit;
            one.values.push_back(m.value);
            if (!c.bounds.empty())
                one.bounds = c.bounds;
        }
    };
    for (std::size_t i = from; i < stored.size(); ++i)
        if (auto r = load_run(s, stored[i].id))
            std::ranges::for_each(r->cases, take);
    json items = json::array();
    std::size_t over = 0;
    for (auto& [key, one] : all) {
        auto sorted = one.values;
        std::ranges::sort(sorted);
        const double last = one.values.back();
        json item = {{"program", one.program},
                     {"case", one.id},
                     {"metric", one.metric},
                     {"unit", one.unit},
                     {"runs", one.values.size()},
                     {"last", last},
                     {"min", sorted.front()},
                     {"max", sorted.back()},
                     {"median", sorted[sorted.size() / 2]},
                     {"values", one.values}};
        const auto b = std::ranges::find(one.bounds, one.metric, &bound_info::metric);
        if (b != one.bounds.end()) {
            item["bound"] = to_json(std::vector<bound_info>{*b})[0];
            over += (b->max && last > *b->max) || (b->min && last < *b->min);
            // The share of the bound the last value leaves: 0.2 is 20% inside it.
            if (b->max && *b->max != 0)
                item["margin"] = (*b->max - last) / *b->max;
            else if (b->min && *b->min != 0)
                item["margin"] = (last - *b->min) / *b->min;
        }
        items.push_back(std::move(item));
    }
    return {{"items", items},
            {"runs", stored.size() - from},
            {"conclusion",
             std::format("{} metric(s) over the last {} run(s); {} past its bound now",
                         items.size(), stored.size() - from, over)}};
}

namespace {

std::string sec(double ms) {
    return ms >= 1000 ? std::format("{:.1f} s", ms / 1000) : std::format("{:.0f} ms", ms);
}

std::string bytes(double b) {
    if (b >= 1024.0 * 1024 * 1024)
        return std::format("{:.2f} GiB", b / (1024.0 * 1024 * 1024));
    if (b >= 1024.0 * 1024)
        return std::format("{:.1f} MiB", b / (1024.0 * 1024));
    if (b >= 1024)
        return std::format("{:.1f} KiB", b / 1024);
    return std::format("{:.0f} B", b);
}

// Text a table cell holds: one line, no pipe.
std::string cell(std::string_view s, std::size_t keep = 160) {
    std::string out;
    for (char c : s.substr(0, keep))
        out += c == '|' ? '/' : (c == '\n' || c == '\r') ? ' ' : c;
    if (s.size() > keep)
        out += "…";
    return out;
}

std::string value_of(const metric_info& m) {
    if (m.unit == "B")
        return bytes(m.value);
    if (m.unit == "ms")
        return sec(m.value);
    return std::format("{:g} {}", m.value, m.unit);
}

bool in_scope(const report_options& o, const case_result& c) {
    if (o.scope.starts_with("program:"))
        return glob_match(o.scope.substr(8), c.program);
    if (o.scope.starts_with("kind:"))
        return c.kind == o.scope.substr(5);
    return true;
}

bool program_in_scope(const report_options& o, const program_result& p) {
    if (o.scope.starts_with("program:"))
        return glob_match(o.scope.substr(8), p.name);
    return true;
}

bool wants(const report_options& o, std::string_view section) {
    return std::ranges::find(o.sections, section) != o.sections.end();
}

std::vector<const case_result*> cases_of(const std::vector<run_record>& runs,
                                         const report_options& o) {
    std::vector<const case_result*> out;
    for (const auto& r : runs)
        for (const auto& c : r.cases)
            if (in_scope(o, c))
                out.push_back(&c);
    return out;
}

void summary(std::string& md, const std::vector<run_record>& runs, const report_options& o) {
    md += "## Summary\n\n| run | status | system | machine | commit | cases | pass | fail | skip | "
          "wall | jobs |\n"
          "|---|---|---|---|---|---:|---:|---:|---:|---:|---:|\n";
    std::map<std::string, int> total;
    double wall = 0;
    for (const auto& r : runs) {
        std::map<std::string, int> n;
        for (const auto& c : r.cases)
            if (in_scope(o, c))
                ++n[c.status];
        int cases = 0;
        for (const auto& [k, v] : n) {
            cases += v;
            total[k] += v;
        }
        wall = std::max(wall, r.ms);
        md += std::format("| `{}` | {} | {}-{} | {} | `{}` | {} | {} | {} | {} | {} | {} |\n", r.id,
                          r.status == "pass" ? "pass" : "**" + r.status + "**", r.system, r.arch,
                          r.machine.empty() ? "-" : r.machine,
                          r.commit.substr(0, 10), cases, n["pass"] + n["xfail"] + n["flaky"],
                          n["fail"] + n["xpass"] + n["timeout"], n["skip"], sec(r.ms), r.jobs);
    }
    if (runs.size() > 1) {
        int cases = 0;
        for (const auto& [k, v] : total)
            cases += v;
        md += std::format("| **all** | | | | | {} | {} | {} | {} | {} | |\n", cases,
                          total["pass"] + total["xfail"] + total["flaky"],
                          total["fail"] + total["xpass"] + total["timeout"], total["skip"],
                          sec(wall));
    }
    md += "\n";
}

// The slowest steps of the cases (P5.6): where an end-to-end case spends its time.
void steps_section(std::string& md, const std::vector<const case_result*>& cases, int top) {
    struct row {
        const case_result* c;
        const step_info* st;
    };
    std::vector<row> rows;
    for (auto* c : cases)
        for (const auto& st : c->steps)
            rows.push_back({c, &st});
    if (rows.empty())
        return;
    std::ranges::sort(rows, std::greater{}, [](const row& r) { return r.st->ms; });
    md += "## Slowest steps\n\n| case | step | time | processor | status | build |\n"
          "|---|---|---:|---:|---:|---|\n";
    for (std::size_t i = 0; i < rows.size() && int(i) < top; ++i) {
        const auto& st = *rows[i].st;
        md += std::format("| `{}` | `{}` | {} | {} | {} | {} |\n",
                          case_label(rows[i].c->program, rows[i].c->id), cell(st.name, 80), sec(st.ms),
                          st.cpu_ms ? sec(*st.cpu_ms) : "-", st.status ? std::to_string(*st.status) : "-",
                          st.build.empty() ? "-" : "`" + st.build + "`");
    }
    md += "\n";
}

// The builds the runner made (B4 `builds.ndjson`): the slowest edges, and the packages the cache
// served.
void builds_section(std::string& md, const std::vector<run_record>& runs, int top) {
    std::vector<const build_edge*> ran;
    std::size_t cached_packages = 0, cached_units = 0;
    double total = 0, cpu = 0;
    for (const auto& r : runs)
        for (const auto& b : r.builds) {
            if (b.cached) {
                ++cached_packages;
                cached_units += std::size_t(b.units);
                continue;
            }
            ran.push_back(&b);
            total += b.ms;
            cpu += b.cpu_ms.value_or(0);
        }
    if (ran.empty() && cached_packages == 0)
        return;
    std::ranges::sort(ran, std::greater{}, [](const build_edge* b) { return b->ms; });
    md += std::format("## Builds\n\n{} edge(s) ran, {} of edge time, {} of processor time; the cache "
                      "served {} package(s), {} unit(s).\n\n"
                      "| edge | package | program | time | processor | largest resident set |\n"
                      "|---|---|---|---:|---:|---:|\n",
                      ran.size(), sec(total), sec(cpu), cached_packages, cached_units);
    for (std::size_t i = 0; i < ran.size() && int(i) < top; ++i)
        md += std::format("| `{}` | {} | {} | {} | {} | {} |\n", cell(ran[i]->output, 80),
                          ran[i]->package.empty() ? "-" : ran[i]->package,
                          ran[i]->program.empty() ? "-" : ran[i]->program, sec(ran[i]->ms),
                          ran[i]->cpu_ms ? sec(*ran[i]->cpu_ms) : "-",
                          ran[i]->peak_bytes ? bytes(*ran[i]->peak_bytes) : "-");
    md += "\n";
}

// The cases that took more than four fifths of their time limit (SPEC-017 O5): those a slower
// machine fails first, with the machine's class.
void margin_section(std::string& md, const std::vector<run_record>& runs, const report_options& o) {
    std::string rows;
    for (const auto& r : runs)
        for (const auto& c : r.cases)
            if (in_scope(o, c) && c.limit_ms > 0 && c.ms > 0.8 * c.limit_ms)
                rows += std::format("| `{}` | {} | {} | {:.0f}% | {} |\n", case_label(c.program, c.id),
                                    sec(c.ms), sec(c.limit_ms), 100 * c.ms / c.limit_ms,
                                    r.machine.empty() ? "-" : r.machine);
    if (rows.empty())
        return;
    md += "## Margin\n\nCases that took more than four fifths of their time limit.\n\n"
          "| case | time | limit | share | machine |\n|---|---:|---:|---:|---|\n" +
          rows + "\n";
}

// The cases over the processor budget, and each ledger's state (0.11.0).
void budget_section(std::string& md, const run_record& r, const report_options& o) {
    if (!o.cfg)
        return;
    const auto over = budget_overruns(*o.cfg, r);
    if (over.empty())
        return;
    md += "## Budget\n\n| case | processor time | limit | waived |\n|---|---:|---:|---|\n";
    for (const auto& x : over)
        if (!x.value("stale_waiver", false))
            md += std::format("| `{}` | {} | {} | {} |\n", x.value("case", ""),
                              sec(x.value("cpu_ms", 0.0)), sec(x.value("limit_ms", 0.0)),
                              x.value("waived", false) ? "yes" : "**no**");
        else
            md += std::format("| `{}` | - | - | a waiver that excuses nothing |\n", x.value("case", ""));
    md += "\n";
}

void ledgers_section(std::string& md, const store& s, const run_record& r, const report_options& o) {
    if (!o.cfg || o.cfg->ledgers.empty())
        return;
    md += "## Ledgers\n\n| ledger | evidence | requirements | met | waived | missing |\n"
          "|---|---|---:|---:|---:|---:|\n";
    std::string missing;
    for (const auto& l : ledger_table(*o.cfg, s, r)) {
        if (l.contains("error")) {
            md += std::format("| {} | - | - | - | - | {} |\n", l.value("ledger", ""), l.value("error", ""));
            continue;
        }
        md += std::format("| {} | {} | {} | {} | {} | {} |\n", l.value("ledger", ""),
                          l.value("evidence", ""), l.value("requirements", 0), l.value("met", 0),
                          l["waived"].size(), l["missing"].size());
        for (const auto& m : l["missing"])
            missing += std::format("- `{}`: {}\n", l.value("ledger", ""), m.get<std::string>());
    }
    md += "\n" + (missing.empty() ? "" : "Missing:\n\n" + missing + "\n");
}

void kinds(std::string& md, const std::vector<const case_result*>& cases) {
    std::map<std::string, std::tuple<int, int, double>> by;
    for (auto* c : cases) {
        auto& [n, failed, ms] = by[c->kind.empty() ? "unit" : c->kind];
        ++n;
        failed += failing(c->status) && !c->quarantined;
        ms += c->ms;
    }
    md += "## Kinds\n\n| kind | cases | failed | time |\n|---|---:|---:|---:|\n";
    for (const auto& [k, v] : by)
        md += std::format("| {} | {} | {} | {} |\n", k, std::get<0>(v), std::get<1>(v),
                          sec(std::get<2>(v)));
    md += "\n";
}

void failures(std::string& md, const std::vector<const case_result*>& cases) {
    std::vector<const case_result*> bad;
    for (auto* c : cases)
        if (failing(c->status) || c->status == "infra")
            bad.push_back(c);
    md += std::format("## Failures\n\n");
    if (bad.empty()) {
        md += "No case failed.\n\n";
        return;
    }
    md += "| case | status | where | why |\n|---|---|---|---|\n";
    for (auto* c : bad) {
        const auto where = c->failures.empty()
                               ? std::string{}
                               : std::format("`{}:{}`", c->failures[0].file, c->failures[0].line);
        std::string why = c->failures.empty() ? c->reason : c->failures[0].message;
        if (!c->failures.empty() && !c->failures[0].expected.empty())
            why += std::format(" (expected {}, actual {})", c->failures[0].expected,
                               c->failures[0].actual);
        md += std::format("| `{}` | {}{} | {} | {} |\n", case_label(c->program, c->id), c->status,
                          c->quarantined ? " (quarantined)" : "", where, cell(why));
    }
    md += "\nRepeat one with `mtest explain --case '<case>'`.\n\n";
}

// Below the failures, the processes each timeout recorded (0.10.0), once per program.
void timeout_records(std::string& md, const std::vector<run_record>& runs,
                     const std::vector<const case_result*>& cases) {
    const std::set<const case_result*> in(cases.begin(), cases.end());
    std::set<const program_result*> shown;
    std::string blocks;
    for (const auto& r : runs)
        for (const auto& c : r.cases) {
            const auto* p = c.status == "timeout" && in.contains(&c)
                                ? timed_out_program(r, c.program, c.id)
                                : nullptr;
            if (!p || p->timeout->seen.empty() || !shown.insert(p).second)
                continue;
            blocks += std::format("<details><summary><code>{}</code>: "
                                  "{}</summary>\n\n```text\n{}```\n\n</details>\n\n",
                                  case_label(c.program, c.id),
                                  p->timeout->ended == "requested" ? "it ended when asked"
                                                                   : "it was ended after its grace",
                                  timeout_text(*p->timeout, "", 200));
        }
    if (!blocks.empty())
        md += "The processes of each program a timeout ended, as they were then:\n\n" + blocks;
}

void slowest(std::string& md, std::vector<const case_result*> cases, int top) {
    std::ranges::sort(cases, std::greater{}, [](const case_result* c) { return c->ms; });
    md += "## Slowest cases\n\n| case | kind | time | status |\n|---|---|---:|---|\n";
    for (std::size_t i = 0; i < cases.size() && int(i) < top; ++i)
        md += std::format("| `{}` | {} | {} | {} |\n", case_label(cases[i]->program, cases[i]->id),
                          cases[i]->kind, sec(cases[i]->ms), cases[i]->status);
    md += "\n";
}

void metrics(std::string& md, const std::vector<const case_result*>& cases, int top) {
    struct row {
        const case_result* c;
        const metric_info* m;
        const bound_info* b;
    };
    std::vector<row> bounded, measured;
    for (auto* c : cases)
        for (const auto& m : c->metrics) {
            auto b = std::ranges::find(c->bounds, m.name, &bound_info::metric);
            if (b != c->bounds.end())
                bounded.push_back({c, &m, &*b});
            else if (!m.name.starts_with("cpu.") && !m.name.starts_with("mem.") &&
                     !m.name.starts_with("io.") && !m.name.starts_with("net.") &&
                     !m.name.starts_with("alloc."))
                measured.push_back({c, &m, nullptr});
        }
    md += "## Metrics\n\n";
    if (bounded.empty() && measured.empty()) {
        md += "No case reported a metric of its own.\n\n";
        return;
    }
    if (!bounded.empty()) {
        md += "| case | metric | value | bound | verdict |\n|---|---|---:|---|---|\n";
        for (const auto& r : bounded) {
            std::string bound;
            if (r.b->max)
                bound += std::format("≤ {:g}", *r.b->max);
            if (r.b->min)
                bound += std::format("{}≥ {:g}", bound.empty() ? "" : ", ", *r.b->min);
            const bool over =
                (r.b->max && r.m->value > *r.b->max) || (r.b->min && r.m->value < *r.b->min);
            md += std::format("| `{}` | {} | {} | {} {} | {} |\n",
                              case_label(r.c->program, r.c->id), r.m->name, value_of(*r.m), bound,
                              r.m->unit, over ? "**past its bound**" : "within");
        }
        md += "\n";
    }
    if (!measured.empty()) {
        md += "| case | metric | value |\n|---|---|---:|\n";
        for (std::size_t i = 0; i < measured.size() && int(i) < top * 3; ++i)
            md += std::format("| `{}` | {} | {} |\n",
                              case_label(measured[i].c->program, measured[i].c->id),
                              measured[i].m->name, value_of(*measured[i].m));
        md += "\n";
    }
}

void resources(std::string& md, const std::vector<run_record>& runs,
               const std::vector<const case_result*>& cases, const report_options& o) {
    md += "## Use of the machine\n\n";
    bool any = false;
    std::string table = "| program | worker | time | user | sys | largest resident set | read | "
                        "written | measured by |\n"
                        "|---|---:|---:|---:|---:|---:|---:|---:|---|\n";
    for (const auto& r : runs)
        for (const auto& p : r.programs) {
            if (!p.usage || !program_in_scope(o, p))
                continue;
            any = true;
            const auto& u = *p.usage;
            table += std::format("| `{}` | {} | {} | {} | {} | {} | {} | {} | {} |\n", p.name,
                                 p.worker, sec(p.ms), sec(u.user_ms), sec(u.sys_ms),
                                 u.peak_bytes ? bytes(*u.peak_bytes) : "-",
                                 u.read_bytes ? bytes(*u.read_bytes) : "-",
                                 u.written_bytes ? bytes(*u.written_bytes) : "-",
                                 u.source == "engine" ? "the engine" : "the program");
        }
    // The cases that used the most, by their own measurements (P6.6).
    std::vector<std::pair<const case_result*, const metric_info*>> heavy;
    for (auto* c : cases)
        for (const auto& m : c->metrics)
            if (m.name == "mem.peak" || m.name == "cpu.user")
                heavy.emplace_back(c, &m);
    std::ranges::sort(heavy, std::greater{}, [](const auto& x) {
        return x.second->unit == "B" ? x.second->value / (1024 * 1024) : x.second->value;
    });
    if (!any && heavy.empty()) {
        md += "Nothing was measured (`[monitor] level` or `--monitor`).\n\n";
        return;
    }
    if (any)
        md += table + "\n";
    if (!heavy.empty()) {
        md += "| case | metric | value | precision |\n|---|---|---:|---|\n";
        for (std::size_t i = 0; i < heavy.size() && int(i) < o.top; ++i)
            md +=
                std::format("| `{}` | {} | {} | {} |\n",
                            case_label(heavy[i].first->program, heavy[i].first->id),
                            heavy[i].second->name, value_of(*heavy[i].second),
                            heavy[i].second->precision.empty() ? "-" : heavy[i].second->precision);
        md += "\n";
    }
}

void flaky_section(std::string& md, const store& s, int top) {
    const auto f = flaky(s, 30);
    md += "## Flaky cases\n\n";
    if (!f.contains("items") || f["items"].empty()) {
        md += "No case changed its verdict without a change of commit in the last 30 runs.\n\n";
        return;
    }
    md += "| case | changes | runs | rate |\n|---|---:|---:|---:|\n";
    int n = 0;
    for (const auto& item : f["items"]) {
        if (n++ >= top)
            break;
        md += std::format("| `{}` | {} | {} | {:.0f}% |\n", item.value("case", ""),
                          item.value("changes", 0), item.value("runs", 0),
                          100 * item.value("rate", 0.0));
    }
    md += "\n";
}

void trend_section(std::string& md, const store& s, int top) {
    const auto t = trend(s, 20, {});
    md += "## Trends\n\n";
    std::vector<json> bounded;
    for (const auto& item : t["items"])
        if (item.contains("bound"))
            bounded.push_back(item);
    if (bounded.empty()) {
        md += "No bounded metric has a history yet.\n\n";
        return;
    }
    std::ranges::sort(bounded, {}, [](const json& j) { return j.value("margin", 1.0); });
    md += "| case | metric | last | median | range | margin to its bound | runs "
          "|\n|---|---|---:|---:|---|---:|---:|\n";
    for (std::size_t i = 0; i < bounded.size() && int(i) < top; ++i) {
        const auto& j = bounded[i];
        md += std::format("| `{}` | {} | {:g} | {:g} | {:g} – {:g} | {:.0f}% | {} |\n",
                          case_label(j.value("program", ""), j.value("case", "")),
                          j.value("metric", ""), j.value("last", 0.0), j.value("median", 0.0),
                          j.value("min", 0.0), j.value("max", 0.0), 100 * j.value("margin", 0.0),
                          j.value("runs", 0));
    }
    md += "\n";
}

void path_section(std::string& md, const run_record& r, bool titled) {
    const auto cp = critical_path(r);
    md += titled ? std::format("## Critical path of `{}`\n\n", r.id) : "## Critical path\n\n";
    md += cp.value("conclusion", "") + ".\n\n";
    md += "| node | kind | start | time | waited for |\n|---|---|---:|---:|---|\n";
    for (const auto& step : cp["path"])
        md +=
            std::format("| {} | {} | {} | {} | {} |\n", cell(step.value("label", "")),
                        step.value("kind", ""), sec(step.value("start_ms", 0.0)),
                        sec(step.value("ms", 0.0)),
                        step.value("waited_for", "").empty() ? "-" : step.value("waited_for", ""));
    md += "\n";
}

}  // namespace

std::string markdown_report(const store& s, const std::vector<run_record>& runs,
                            const report_options& in) {
    report_options o = in;
    if (o.sections.empty())
        o.sections.assign(std::begin(kSections), std::end(kSections));
    std::string md = std::format("# {}\n\n", o.title.empty() ? "mtest report" : o.title);
    if (runs.empty())
        return md + "No run.\n";
    if (o.scope != "run")
        md += std::format("Scope: `{}`.\n\n", o.scope);
    const auto cases = cases_of(runs, o);
    if (wants(o, "summary"))
        summary(md, runs, o);
    if (wants(o, "kinds"))
        kinds(md, cases);
    if (wants(o, "failures")) {
        failures(md, cases);
        timeout_records(md, runs, cases);
    }
    if (wants(o, "slowest"))
        slowest(md, cases, o.top);
    if (wants(o, "steps"))
        steps_section(md, cases, o.top);
    if (wants(o, "builds"))
        builds_section(md, runs, o.top);
    if (wants(o, "margin"))
        margin_section(md, runs, o);
    if (wants(o, "budget"))
        for (const auto& r : runs)
            budget_section(md, r, o);
    if (wants(o, "ledgers") && !runs.empty())
        ledgers_section(md, s, runs.front(), o);
    if (wants(o, "metrics"))
        metrics(md, cases, o.top);
    if (wants(o, "resources"))
        resources(md, runs, cases, o);
    if (wants(o, "flaky"))
        flaky_section(md, s, o.top);
    if (wants(o, "trend"))
        trend_section(md, s, o.top);
    for (const auto& r : runs) {
        const bool titled = runs.size() > 1;
        if (wants(o, "path"))
            path_section(md, r, titled);
        if (wants(o, "graph") || wants(o, "gantt")) {
            const auto g = dag_of(r);
            if (wants(o, "graph"))
                md += std::format("## Graph{}\n\nThe run's work and what each waited for; the "
                                  "critical path in red.\n\n"
                                  "```mermaid\n{}```\n\n",
                                  titled ? std::format(" of `{}`", r.id) : "",
                                  mermaid_graph(g, o.max_nodes));
            if (wants(o, "gantt"))
                md += std::format("## Schedule{}\n\n```mermaid\n{}```\n\n",
                                  titled ? std::format(" of `{}`", r.id) : "",
                                  mermaid_gantt(g, o.max_nodes));
        }
    }
    return md;
}

}  // namespace mcpplibs::mtest::engine
