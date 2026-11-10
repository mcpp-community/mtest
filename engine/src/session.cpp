module mcpplibs.mtest.engine.session;

import std;
import mcpplibs.os;
import mcpplibs.mtest.engine.model;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {

// How mcpp starts one test program (B5 `launch`), and the protocol it implements.
struct launch {
    std::vector<std::string> argv;
    std::vector<std::pair<std::string, std::string>> env;
    std::filesystem::path cwd;
    int protocol = 0;
};

// One process of a run: a program, the cases it runs (none: all of them), and what it did.
struct job {
    std::size_t serial = 0;
    std::string run;
    std::string program;
    std::vector<std::string> cases;
    launch how;
    std::set<std::string> begun, ended, cancelled_cases;
    bool cancelled = false;  // the whole process is cancelled
    bool requeue = false;    // its cases that did not begin run in another process
    os::process::process* proc = nullptr;
};

// A run's verdict: a failure fails it; else a cancelled case cancels it.
std::string verdict(bool cancelled, const std::map<std::string, int>& counts) {
    if (counts.contains("fail") || counts.contains("build-fail"))
        return "fail";
    return cancelled || counts.contains("cancelled") ? "cancelled" : "pass";
}

struct run_state {
    std::filesystem::path project;
    std::vector<std::string> mcpp;  // the program, and arguments before `test`
    std::vector<std::string> args;  // more arguments of `mcpp test` (profile, target)
    bool isolate = false;           // a process per case
    std::size_t outstanding = 0;    // builds and processes not yet ended
    bool cancelled = false, ended = false;
    std::map<std::string, int> counts;
    std::map<std::string, std::string> cases;  // "<program>/<id>" -> status
};

std::vector<json> records_of(std::string_view text) {
    std::vector<json> out;
    for (std::size_t at = 0; at < text.size();) {
        auto nl = text.find('\n', at);
        auto line =
            text.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at);
        at = nl == std::string_view::npos ? text.size() : nl + 1;
        if (line.empty() || line.front() != '{')
            continue;
        if (auto j = json::parse(line, nullptr, false); j.is_object())
            out.push_back(std::move(j));
    }
    return out;
}

}  // namespace

struct session::impl {
    notify_fn notify;
    std::filesystem::path tmp;
    std::mutex m;
    std::condition_variable cv;
    bool stopping = false;
    std::deque<std::shared_ptr<job>> pending;
    std::vector<std::shared_ptr<job>> running;
    std::map<std::string, run_state> runs;
    std::size_t next_run = 1, next_job = 1;
    std::mutex build_m;  // one build at a time: the build state is mcpp's (B5)
    std::vector<std::jthread> workers;
    std::vector<std::jthread> planners;

    // `mcpp test` with `args` in `project`, its records.
    std::pair<int, std::vector<json>> call_mcpp(const run_state& r, std::vector<std::string> args) {
        os::process::spawn_options o;
        o.program = r.mcpp.front();
        o.args.assign(r.mcpp.begin() + 1, r.mcpp.end());
        o.args.insert(o.args.end(), args.begin(), args.end());
        o.args.insert(o.args.end(), r.args.begin(), r.args.end());
        o.cwd = r.project;
        o.in = os::process::stdio::null;
        auto out = os::process::run(o);
        if (!out)
            return {-1,
                    {json{{"error", std::format("{} did not start: {}", r.mcpp.front(),
                                                out.error().message())}}}};
        return {out->status.shell_code(), records_of(out->out)};
    }

    void case_end(const job& j, const std::string& id, std::string_view status, const json& extra) {
        json p = {{"run", j.run},
                  {"program", j.program},
                  {"id", id},
                  {"event", "end"},
                  {"status", status}};
        for (auto it = extra.begin(); it != extra.end(); ++it)
            p[it.key()] = *it;
        {
            std::lock_guard g(m);
            auto& r = runs[j.run];
            r.counts[std::string(status)]++;
            r.cases[j.program + "/" + id] = std::string(status);
        }
        notify("test/case", p);
    }

    // The run ends when nothing of it is outstanding; m held.
    void maybe_end(const std::string& id) {
        auto& r = runs[id];
        if (r.outstanding != 0 || r.ended)
            return;
        r.ended = true;
        cv.notify_all();
        notify("test/run",
               {{"run", id}, {"status", verdict(r.cancelled, r.counts)}, {"counts", r.counts}});
    }

