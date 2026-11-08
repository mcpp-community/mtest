module mcpplibs.mtest.engine.schedule;

import std;
import mcpplibs.os;
import mcpplibs.mtest.engine.exec;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

struct item {
    program_spec program;
    std::vector<std::string> select;  // empty: the whole program
    std::set<std::string> resources;
    double expected_ms = 0;
    std::map<std::string, int> retries;  // case id -> how many times it may run again
    std::map<std::string, const case_info*> diagnosed;  // cases with a diagnosis after failing
};

// P7.1: `machine` holds the machine alone; `serial:<g>` one at a time per group; `gpu`, `display`
// and any other name one holder at a time; `network` many.
class locks {
public:
    bool can_take(const std::set<std::string>& want) const {
        if (machine_)
            return false;
        if (want.contains("machine"))
            return running_ == 0;
        for (const auto& r : want)
            if (r != "network" && held_.contains(r))
                return false;
        return true;
    }
    void take(const std::set<std::string>& want) {
        ++running_;
        if (want.contains("machine"))
            machine_ = true;
        for (const auto& r : want)
            if (r != "network" && r != "machine")
                held_.insert(r);
    }
    void give(const std::set<std::string>& want) {
        --running_;
        if (want.contains("machine"))
            machine_ = false;
        for (const auto& r : want)
            held_.erase(r);
    }
private:
    std::set<std::string> held_;
    bool machine_ = false;
    int running_ = 0;
};

std::string git_commit(const std::filesystem::path& root) {
    os::process::spawn_options o;
    o.program = "git";
    o.args = {"rev-parse", "HEAD"};
    o.cwd = root;
    o.in = os::process::stdio::null;
    os::process::bounds b;
    b.hard = std::chrono::seconds{10};
    auto r = os::process::run(std::move(o), b);
    if (!r || !r->status.success())
        return {};
    auto out = r->out;
    while (!out.empty() && (out.back() == '\n' || out.back() == '\r'))
        out.pop_back();
    return out;
}

std::chrono::seconds case_limit(double seconds) {
    return std::chrono::seconds{static_cast<std::int64_t>(std::ceil(seconds))};
}

// mtest.toml's expected failure, for a case whose program states none: a failure is expected, a
// pass is not.
void expected_to_fail(case_result& cr, const std::string& reason) {
    if (reason.empty())
        return;
    if (failing(cr.status) && cr.status != "xpass") {
        cr.status = "xfail";
        cr.reason = reason;
    } else if (cr.status == "pass") {
        cr.status = "xpass";
        cr.reason = "expected to fail: " + reason;
    }
}

// P6.5 for a case that passed: a bound its metrics do not keep -- one mtest.toml states, which the
// program never saw -- fails it. A bound the program states, it judged itself.
void judge_bounds(case_result& cr, const std::vector<bound_info>& stated) {
    if (cr.status != "pass" && cr.status != "flaky")
        return;
    for (const auto& b : stated) {
        const auto m = std::ranges::find(cr.metrics, b.metric, &metric_info::name);
        std::string why;
        if (m == cr.metrics.end())
            why = std::format("the case reported no metric `{}`, which it bounds", b.metric);
        else if (b.max && m->value > *b.max)
            why = std::format("metric {} = {:g} {}, over its bound {:g} (at_most)", b.metric,
                              m->value, m->unit, *b.max);
        else if (b.min && m->value < *b.min)
            why = std::format("metric {} = {:g} {}, under its bound {:g} (at_least)", b.metric,
                              m->value, m->unit, *b.min);
        if (why.empty())
            continue;
        cr.failures.push_back({"", why, "", "", 0, false});
        cr.status = "fail";
        cr.reason = why;
    }
}

bool unstable(const case_info& c) {
    return std::ranges::find(c.tags, "flaky") != c.tags.end();
}

// What a case adds to the item that runs it: what it holds, how long it may take, how often it may
// run again (its own count, else the run's for a case the project marks unstable or quarantined)
// and its diagnosis.
void add_case(item& it, const case_info& ci, int run_retries, bool quarantined) {
    it.resources.insert(ci.resources.begin(), ci.resources.end());
    const int again = ci.retries > 0 ? ci.retries : (unstable(ci) || quarantined) ? run_retries : 0;
    if (again > 0)
        it.retries[ci.id] = again;
    if (ci.diagnose_env)
        it.diagnosed[ci.id] = &ci;
}

