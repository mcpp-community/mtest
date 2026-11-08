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
    std::set<std::string> flaky;  // case ids that may be retried
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

bool unstable(const case_info& c) {
    return std::ranges::find(c.tags, "flaky") != c.tags.end();
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
    for (const auto& u : p.units) {
        std::set<std::string> program_resources(u.program.resources.begin(),
                                                u.program.resources.end());
        if (u.program.isolation == "case" && u.program.level >= 1) {
            for (const auto& uc : u.cases) {
                item it{u.program,
                        {uc.info.id},
                        program_resources,
                        u.expected_ms / double(u.cases.size()),
                        {}};
                it.resources.insert(uc.info.resources.begin(), uc.info.resources.end());
                if (unstable(uc.info) ||
                    is_quarantined(quarantined_cases, u.program.name, uc.info.id))
                    it.flaky.insert(uc.info.id);
                items.push_back(std::move(it));
            }
            continue;
        }
        item it{u.program, {}, program_resources, u.expected_ms, {}};
        if (!u.all && u.program.level >= 1)
            for (const auto& uc : u.cases)
                it.select.push_back(uc.info.id);
        for (const auto& uc : u.cases) {
            it.resources.insert(uc.info.resources.begin(), uc.info.resources.end());
            if (unstable(uc.info) || is_quarantined(quarantined_cases, u.program.name, uc.info.id))
                it.flaky.insert(uc.info.id);
        }
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
            auto outcome = run_program(it.program, req);
            // Retries: a failing case the project marks unstable (tag `flaky`) or has quarantined
            // runs again, alone; one that then passes is `flaky`, which does not fail the run and
            // is counted.
            for (auto& cr : outcome.cases) {
                if (!failing(cr.status) || !it.flaky.contains(cr.id))
                    continue;
                for (int attempt = 1; attempt <= retries; ++attempt) {
                    run_request again = req;
                    again.work = req.work / std::format("retry-{}-{}", attempt,
                                                        std::hash<std::string>{}(cr.id));
                    again.select = {cr.id};
                    auto second = run_program(it.program, again);
                    auto found = std::ranges::find(second.cases, cr.id, &case_result::id);
                    cr.attempts = attempt + 1;
                    if (found != second.cases.end() && found->status == "pass") {
                        cr.status = "flaky";
                        cr.reason = std::format("failed, then passed on attempt {}", attempt + 1);
                        break;
                    }
                }
            }
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
                }
            }
            rec.cases.push_back(std::move(cr));
        }
    }
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