    void enqueue(const std::string& run, const std::string& program, const launch& how,
                 const std::vector<std::string>& cases) {
        auto& r = runs[run];
        auto add = [&](std::vector<std::string> one) {
            auto j = std::make_shared<job>();
            j->serial = next_job++;
            j->run = run;
            j->program = program;
            j->cases = std::move(one);
            j->how = how;
            pending.push_back(j);
            ++r.outstanding;
        };
        if (r.isolate && !cases.empty())
            for (const auto& c : cases)
                add({c});
        else
            add(cases);
        cv.notify_all();
    }

    // B5: the selected programs built when they need to be, and their launches; then their jobs.
    void
    build_and_enqueue(const std::string& run,
                      std::vector<std::pair<std::string, std::vector<std::string>>> selection) {
        run_state r;
        {
            std::lock_guard g(m);
            r = runs[run];
        }
        std::map<std::string, launch> launches;
        {
            std::lock_guard b(build_m);
            notify("test/build", {{"run", run}, {"event", "begin"}});
            std::vector<std::string> args = {"test", "--no-run", "--message-format", "json"};
            if (selection.size() == 1)
                args.push_back(selection.front().first);
            auto [status, records] = call_mcpp(r, args);
            json built = json::array();
            for (const auto& rec : records) {
                const auto name = rec.value("test", std::string{});
                if (name.empty())
                    continue;
                if (rec.value("status", std::string{}) == "built" && rec.contains("launch")) {
                    const auto& l = rec["launch"];
                    launch one;
                    one.argv = l.value("argv", std::vector<std::string>{});
                    if (l.contains("env") && l["env"].is_object())
                        for (auto it = l["env"].begin(); it != l["env"].end(); ++it)
                            one.env.emplace_back(it.key(), it->get<std::string>());
                    one.cwd = l.value("cwd", std::string{});
                    one.protocol = rec.value("protocol", json(nullptr)).is_number()
                                       ? rec["protocol"].get<int>()
                                       : 0;
                    launches[name] = std::move(one);
                    built.push_back(name);
                } else if (rec.value("status", std::string{}) == "compile_fail") {
                    std::lock_guard g(m);
                    runs[run].counts["build-fail"]++;
                    notify("test/program",
                           {{"run", run},
                            {"program", name},
                            {"status", "build-fail"},
                            {"output", rec.value("compile_output", std::string{})}});
                }
            }
            const bool ok = status == 0 || !launches.empty();
            json end = {{"run", run}, {"event", "end"}, {"ok", ok}, {"programs", built}};
            if (!ok && !records.empty() && records.front().contains("error"))
                end["error"] = records.front()["error"];
            else if (!ok)
                end["error"] = std::
                    format("mcpp test --no-run exited {} and named no launch (it gives one from "
                           "2026.11.10.1, feature `test-launch-records`)",
                           status);
            notify("test/build", end);
        }
        std::lock_guard g(m);
        auto& rs = runs[run];
        if (!rs.cancelled) {
            if (selection.empty())
                for (const auto& [name, how] : launches)
                    enqueue(run, name, how, {});
            for (const auto& [program, cases] : selection)
                if (auto it = launches.find(program); it != launches.end())
                    enqueue(run, program, it->second, cases);
        }
        --rs.outstanding;
        maybe_end(run);
    }