// A failing case of `outcome` that may run again does, alone, until it passes or its count is
// spent; one that then passes is `flaky`, which does not fail the run and is counted.
void retry_failing(const item& it, const run_request& req, run_outcome& outcome) {
    for (auto& cr : outcome.cases) {
        const auto allowed = it.retries.find(cr.id);
        if (!failing(cr.status) || allowed == it.retries.end())
            continue;
        for (int attempt = 1; attempt <= allowed->second && cr.status != "flaky"; ++attempt) {
            run_request again = req;
            again.work =
                req.work / std::format("retry-{}-{}", attempt, std::hash<std::string>{}(cr.id));
            again.select = {cr.id};
            auto second = run_program(it.program, again);
            auto found = std::ranges::find(second.cases, cr.id, &case_result::id);
            cr.attempts = attempt + 1;
            if (found != second.cases.end() && found->status == "pass") {
                cr.status = "flaky";
                cr.reason = std::format("failed, then passed on attempt {}", attempt + 1);
            }
        }
    }
}

// A case that still fails and states a diagnosis runs once more, alone, with its environment; the
// output is kept in the store beside the run and named by the result. It judges nothing.
void diagnose_failing(const store& s, std::string_view run, const item& it, const run_request& req,
                      run_outcome& outcome) {
    for (auto& cr : outcome.cases) {
        const auto d = it.diagnosed.find(cr.id);
        if (!failing(cr.status) || d == it.diagnosed.end())
            continue;
        run_request again = req;
        again.work = req.work / std::format("diagnose-{}", std::hash<std::string>{}(cr.id));
        again.select = {cr.id};
        again.env.insert(again.env.end(), d->second->diagnose_env->begin(),
                         d->second->diagnose_env->end());
        auto program = it.program;
        if (d->second->diagnose_timeout > 0)
            program.timeout = case_limit(d->second->diagnose_timeout);
        auto second = run_program(program, again);
        const auto dir = run_dir(s, run) / "diagnoses" /
                         std::to_string(std::hash<std::string>{}(cr.program + "::" + cr.id));
        std::filesystem::create_directories(dir);
        (void)os::fs::write(dir / "out.txt", second.out);
        (void)os::fs::write(dir / "err.txt", second.err);
        cr.diagnosis = os::fs::generic_utf8(dir);
    }
}

}  // namespace

