module mcpplibs.mtest.engine.schedule;

import std;
import mcpplibs.os;
import mcpplibs.mtest.engine.exec;
import mcpplibs.mtest.engine.capability;

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
    std::vector<std::pair<std::string, double>> cases;  // its cases and their expected times
    std::set<std::string> fixtures;                     // the run fixtures its cases use (P11)
    std::map<std::string, std::vector<std::string>> case_uses;       // case id -> its fixtures
    std::map<std::string, std::vector<std::string>> case_resources;  // case id -> its own resources
    std::set<std::string> program_resources;                         // the program's own
    double expected_peak = 0;  // bytes, as last measured; 0: unknown
    bool whole = false;        // it runs every case of its program (no selection is passed)
};

// P7.1: `machine` holds the machine alone; `serial:<g>` one at a time per group; `network` many; a
// pool a component brings as many as its capacity (P7.3); `gpu`, `display` and any other name one
// holder at a time. Memory: programs run together while their measured largest resident sets fit.
class locks {
public:
    locks(std::map<std::string, int> pools, double memory)
        : pools_(std::move(pools)), memory_(memory) {}
    bool can_take(const std::set<std::string>& want, double peak) const {
        if (machine_)
            return false;
        if (want.contains("machine"))
            return running_ == 0;
        if (memory_ > 0 && running_ > 0 && used_ + peak > memory_)
            return false;
        for (const auto& r : want) {
            if (r == "network")
                continue;
            const auto n = held_.find(r);
            if (n != held_.end() && n->second >= capacity(r))
                return false;
        }
        return true;
    }
    void take(const std::set<std::string>& want, double peak) {
        ++running_;
        used_ += peak;
        if (want.contains("machine"))
            machine_ = true;
        for (const auto& r : want)
            if (r != "network" && r != "machine")
                ++held_[r];
    }
    void give(const std::set<std::string>& want, double peak) {
        --running_;
        used_ -= peak;
        if (want.contains("machine"))
            machine_ = false;
        for (const auto& r : want)
            if (auto n = held_.find(r); n != held_.end() && --n->second <= 0)
                held_.erase(n);
    }
private:
    int capacity(const std::string& r) const {
        auto it = pools_.find(r);
        return it == pools_.end() ? 1 : std::max(1, it->second);
    }
    std::map<std::string, int> pools_;
    std::map<std::string, int> held_;
    double memory_ = 0, used_ = 0;
    bool machine_ = false;
    int running_ = 0;
};

// DIVIDING A PROGRAM AMONG PROCESSES (`[run] split = "auto"`): while there are fewer items than
// workers, the longest item whose program allows it is divided in two by its cases' expected
// times, the longest case first into the lighter half. Each half runs the same program with a
// selection (MTEST_SELECT), so its cases can be scheduled beside other work.
void split_items(std::vector<item>& items, int jobs) {
    auto splittable = [](const item& it) {
        return it.program.split && it.program.level >= 1 && it.cases.size() >= 2 &&
               !it.resources.contains("machine");
    };
    while (int(items.size()) < jobs) {
        auto longest = items.end();
        for (auto i = items.begin(); i != items.end(); ++i)
            if (splittable(*i) && (longest == items.end() || i->expected_ms > longest->expected_ms))
                longest = i;
        if (longest == items.end())
            return;
        auto cases = longest->cases;
        std::ranges::sort(cases, std::greater{}, &std::pair<std::string, double>::second);
        item a = *longest, b = *longest;
        for (auto* x : {&a, &b}) {
            x->cases.clear();
            x->select.clear();
            x->expected_ms = 0;
            x->whole = false;
        }
        for (const auto& c : cases) {
            item& to = a.expected_ms <= b.expected_ms ? a : b;
            to.cases.push_back(c);
            to.select.push_back(c.first);
            to.expected_ms += c.second;
        }
        auto keep = [](item& x) {
            x.fixtures.clear();
            x.resources = x.program_resources;
            for (const auto& id : x.select)
                if (auto r = x.case_resources.find(id); r != x.case_resources.end())
                    x.resources.insert(r->second.begin(), r->second.end());
            for (const auto& id : x.select)
                if (auto u = x.case_uses.find(id); u != x.case_uses.end())
                    x.fixtures.insert(u->second.begin(), u->second.end());
            std::erase_if(x.retries, [&](const auto& kv) {
                return std::ranges::find(x.select, kv.first) == x.select.end();
            });
            std::erase_if(x.diagnosed, [&](const auto& kv) {
                return std::ranges::find(x.select, kv.first) == x.select.end();
            });
        };
        keep(a);
        keep(b);
        *longest = std::move(a);
        items.push_back(std::move(b));
    }
}