    void execute(job& j) {
        const auto dir = tmp / j.run / std::format("{}", j.serial);
        std::error_code ec;
        std::filesystem::create_directories(dir / "tmp", ec);
        const auto report = dir / "report.ndjson";
        os::process::spawn_options o;
        if (j.how.argv.empty())
            return;
        o.program = j.how.argv.front();
        o.args.assign(j.how.argv.begin() + 1, j.how.argv.end());
        o.cwd = j.how.cwd;
        o.in = os::process::stdio::null;
        o.out = os::process::stdio::pipe;
        o.err = os::process::stdio::pipe;
        o.own_unit = true;  // cancelling ends it and everything it started
        for (const auto& [k, v] : j.how.env)
            o.env.emplace_back(k, v);
        if (j.how.protocol) {
            o.env.emplace_back("MTEST_PROTOCOL", "1");
            o.env.emplace_back("MTEST_MODE", "run");
            o.env.emplace_back("MTEST_REPORT", report.string());
            o.env.emplace_back("MTEST_TMPDIR", (dir / "tmp").string());
            if (!j.cases.empty()) {
                std::string lines;
                for (const auto& c : j.cases)
                    lines += c + "\n";
                (void)os::fs::write(dir / "select.txt", lines);
                o.env.emplace_back("MTEST_SELECT", (dir / "select.txt").string());
            }
        }
        const auto started = std::chrono::steady_clock::now();
        auto p = os::process::process::spawn(o);
        if (!p) {
            notify("test/program",
                   {{"run", j.run},
                    {"program", j.program},
                    {"status", "not-run"},
                    {"reason", std::format("{} did not start: {}", o.program.string(),
                                           p.error().message())}});
            return;
        }
        {
            std::lock_guard g(m);
            j.proc = &*p;
            if (j.cancelled)
                p->kill();
        }
        std::string out_text;
        std::jthread out_reader([&] {
            for (;;) {
                auto chunk = p->read_output();
                if (!chunk || chunk->empty())
                    break;
                out_text += *chunk;
            }
        });
        std::jthread err_reader([&] {
            for (;;) {
                auto chunk = p->read_error();
                if (!chunk || chunk->empty())
                    break;
            }
        });
        std::uintmax_t offset = 0;
        auto poll = [&] {
            std::ifstream in(report, std::ios::binary);
            if (!in)
                return;
            in.seekg(static_cast<std::streamoff>(offset));
            std::string text{std::istreambuf_iterator<char>(in), {}};
            const auto last = text.rfind('\n');
            if (last == std::string::npos)
                return;
            offset += last + 1;
            for (const auto& e : records_of(std::string_view(text).substr(0, last + 1))) {
                const auto kind = e.value("e", std::string{});
                const auto id = e.value("id", std::string{});
                if (kind == "begin") {
                    {
                        std::lock_guard g(m);
                        j.begun.insert(id);
                    }
                    notify("test/case", {{"run", j.run},
                                         {"program", j.program},
                                         {"id", id},
                                         {"event", "begin"},
                                         {"t_ms", e.value("t", 0.0)}});
                } else if (kind == "end") {
                    {
                        std::lock_guard g(m);
                        j.ended.insert(id);
                    }
                    json extra = {{"ms", e.value("ms", 0.0)}};
                    for (const char* k : {"reason", "checks"})
                        if (e.contains(k))
                            extra[k] = e[k];
                    case_end(j, id, e.value("status", std::string("fail")), extra);
                } else if (kind == "output" || kind == "failure") {
                    json n = e;
                    n.erase("e");
                    n["run"] = j.run;
                    n["program"] = j.program;
                    n["event"] = kind;
                    notify("test/case", n);
                }
            }
        };
        std::optional<os::process::exit_status> st;
        for (;;) {
            auto w = p->wait_for(std::chrono::milliseconds(50));
            poll();
            if (!w)
                break;
            if (*w) {
                st = **w;
                break;
            }
        }
        out_reader.join();
        err_reader.join();
        poll();
        {
            std::lock_guard g(m);
            j.proc = nullptr;
        }
        const int code = st ? st->shell_code() : -1;
        // P5.4: a case that began and did not end failed, or was cancelled.
        std::set<std::string> begun;
        {
            std::lock_guard g(m);
            begun = j.begun;
        }
        for (const auto& id : begun)
            if (!j.ended.contains(id)) {
                const bool cancelled = j.cancelled || j.cancelled_cases.contains(id) || j.requeue;
                case_end(j, id, cancelled ? "cancelled" : "fail",
                         {{"reason", cancelled ? std::string("cancelled")
                                               : std::format("the program ended with status {} "
                                                             "before the case ended",
                                                             code)}});
                std::lock_guard g(m);
                j.ended.insert(id);
            }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        std::string status = j.cancelled ? "cancelled" : code == 0 ? "pass" : "fail";
        json prog = {{"run", j.run},
                     {"program", j.program},
                     {"status", status},
                     {"exit", code},
                     {"ms", ms}};
        if (!j.how.protocol || (code != 0 && j.begun.empty()))
            prog["output"] = out_text;
        if (!j.how.protocol) {
            // A program of no protocol is one case, judged by its exit status (P1.1).
            case_end(j, j.program,
                     j.cancelled ? "cancelled"
                     : code == 0 ? "pass"
                                 : "fail",
                     {{"ms", ms}});
        }
        notify("test/program", prog);
        std::filesystem::remove_all(dir, ec);
    }