run_record execute(const config& c, const store& s, const plan& p, const execute_options& o) {
    run_record rec;
    rec.id = o.id.empty() ? new_run_id() : o.id;
    rec.started = now_iso();
    rec.system = std::string(os::name(os::host()));
    rec.arch = std::string(os::name(os::ARCH));
    rec.machine = os::env::get("HOSTNAME").value_or(os::env::get("COMPUTERNAME").value_or(""));
    rec.commit = git_commit(c.root);
    rec.set = o.set;
    rec.selection = o.selection;
    const int hw = int(std::max(1u, std::thread::hardware_concurrency()));
    rec.jobs = o.jobs > 0 ? o.jobs : c.jobs > 0 ? c.jobs : hw;
    const int retries = o.retries >= 0 ? o.retries : c.retries;
    const auto quarantined_cases = quarantine(s);
    const auto began = std::chrono::steady_clock::now();
    auto emit = [&](const json& e) {
        if (o.on_event)
            o.on_event(e);
    };

    std::vector<item> items;
    // Cases mtest.toml skips are recorded so, and not run.
    std::vector<case_result> skipped;
    auto skips = [&](const unit& u, const unit_case& uc) {
        if (uc.info.skip.empty())
            return false;
        case_result cr;
        cr.program = u.program.name;
        cr.id = uc.info.id;
        cr.kind = uc.info.kind;
        cr.status = "skip";
        cr.reason = uc.info.skip;
        skipped.push_back(std::move(cr));
        return true;
    };
    for (const auto& u : p.units) {
        std::set<std::string> program_resources(u.program.resources.begin(),
                                                u.program.resources.end());
        if (u.program.level < 1 && !u.cases.empty() && skips(u, u.cases.front()))
            continue;
        if (u.program.isolation == "case" && u.program.level >= 1) {
            for (const auto& uc : u.cases) {
                if (skips(u, uc))
                    continue;
                item it{u.program,
                        {uc.info.id},
                        program_resources,
                        u.expected_ms / double(u.cases.size()),
                        {}};
                add_case(it, uc.info, retries,
                         is_quarantined(quarantined_cases, u.program.name, uc.info.id));
                // P3.3: a case's own limit is its process's.
                if (uc.info.timeout > 0)
                    it.program.timeout = case_limit(uc.info.timeout);
                items.push_back(std::move(it));
            }
            continue;
        }
        // A case that states isolation runs alone; the others share one process.
        const auto per_case = u.expected_ms / double(std::max<std::size_t>(1, u.cases.size()));
        item it{u.program, {}, program_resources, u.expected_ms, {}};
        // A case that runs alone or is skipped narrows the rest to a selection.
        const bool narrowed = std::ranges::any_of(u.cases, [](const unit_case& uc) {
            return uc.info.isolated || !uc.info.skip.empty();
        });
        // A process of several cases runs at least as long as the limits its cases state.
        std::chrono::seconds stated{0};
        for (const auto& uc : u.cases) {
            const bool quarantined = is_quarantined(quarantined_cases, u.program.name, uc.info.id);
            if (u.program.level >= 1 && skips(u, uc)) {
                it.expected_ms -= per_case;
                continue;
            }
            if (uc.info.isolated && u.program.level >= 1) {
                item alone{u.program, {uc.info.id}, program_resources, per_case, {}};
                add_case(alone, uc.info, retries, quarantined);
                if (uc.info.timeout > 0)
                    alone.program.timeout = case_limit(uc.info.timeout);
                it.expected_ms -= per_case;
                items.push_back(std::move(alone));
                continue;
            }
            if ((!u.all || narrowed) && u.program.level >= 1)
                it.select.push_back(uc.info.id);
            add_case(it, uc.info, retries, quarantined);
            if (uc.info.timeout > 0)
                stated += case_limit(uc.info.timeout);
        }
        it.program.timeout = std::max(it.program.timeout, stated);
        if (u.program.level < 1 || !it.select.empty() || (u.all && !narrowed))
            items.push_back(std::move(it));
    }
    std::ranges::sort(items, std::greater{}, &item::expected_ms);

    std::mutex m;
    std::condition_variable cv;
    locks held;
    std::vector<bool> started(items.size(), false);
    std::size_t done = 0, failed = 0;
    bool stop = false, cancelled = false;
    std::vector<std::pair<std::size_t, run_outcome>> outcomes;

    emit({{"event", "run/started"},
          {"run", rec.id},
          {"programs", items.size()},
          {"cases", p.selected}});
    write_progress(s, rec.id,
                   {{"run", rec.id},
                    {"done", 0},
                    {"total", items.size()},
                    {"started", rec.started}});

    auto worker = [&] {
        for (;;) {
            std::size_t mine = 0;
            {
                std::unique_lock g(m);
                for (;;) {
                    if (cancel_requested(s, rec.id)) {
                        stop = true;
                        cancelled = true;
                    }
                    if (stop)
                        return;
                    bool remaining = false, found = false;
                    for (std::size_t i = 0; i < items.size(); ++i) {
                        if (started[i])
                            continue;
                        remaining = true;
                        if (held.can_take(items[i].resources)) {
                            mine = i;
                            found = true;
                            break;
                        }
                    }
                    if (!remaining)
                        return;
                    if (found)
                        break;
                    cv.wait_for(g, std::chrono::milliseconds{200});
                }
                started[mine] = true;
                held.take(items[mine].resources);
            }
            const auto& it = items[mine];
            run_request req;
            req.work = s.dir / "work" / rec.id / std::to_string(mine);
            req.root = c.root;
            req.select = it.select;
            req.seed = o.seed;
            req.env = o.env;
            req.run_under = o.run_under;
            if (o.coverage_dir) {
                req.env.emplace_back("GCOV_PREFIX",
                                     os::fs::utf8(*o.coverage_dir / std::to_string(mine)));
                req.env.emplace_back("GCOV_PREFIX_STRIP", std::to_string(o.coverage_strip));
            }
            const auto started_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began)
                    .count();
            auto outcome = run_program(it.program, req);
            outcome.program.started_ms = started_ms;
            outcome.program.resources.assign(it.resources.begin(), it.resources.end());
            retry_failing(it, req, outcome);
            diagnose_failing(s, rec.id, it, req, outcome);
            for (auto& cr : outcome.cases)
                if (failing(cr.status) && is_quarantined(quarantined_cases, cr.program, cr.id))
                    cr.quarantined = true;
            const bool bad = std::ranges::any_of(outcome.cases, [](const case_result& cr) {
                return failing(cr.status) && !cr.quarantined;
            });
            for (const auto& cr : outcome.cases)
                emit({{"event", "case/finished"},
                      {"run", rec.id},
                      {"program", cr.program},
                      {"case", cr.id},
                      {"status", cr.status},
                      {"ms", cr.ms}});
            emit({{"event", "program/finished"},
                  {"run", rec.id},
                  {"program", outcome.program.name},
                  {"status", outcome.program.status},
                  {"ms", outcome.program.ms}});
            {
                std::lock_guard g(m);
                held.give(it.resources);
                ++done;
                if (bad) {
                    ++failed;
                    if (o.fail_fast)
                        stop = true;
                }
                write_progress(s, rec.id,
                               {{"run", rec.id},
                                {"done", done},
                                {"total", items.size()},
                                {"failed", failed},
                                {"started", rec.started}});
                if (o.coverage_dir && o.coverage_cases) {
                    auto& cases = (*o.coverage_cases)[*o.coverage_dir / std::to_string(mine)];
                    for (const auto& cr : outcome.cases)
                        cases.push_back(cr.program + "::" + cr.id);
                }
                outcomes.emplace_back(mine, std::move(outcome));
            }
            cv.notify_all();
        }
    };
    {
        std::vector<std::jthread> pool;
        const int n = std::min<int>(rec.jobs, int(std::max<std::size_t>(1, items.size())));
        for (int i = 0; i < n; ++i)
            pool.emplace_back(worker);
    }

    std::ranges::sort(outcomes, {}, &std::pair<std::size_t, run_outcome>::first);
    std::size_t n = 0;
    for (auto& [index, outcome] : outcomes) {
        if (outcome.program.status != "pass")
            write_failure(s, rec.id, n++, outcome.program, outcome.out, outcome.err);
        rec.programs.push_back(outcome.program);
        for (auto& cr : outcome.cases) {
            if (const auto info = std::ranges::find_if(p.units,
                                                       [&](const unit& u) {
                                                           return u.program.name == cr.program;
                                                       });
                info != p.units.end()) {
                auto ci = std::ranges::find(info->cases, cr.id,
                                            [](const unit_case& x) { return x.info.id; });
                if (ci != info->cases.end()) {
                    cr.kind = ci->info.kind;
                    cr.tags = ci->info.tags;
                    cr.clauses = ci->info.clauses;
                    cr.bounds = ci->info.bounds;
                    judge_bounds(cr, ci->info.stated_bounds);
                    expected_to_fail(cr, ci->info.expect_fail);
                } else if (cr.kind.empty()) {
                    // A case a listed case declared (P3.4) is of its program's kind unless its
                    // begin stated one.
                    cr.kind = info->program.kind.empty() ? "unit" : info->program.kind;
                }
            }
            rec.cases.push_back(std::move(cr));
        }
    }
    for (auto& cr : skipped)
        rec.cases.push_back(std::move(cr));
    std::error_code ec;
    std::filesystem::remove_all(s.dir / "work" / rec.id, ec);
    rec.counts = count(rec.cases);
    rec.ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
    rec.finished = now_iso();
    const bool test_failed = std::ranges::any_of(rec.cases, [](const case_result& cr) {
        return failing(cr.status) && !cr.quarantined;
    });
    const bool infra =
        std::ranges::any_of(rec.cases, [](const case_result& cr) { return cr.status == "infra"; });
    rec.status = cancelled ? "cancelled" : test_failed ? "fail" : infra ? "infra" : "pass";
    write_run(s, rec);
    finish_progress(s, rec.id);
    emit({{"event", "run/finished"},
          {"run", rec.id},
          {"status", rec.status},
          {"counts", rec.counts},
          {"ms", rec.ms}});
    return rec;
}

}  // namespace mcpplibs::mtest::engine
