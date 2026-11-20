module mcpplibs.mtest.engine.model;

import std;
import mcpplibs.os;

namespace mcpplibs::mtest::engine {

namespace os = mcpplibs::os;

namespace {
template <class T>
std::vector<T> strings(const json& j, const char* key) {
    std::vector<T> out;
    if (auto it = j.find(key); it != j.end() && it->is_array())
        for (const auto& v : *it)
            if (v.is_string())
                out.push_back(v.get<std::string>());
    return out;
}
std::string str(const json& j, const char* key) {
    auto it = j.find(key);
    return it != j.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
}  // namespace

std::string dumped(const json& j, int indent) {
    return j.dump(indent, ' ', false, json::error_handler_t::replace);
}

json to_json(const std::vector<bound_info>& bounds) {
    json out = json::array();
    for (const auto& b : bounds) {
        json one = {{"metric", b.metric}};
        if (b.max)
            one["max"] = *b.max;
        if (b.min)
            one["min"] = *b.min;
        out.push_back(std::move(one));
    }
    return out;
}

std::vector<bound_info> bounds_from(const json& j) {
    std::vector<bound_info> out;
    if (auto it = j.find("bounds"); it != j.end() && it->is_array())
        for (const auto& b : *it) {
            if (!b.is_object())
                continue;
            bound_info one{b.value("metric", std::string{}), std::nullopt, std::nullopt};
            if (auto m = b.find("max"); m != b.end() && m->is_number())
                one.max = m->get<double>();
            if (auto m = b.find("min"); m != b.end() && m->is_number())
                one.min = m->get<double>();
            out.push_back(std::move(one));
        }
    return out;
}

json to_json(const case_info& c) {
    return {{"id", c.id},           {"name", c.name},           {"file", c.file},
            {"line", c.line},       {"kind", c.kind},           {"tags", c.tags},
            {"clauses", c.clauses}, {"resources", c.resources}, {"artifacts", c.artifacts},
            {"expect", c.expect},   {"timeout", c.timeout},     {"isolated", c.isolated},
            {"retries", c.retries}, {"uses", c.uses},           {"bounds", to_json(c.bounds)},
            {"only", c.only},       {"except", c.except},       {"capabilities", c.capabilities},
            {"reason", c.reason}};
}
json to_json(const failure_info& f) {
    json j = {{"file", f.file}, {"line", f.line}, {"message", f.message}};
    if (!f.expected.empty() || !f.actual.empty()) {
        j["expected"] = f.expected;
        j["actual"] = f.actual;
    }
    if (f.fatal)
        j["fatal"] = true;
    return j;
}
json to_json(const metric_info& m) {
    json j = {{"name", m.name},
              {"value", m.value},
              {"unit", m.unit},
              {"samples", m.samples},
              {"better", m.better}};
    if (!m.precision.empty())
        j["precision"] = m.precision;
    return j;
}
json to_json(const case_result& c) {
    json j = {{"program", c.program},
              {"id", c.id},
              {"kind", c.kind},
              {"status", c.status},
              {"ms", c.ms}};
    if (!c.reason.empty())
        j["reason"] = c.reason;
    if (!c.failures.empty()) {
        j["failures"] = json::array();
        for (auto& f : c.failures)
            j["failures"].push_back(to_json(f));
    }
    if (!c.metrics.empty()) {
        j["metrics"] = json::array();
        for (auto& m : c.metrics)
            j["metrics"].push_back(to_json(m));
    }
    if (!c.artifacts.empty())
        j["artifacts"] = c.artifacts;
    if (!c.tags.empty())
        j["tags"] = c.tags;
    if (!c.only.empty())
        j["only"] = c.only;
    if (!c.except.empty())
        j["except"] = c.except;
    if (!c.clauses.empty())
        j["clauses"] = c.clauses;
    if (c.quarantined)
        j["quarantined"] = true;
    if (!c.fixtures.empty()) {
        j["fixtures"] = json::array();
        for (const auto& f : c.fixtures)
            j["fixtures"].push_back({{"name", f.name}, {"ms", f.ms}});
    }
    if (!c.diagnosis.empty())
        j["diagnosis"] = c.diagnosis;
    if (!c.bounds.empty())
        j["bounds"] = to_json(c.bounds);
    if (c.attempts > 1)
        j["attempts"] = c.attempts;
    if (c.started_ms >= 0)
        j["started_ms"] = c.started_ms;
    if (c.cpu_ms >= 0)
        j["cpu_ms"] = c.cpu_ms;
    if (!c.steps.empty()) {
        j["steps"] = json::array();
        for (const auto& st : c.steps) {
            json one = {{"name", st.name}, {"t", st.t}, {"ms", st.ms}};
            if (st.cpu_ms)
                one["cpu_ms"] = *st.cpu_ms;
            if (st.status)
                one["status"] = *st.status;
            if (!st.build.empty())
                one["build"] = st.build;
            j["steps"].push_back(std::move(one));
        }
    }
    if (!c.exercised.empty())
        j["exercised"] = c.exercised;
    if (c.limit_ms >= 0)
        j["limit_ms"] = c.limit_ms;
    return j;
}

build_edge build_edge_from(const json& j) {
    build_edge b;
    b.build = str(j, "build");
    b.output = str(j, "output");
    b.package = str(j, "package");
    b.program = str(j, "program");
    b.started_ms = j.value("started_ms", 0.0);
    b.ms = j.value("ms", 0.0);
    if (auto it = j.find("cpu_ms"); it != j.end() && it->is_number())
        b.cpu_ms = it->get<double>();
    if (auto it = j.find("peak_bytes"); it != j.end() && it->is_number())
        b.peak_bytes = it->get<double>();
    b.cached = j.value("cached", false);
    b.units = j.value("units", 0);
    return b;
}

namespace {

// A process as the timeout recorded it; an empty field is one the system did not tell.
json to_json(const process_info& p) {
    json j = {{"id", p.id},           {"parent", p.parent}, {"group", p.group},  {"name", p.name},
              {"command", p.command}, {"state", p.state},   {"cpu_ms", p.cpu_ms}};
    if (!p.waiting.empty())
        j["waiting"] = p.waiting;
    if (!p.request.empty())
        j["request"] = p.request;
    if (!p.threads.empty()) {
        j["threads"] = json::array();
        for (const auto& t : p.threads) {
            json one = {{"id", t.id}, {"name", t.name}, {"state", t.state}, {"cpu_ms", t.cpu_ms}};
            if (!t.waiting.empty())
                one["waiting"] = t.waiting;
            j["threads"].push_back(std::move(one));
        }
    }
    if (!p.stack.empty())
        j["stack"] = p.stack;
    return j;
}

std::vector<process_info> processes_from(const json& j, const char* key) {
    std::vector<process_info> out;
    auto it = j.find(key);
    if (it == j.end() || !it->is_array())
        return out;
    for (const auto& e : *it) {
        process_info p;
        p.id = e.value("id", std::uint64_t{0});
        p.parent = e.value("parent", std::uint64_t{0});
        p.group = e.value("group", std::uint64_t{0});
        p.name = str(e, "name");
        p.command = strings<std::string>(e, "command");
        p.state = str(e, "state");
        p.waiting = str(e, "waiting");
        p.request = str(e, "request");
        p.cpu_ms = e.value("cpu_ms", 0.0);
        p.stack = str(e, "stack");
        if (auto t = e.find("threads"); t != e.end() && t->is_array())
            for (const auto& x : *t)
                p.threads.push_back({x.value("id", std::uint64_t{0}), str(x, "name"),
                                     str(x, "state"), str(x, "waiting"), x.value("cpu_ms", 0.0)});
        out.push_back(std::move(p));
    }
    return out;
}

}  // namespace

json to_json(const timeout_info& t) {
    json j = {{"limit_ms", t.limit_ms}, {"grace_ms", t.grace_ms}, {"ended", t.ended}};
    if (!t.seen.empty()) {
        j["seen"] = t.seen;
        j["processes"] = json::array();
        for (const auto& p : t.processes)
            j["processes"].push_back(to_json(p));
        j["remaining"] = json::array();
        for (const auto& p : t.remaining)
            j["remaining"].push_back(to_json(p));
    }
    return j;
}

timeout_info timeout_info_from(const json& j) {
    timeout_info t;
    t.limit_ms = j.value("limit_ms", 0.0);
    t.grace_ms = j.value("grace_ms", 0.0);
    t.ended = str(j, "ended");
    t.seen = str(j, "seen");
    t.processes = processes_from(j, "processes");
    t.remaining = processes_from(j, "remaining");
    return t;
}

json to_json(const program_result& p) {
    json j = {{"name", p.name},     {"argv", p.argv}, {"cwd", p.cwd}, {"level", p.level},
              {"status", p.status}, {"exit", p.exit}, {"ms", p.ms}};
    if (p.timed_out)
        j["timed_out"] = true;
    if (p.timeout)
        j["timeout"] = to_json(*p.timeout);
    if (!p.out_tail.empty())
        j["out_tail"] = p.out_tail;
    if (!p.err_tail.empty())
        j["err_tail"] = p.err_tail;
    if (!p.error.empty())
        j["error"] = p.error;
    j["origin"] = p.origin;
    j["started_ms"] = p.started_ms;
    if (!p.resources.empty())
        j["resources"] = p.resources;
    if (p.worker >= 0)
        j["worker"] = p.worker;
    if (!p.select.empty())
        j["select"] = p.select;
    if (!p.fixtures.empty())
        j["fixtures"] = p.fixtures;
    if (p.build_started_ms >= 0) {
        j["build_started_ms"] = p.build_started_ms;
        j["build_ms"] = p.build_ms;
    }
    if (p.usage) {
        json u = {{"user_ms", p.usage->user_ms},
                  {"sys_ms", p.usage->sys_ms},
                  {"source", p.usage->source}};
        if (p.usage->peak_bytes)
            u["peak_bytes"] = *p.usage->peak_bytes;
        if (p.usage->read_bytes)
            u["read_bytes"] = *p.usage->read_bytes;
        if (p.usage->written_bytes)
            u["written_bytes"] = *p.usage->written_bytes;
        j["usage"] = u;
    }
    return j;
}

json to_json(const fixture_run& f) {
    return {{"name", f.name},         {"program", f.program},   {"started_ms", f.started_ms},
            {"ready_ms", f.ready_ms}, {"ended_ms", f.ended_ms}, {"status", f.status}};
}

fixture_run fixture_run_from(const json& j) {
    return {str(j, "name"),           str(j, "program"),        j.value("started_ms", 0.0),
            j.value("ready_ms", 0.0), j.value("ended_ms", 0.0), str(j, "status")};
}
std::string machine_class() {
    if (auto model = os::machine::processor_model(); model && !model->empty())
        return *model;
    return std::format("{}/{}", os::machine::arch(), os::machine::processors().logical);
}

json to_json(const run_record& r) {
    return {{"schema", "mtest.run/1"},
            {"id", r.id},
            {"processors", r.processors},
            {"memory", r.memory},
            {"budget", r.budget},
            {"jitter",
             r.jitter_ms > 0 ? json{{"max_ms", r.jitter_ms}, {"seed", r.jitter_seed}} : json()},
            {"started", r.started},
            {"finished", r.finished},
            {"status", r.status},
            {"system", r.system},
            {"arch", r.arch},
            {"machine", r.machine},
            {"commit", r.commit},
            {"set", r.set},
            {"selection", r.selection},
            {"ms", r.ms},
            {"jobs", r.jobs},
            {"counts", r.counts},
            {"group", r.group},
            {"monitor", r.monitor},
            {"host", r.host},
            {"capabilities", r.capabilities},
            {"resumed", r.resumed},
            {"programs", r.programs.size()},
            {"cases", r.cases.size()}};
}

case_info case_info_from(const json& j) {
    case_info c;
    c.id = str(j, "id");
    c.name = str(j, "name");
    c.file = str(j, "file");
    c.kind = j.value("kind", std::string{});
    c.expect = str(j, "expect");
    c.timeout = j.value("timeout", 0.0);
    c.isolated = j.value("isolation", std::string{}) == "case" || j.value("isolated", false);
    c.retries = j.value("retries", 0);
    c.bounds = bounds_from(j);
    if (auto d = j.find("diagnose"); d != j.end() && d->is_object()) {
        std::vector<std::pair<std::string, std::string>> env;
        if (auto e = d->find("env"); e != d->end() && e->is_object())
            for (auto it = e->begin(); it != e->end(); ++it)
                if (it.value().is_string())
                    env.emplace_back(it.key(), it.value().get<std::string>());
        c.diagnose_env = std::move(env);
        c.diagnose_timeout = d->value("timeout", 0.0);
    }
    c.line = j.value("line", 0);
    c.tags = strings<std::string>(j, "tags");
    c.clauses = strings<std::string>(j, "clauses");
    c.resources = strings<std::string>(j, "resources");
    c.artifacts = strings<std::string>(j, "artifacts");
    c.uses = strings<std::string>(j, "uses");
    c.only = strings<std::string>(j, "only");
    c.except = strings<std::string>(j, "except");
    if (auto a = j.find("capabilities"); a != j.end() && a->is_object())
        for (auto it = a->begin(); it != a->end(); ++it)
            if (it.value().is_boolean())
                c.capabilities[it.key()] = it.value().get<bool>();
    c.reason = str(j, "reason");
    return c;
}
step_info step_from(const json& j) {
    step_info st;
    st.name = str(j, "name");
    st.t = j.value("t", 0.0);
    st.ms = j.value("ms", 0.0);
    if (auto it = j.find("cpu_ms"); it != j.end() && it->is_number())
        st.cpu_ms = it->get<double>();
    if (auto it = j.find("status"); it != j.end() && it->is_number_integer())
        st.status = it->get<int>();
    st.build = str(j, "build");
    return st;
}
case_result case_result_from(const json& j) {
    case_result c;
    c.program = str(j, "program");
    c.id = str(j, "id");
    c.kind = j.value("kind", std::string("unit"));
    c.status = str(j, "status");
    c.ms = j.value("ms", 0.0);
    c.reason = str(j, "reason");
    if (auto it = j.find("failures"); it != j.end() && it->is_array())
        for (const auto& f : *it)
            c.failures.push_back({str(f, "file"), str(f, "message"), str(f, "expected"),
                                  str(f, "actual"), f.value("line", 0), f.value("fatal", false)});
    if (auto it = j.find("metrics"); it != j.end() && it->is_array())
        for (const auto& m : *it) {
            metric_info mi;
            mi.name = str(m, "name");
            mi.unit = str(m, "unit");
            mi.better = m.value("better", std::string("lower"));
            mi.value = m.value("value", 0.0);
            mi.precision = m.value("precision", std::string{});
            if (auto s = m.find("samples"); s != m.end() && s->is_array())
                for (const auto& x : *s)
                    if (x.is_number())
                        mi.samples.push_back(x.get<double>());
            c.metrics.push_back(std::move(mi));
        }
    c.artifacts = strings<std::string>(j, "artifacts");
    c.tags = strings<std::string>(j, "tags");
    c.clauses = strings<std::string>(j, "clauses");
    c.only = strings<std::string>(j, "only");
    c.except = strings<std::string>(j, "except");
    c.quarantined = j.value("quarantined", false);
    c.attempts = j.value("attempts", 1);
    if (auto it = j.find("fixtures"); it != j.end() && it->is_array())
        for (const auto& f : *it)
            c.fixtures.push_back({str(f, "name"), f.value("ms", 0.0)});
    c.diagnosis = str(j, "diagnosis");
    c.bounds = bounds_from(j);
    c.cpu_ms = j.value("cpu_ms", -1.0);
    if (auto it = j.find("steps"); it != j.end() && it->is_array())
        for (const auto& st : *it)
            c.steps.push_back(step_from(st));
    c.exercised = strings<std::string>(j, "exercised");
    c.limit_ms = j.value("limit_ms", -1.0);
    c.started_ms = j.value("started_ms", -1.0);
    return c;
}
program_result program_result_from(const json& j) {
    program_result p;
    p.name = str(j, "name");
    p.argv = strings<std::string>(j, "argv");
    p.cwd = str(j, "cwd");
    p.level = j.value("level", 0);
    p.status = str(j, "status");
    p.exit = j.value("exit", 0);
    p.ms = j.value("ms", 0.0);
    p.timed_out = j.value("timed_out", false);
    if (auto t = j.find("timeout"); t != j.end() && t->is_object())
        p.timeout = timeout_info_from(*t);
    p.out_tail = str(j, "out_tail");
    p.err_tail = str(j, "err_tail");
    p.error = str(j, "error");
    p.origin = j.value("origin", std::string("config"));
    p.started_ms = j.value("started_ms", 0.0);
    p.resources = strings<std::string>(j, "resources");
    p.worker = j.value("worker", -1);
    p.select = strings<std::string>(j, "select");
    p.fixtures = strings<std::string>(j, "fixtures");
    p.build_started_ms = j.value("build_started_ms", -1.0);
    p.build_ms = j.value("build_ms", 0.0);
    if (auto u = j.find("usage"); u != j.end() && u->is_object()) {
        usage_info ui;
        ui.user_ms = u->value("user_ms", 0.0);
        ui.sys_ms = u->value("sys_ms", 0.0);
        ui.source = u->value("source", std::string{});
        if (u->contains("peak_bytes"))
            ui.peak_bytes = (*u)["peak_bytes"].get<double>();
        if (u->contains("read_bytes"))
            ui.read_bytes = (*u)["read_bytes"].get<double>();
        if (u->contains("written_bytes"))
            ui.written_bytes = (*u)["written_bytes"].get<double>();
        p.usage = ui;
    }
    return p;
}
run_record run_record_from(const json& j) {
    run_record r;
    r.id = str(j, "id");
    r.started = str(j, "started");
    r.finished = str(j, "finished");
    r.status = str(j, "status");
    r.system = str(j, "system");
    r.arch = str(j, "arch");
    r.machine = str(j, "machine");
    r.processors = j.value("processors", 0);
    r.memory = j.value("memory", std::uint64_t{0});
    r.budget = j.value("budget", 0);
    if (auto jt = j.find("jitter"); jt != j.end() && jt->is_object()) {
        r.jitter_ms = jt->value("max_ms", 0);
        r.jitter_seed = jt->value("seed", std::uint64_t{0});
    }
    r.commit = str(j, "commit");
    r.set = str(j, "set");
    r.selection = str(j, "selection");
    r.ms = j.value("ms", 0.0);
    r.jobs = j.value("jobs", 1);
    r.group = str(j, "group");
    r.monitor = j.value("monitor", std::string("basic"));
    r.host = str(j, "host");
    if (auto c = j.find("capabilities"); c != j.end() && c->is_object())
        for (auto it = c->begin(); it != c->end(); ++it)
            if (it.value().is_boolean())
                r.capabilities[it.key()] = it.value().get<bool>();
    r.resumed = str(j, "resumed");
    if (auto it = j.find("counts"); it != j.end() && it->is_object())
        for (auto& item_ : it->items())
            if (auto& k = item_.key(); true)
                if (auto& v = item_.value(); true)
                    if (v.is_number_integer())
                        r.counts[k] = v.get<int>();
    return r;
}

bool failing(std::string_view status) {
    return status == "fail" || status == "xpass" || status == "timeout";
}

std::map<std::string, int> count(const std::vector<case_result>& cases) {
    std::map<std::string, int> out;
    for (const auto& c : cases)
        out[c.status]++;
    return out;
}

std::string now_iso() {
    const auto now =
        std::chrono::floor<std::chrono::milliseconds>(std::chrono::system_clock::now());
    return std::format("{:%Y-%m-%dT%H:%M:%S}Z", now);
}

std::string new_run_id() {
    const auto now = std::chrono::floor<std::chrono::seconds>(std::chrono::system_clock::now());
    return std::format("{:%Y%m%dT%H%M%S}-{:04x}", now, std::random_device{}() & 0xffff);
}

}  // namespace mcpplibs::mtest::engine