    // After a job: the cases it was to run that did not begin, cancelled with it or run again.
    void settle(const std::shared_ptr<job>& j) {
        std::lock_guard g(m);
        std::vector<std::string> rest;
        for (const auto& c : j->cases)
            if (!j->begun.contains(c) && !j->cancelled_cases.contains(c))
                rest.push_back(c);
        auto& r = runs[j->run];
        if (j->requeue && !j->cancelled && !r.cancelled && !rest.empty())
            enqueue(j->run, j->program, j->how, rest);
        else if (j->cancelled || r.cancelled)
            for (const auto& c : rest) {
                r.counts["cancelled"]++;
                r.cases[j->program + "/" + c] = "cancelled";
                notify("test/case", {{"run", j->run},
                                     {"program", j->program},
                                     {"id", c},
                                     {"event", "end"},
                                     {"status", "cancelled"},
                                     {"reason", "cancelled"}});
            }
        std::erase(running, j);
        --r.outstanding;
        maybe_end(j->run);
    }

    void work() {
        for (;;) {
            std::shared_ptr<job> j;
            {
                std::unique_lock l(m);
                cv.wait(l, [&] { return stopping || !pending.empty(); });
                if (pending.empty())
                    return;
                j = pending.front();
                pending.pop_front();
                running.push_back(j);
            }
            execute(*j);
            settle(j);
        }
    }
};

session::session(notify_fn notify) : d_(std::make_unique<impl>()) {
    d_->notify = std::move(notify);
    const auto self = os::process::self();
    d_->tmp = std::filesystem::temp_directory_path() /
              std::format("mtest-session-{}-{}", self ? self->id : 0, self ? self->started_ns : 0);
    unsigned n = std::max(2u, std::thread::hardware_concurrency());
    for (unsigned i = 0; i < n; ++i)
        d_->workers.emplace_back([this] { d_->work(); });
}

session::~session() {
    shutdown();
}

namespace {
run_state state_of(const json& params) {
    run_state r;
    r.project = params.value("project", std::filesystem::current_path().string());
    const auto options = params.value("options", json::object());
    // `mcpp`: a program, or a program and the arguments before `test`; else MTEST_MCPP, else mcpp.
    if (const auto m = options.value("mcpp", json()); m.is_string())
        r.mcpp = {m.get<std::string>()};
    else if (m.is_array())
        r.mcpp = m.get<std::vector<std::string>>();
    if (r.mcpp.empty() || r.mcpp.front().empty())
        r.mcpp = {os::env::get("MTEST_MCPP").value_or("mcpp")};
    r.args = options.value("args", std::vector<std::string>{});
    r.isolate = options.value("isolation", std::string{}) == "case";
    return r;
}

std::vector<std::pair<std::string, std::vector<std::string>>> selection_of(const json& params) {
    std::vector<std::pair<std::string, std::vector<std::string>>> out;
    for (const auto& s : params.value("selection", json::array()))
        out.emplace_back(s.value("program", std::string{}),
                         s.value("cases", std::vector<std::string>{}));
    return out;
}
}  // namespace

json session::plan(const json& params) {
    auto r = state_of(params);
    std::vector<std::string> args = {"test", "--cases", "--message-format", "json"};
    if (auto f = params.value("filter", std::string{}); !f.empty())
        args.push_back(f);
    std::lock_guard b(d_->build_m);
    auto [status, records] = d_->call_mcpp(r, args);
    json programs = json::array();
    for (const auto& rec : records)
        if (rec.contains("test"))
            programs.push_back(rec);
    if (programs.empty() && status != 0)
        return {{"error", std::format("mcpp test --cases exited {}", status)}};
    return {{"programs", programs}};
}

json session::run(const json& params) {
    auto r = state_of(params);
    auto selection = selection_of(params);
    std::string id;
    {
        std::lock_guard g(d_->m);
        if (d_->stopping)
            return {{"error", "the session is shutting down"}};
        id = std::format("r{}", d_->next_run++);
        r.outstanding = 1;  // its build
        d_->runs[id] = std::move(r);
    }
    {
        std::lock_guard g(d_->m);
        d_->planners.emplace_back([this, id, selection] { d_->build_and_enqueue(id, selection); });
    }
    return {{"run", id}};
}