// RUN FIXTURES (P11): each served by one process for the run, started before the first item that
// uses it and ended after the last; their times are nodes of the run's graph.
class fixtures {
public:
    fixtures(const std::map<std::string, program_spec>& providers, std::filesystem::path root,
             std::chrono::steady_clock::time_point began)
        : providers_(providers), root_(std::move(root)), began_(began) {}
    void count_users(const std::vector<item>& items) {
        for (const auto& it : items)
            for (const auto& f : it.fixtures)
                ++users_[f];
    }
    // The endpoints of `names`, serving each first; an error names the fixture that failed.
    std::expected<std::vector<std::pair<std::string, std::string>>, std::string>
    acquire(const std::set<std::string>& names) {
        std::vector<std::pair<std::string, std::string>> env;
        for (const auto& name : names) {
            std::unique_lock g(m_);
            auto& s = served_[name];
            if (!s.started) {
                s.started = true;
                s.run.name = name;
                s.run.started_ms = now();
                auto provider = providers_.find(name);
                if (provider == providers_.end()) {
                    s.run.status = "failed";
                    s.error = std::format("no program serves the run fixture {}", name);
                } else {
                    s.run.program = provider->second.name;
                    start(provider->second, s);
                }
                s.run.ready_ms = now();
                cv_.notify_all();
            }
            cv_.wait(g, [&] { return s.run.ready_ms > 0 || !s.error.empty(); });
            if (!s.error.empty())
                return std::unexpected(s.error);
            env.emplace_back(variable(name), s.endpoint);
        }
        return env;
    }
    void release(const std::set<std::string>& names) {
        for (const auto& name : names) {
            std::lock_guard g(m_);
            if (--users_[name] > 0)
                continue;
            auto& s = served_[name];
            if (s.process) {
                s.process->close_input();
                (void)s.process->wait();
                s.process.reset();
            }
            s.run.ended_ms = now();
        }
    }
    std::vector<fixture_run> runs() {
        std::lock_guard g(m_);
        std::vector<fixture_run> out;
        for (auto& [name, s] : served_) {
            if (s.process) {
                s.process->close_input();
                (void)s.process->wait();
                s.process.reset();
                s.run.ended_ms = now();
            }
            out.push_back(s.run);
        }
        return out;
    }
    static std::string variable(std::string_view name) {
        std::string key = "MTEST_FIXTURE_";
        for (char c : name)
            key += std::isalnum(static_cast<unsigned char>(c))
                       ? char(std::toupper(static_cast<unsigned char>(c)))
                       : '_';
        return key;
    }
private:
    struct served {
        bool started = false;
        fixture_run run;
        std::string endpoint, error;
        std::optional<os::process::process> process;
    };
    double now() const {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began_)
            .count();
    }
    void start(const program_spec& p, served& s) {
        os::process::spawn_options o;
        o.program = os::fs::from_utf8(p.argv.front());
        o.args.assign(p.argv.begin() + 1, p.argv.end());
        o.cwd = p.cwd.empty() ? root_ : root_ / os::fs::from_utf8(p.cwd);
        o.in = os::process::stdio::pipe;
        o.out = os::process::stdio::pipe;
        o.err = os::process::stdio::inherit;
        for (const auto& [k, v] : p.env)
            o.env.emplace_back(k, v);
        o.env.emplace_back("MTEST_SERVE", s.run.name);
        o.env.emplace_back("MTEST_PROTOCOL", std::nullopt);
        o.env.emplace_back("MTEST_REPORT", std::nullopt);
        auto proc = os::process::process::spawn(o);
        if (!proc) {
            s.run.status = "failed";
            s.error = std::format("the run fixture {} did not start: {}", s.run.name,
                                  proc.error().message());
            return;
        }
        std::string text;
        for (;;) {
            auto chunk = proc->read_output();
            if (!chunk || chunk->empty())
                break;
            text += *chunk;
            if (auto nl = text.find('\n'); nl != std::string::npos) {
                const auto line = text.substr(0, nl);
                if (line.starts_with("mtest-ready ")) {
                    s.endpoint = line.substr(12);
                    if (s.endpoint.ends_with('\r'))
                        s.endpoint.pop_back();
                    s.run.status = "ready";
                    s.process = std::move(*proc);
                    return;
                }
                text.erase(0, nl + 1);
            }
        }
        s.run.status = "failed";
        s.error = std::format("the run fixture {} ended before it was ready", s.run.name);
    }
    const std::map<std::string, program_spec>& providers_;
    std::filesystem::path root_;
    std::chrono::steady_clock::time_point began_;
    std::mutex m_;
    std::condition_variable cv_;
    std::map<std::string, int> users_;
    std::map<std::string, served> served_;
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
    it.case_resources[ci.id] = ci.resources;
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
    rec.group = !o.group.empty() ? o.group : os::env::get("MTEST_GROUP").value_or("");
    rec.monitor = !o.monitor.empty() ? o.monitor : c.monitor.level;
    rec.host = host_name();
    for (const auto& [word, a] : machine_capabilities(c))
        rec.capabilities[word] = a.present;
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
                it.cases.emplace_back(uc.info.id, uc.expected_ms);
                it.fixtures.insert(uc.info.uses.begin(), uc.info.uses.end());
                it.expected_peak = u.expected_peak.value_or(0);
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
        it.expected_peak = u.expected_peak.value_or(0);
        it.program_resources = program_resources;
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
                alone.cases.emplace_back(uc.info.id, uc.expected_ms);
                alone.fixtures.insert(uc.info.uses.begin(), uc.info.uses.end());
                alone.expected_peak = u.expected_peak.value_or(0);
                add_case(alone, uc.info, retries, quarantined);
                if (uc.info.timeout > 0)
                    alone.program.timeout = case_limit(uc.info.timeout);
                it.expected_ms -= per_case;
                items.push_back(std::move(alone));
                continue;
            }
            if ((!u.all || narrowed) && u.program.level >= 1)
                it.select.push_back(uc.info.id);
            it.cases.emplace_back(uc.info.id, uc.expected_ms);
            it.fixtures.insert(uc.info.uses.begin(), uc.info.uses.end());
            it.case_uses[uc.info.id] = uc.info.uses;
            add_case(it, uc.info, retries, quarantined);
            if (uc.info.timeout > 0)
                stated += case_limit(uc.info.timeout);
        }
        it.program.timeout = std::max(it.program.timeout, stated);
        it.whole = it.select.empty();
        if (u.program.level < 1 || !it.select.empty() || (u.all && !narrowed))
            items.push_back(std::move(it));
    }
    if (c.run.split == "auto")
        split_items(items, rec.jobs);
    std::ranges::sort(items, std::greater{}, &item::expected_ms);

    std::mutex m;
    std::condition_variable cv;
    const double memory = [&]() -> double {
        if (c.run.memory == "off")
            return 0;
        if (c.run.memory == "auto")
            return double(os::machine::memory().value_or(0));
        return parse_size(c.run.memory).value_or(0);
    }();
    locks held(p.pools, memory);
    fixtures served(p.fixture_providers, c.root, began);
    served.count_users(items);
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

    auto worker = [&](int slot) {
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
                        if (held.can_take(items[i].resources, items[i].expected_peak)) {
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
                held.take(items[mine].resources, items[mine].expected_peak);
            }
            const auto& it = items[mine];
            run_request req;
            req.work = s.dir / "work" / rec.id / std::to_string(mine);
            req.root = c.root;
            req.select = it.select;
            req.seed = o.seed;
            req.env = o.env;
            req.run_under = o.run_under;
            req.monitor = rec.monitor;
            req.sample_ms = c.monitor.sample_ms;
            auto endpoints = served.acquire(it.fixtures);
            if (o.coverage_dir) {
                req.env.emplace_back("GCOV_PREFIX",
                                     os::fs::utf8(*o.coverage_dir / std::to_string(mine)));
                req.env.emplace_back("GCOV_PREFIX_STRIP", std::to_string(o.coverage_strip));
            }
            const auto started_ms =
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began)
                    .count();
            run_outcome outcome;
            if (!endpoints) {
                // The run fixture failed: the machine's failure, not the cases'.
                outcome.program.name = it.program.name;
                outcome.program.status = "infra";
                outcome.program.error = endpoints.error();
                for (const auto& [id, ms] : it.cases) {
                    case_result cr;
                    cr.program = it.program.name;
                    cr.id = it.program.level >= 1 ? id : it.program.name;
                    cr.status = "infra";
                    cr.reason = endpoints.error();
                    outcome.cases.push_back(std::move(cr));
                }
            } else {
                for (auto& kv : *endpoints)
                    req.env.push_back(std::move(kv));
                outcome = run_program(it.program, req);
            }
            served.release(it.fixtures);
            outcome.program.started_ms = started_ms;
            outcome.program.worker = slot;
            outcome.program.select = it.select;
            outcome.program.fixtures.assign(it.fixtures.begin(), it.fixtures.end());
            outcome.program.build_started_ms = it.program.build_started_ms;
            outcome.program.build_ms = it.program.build_ms;
            for (auto& cr : outcome.cases)
                if (cr.started_ms >= 0)
                    cr.started_ms += started_ms;
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
                held.give(it.resources, it.expected_peak);
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
            pool.emplace_back(worker, i);
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
    rec.fixtures = served.runs();
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