json session::add(const json& params) {
    const auto id = params.value("run", std::string{});
    auto selection = selection_of(params);
    {
        std::lock_guard g(d_->m);
        auto it = d_->runs.find(id);
        if (it == d_->runs.end())
            return {{"error", std::format("no run {}", id)}};
        if (it->second.ended || it->second.cancelled)
            return {{"error", std::format("run {} has ended", id)}};
        ++it->second.outstanding;  // its build
        d_->planners.emplace_back([this, id, selection] { d_->build_and_enqueue(id, selection); });
    }
    return {{"run", id}};
}

json session::cancel(const json& params) {
    const auto id = params.value("run", std::string{});
    const auto program = params.value("program", std::string{});
    const auto one = params.value("case", std::string{});
    std::lock_guard g(d_->m);
    auto it = d_->runs.find(id);
    if (it == d_->runs.end())
        return {{"error", std::format("no run {}", id)}};
    auto& r = it->second;
    if (program.empty() && one.empty())
        r.cancelled = true;
    auto matches = [&](const job& j) {
        return j.run == id && (program.empty() || j.program == program);
    };
    // What has not started: removed, or the case taken out of its selection.
    for (auto p = d_->pending.begin(); p != d_->pending.end();) {
        auto& j = **p;
        if (!matches(j)) {
            ++p;
            continue;
        }
        std::vector<std::string> dropped;
        if (one.empty())
            dropped = j.cases.empty() ? std::vector<std::string>{} : j.cases;
        else if (std::erase(j.cases, one) > 0)
            dropped = {one};
        else {
            ++p;
            continue;
        }
        for (const auto& c : dropped) {
            r.counts["cancelled"]++;
            r.cases[j.program + "/" + c] = "cancelled";
            d_->notify("test/case", {{"run", id},
                                     {"program", j.program},
                                     {"id", c},
                                     {"event", "end"},
                                     {"status", "cancelled"},
                                     {"reason", "cancelled"}});
        }
        if (one.empty() || j.cases.empty()) {
            d_->notify("test/program",
                       {{"run", id}, {"program", j.program}, {"status", "cancelled"}});
            p = d_->pending.erase(p);
            --r.outstanding;
        } else {
            ++p;
        }
    }
    // What runs: its process tree ended. A case shared with others ends the process; the cases it
    // was to run that did not begin run in another.
    for (const auto& jp : d_->running) {
        auto& j = *jp;
        if (!matches(j))
            continue;
        if (one.empty()) {
            j.cancelled = true;
        } else {
            const bool selected =
                j.cases.empty() || std::ranges::find(j.cases, one) != j.cases.end();
            if (!selected || j.ended.contains(one))
                continue;
            j.cancelled_cases.insert(one);
            if (j.cases.size() == 1)
                j.cancelled = true;
            else
                j.requeue = true;
        }
        if (j.proc)
            j.proc->kill();
    }
    d_->maybe_end(id);
    return {{"run", id},
            {"cancelled", one.empty() ? (program.empty() ? "run" : "program") : "case"}};
}

json session::status(const json& params) {
    std::lock_guard g(d_->m);
    json runs = json::object();
    const auto only = params.value("run", std::string{});
    for (const auto& [id, r] : d_->runs) {
        if (!only.empty() && id != only)
            continue;
        runs[id] = {{"status", r.ended ? verdict(r.cancelled, r.counts) : std::string("running")},
                    {"counts", r.counts},
                    {"cases", r.cases}};
    }
    return {{"runs", runs}};
}

void session::drain() {
    std::unique_lock l(d_->m);
    d_->cv.wait(l, [&] {
        return d_->stopping ||
               std::ranges::all_of(d_->runs, [](const auto& r) { return r.second.ended; });
    });
}

void session::shutdown() {
    if (!d_)
        return;
    {
        std::lock_guard g(d_->m);
        if (d_->stopping)
            return;
        d_->stopping = true;
        for (auto& [id, r] : d_->runs)
            r.cancelled = true;
        for (const auto& j : d_->running) {
            j->cancelled = true;
            if (j->proc)
                j->proc->kill();
        }
        d_->pending.clear();
    }
    d_->cv.notify_all();
    std::vector<std::jthread> planners;
    {
        std::lock_guard g(d_->m);
        planners = std::move(d_->planners);
    }
    planners.clear();
    d_->workers.clear();
    std::error_code ec;
    std::filesystem::remove_all(d_->tmp, ec);
}

}  // namespace mcpplibs::mtest::engine
