module mcpplibs.mtest.engine.model;

import std;

namespace mcpplibs::mtest::engine {

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

json to_json(const case_info& c) {
    return {{"id", c.id},           {"name", c.name},           {"file", c.file},
            {"line", c.line},       {"kind", c.kind},           {"tags", c.tags},
            {"clauses", c.clauses}, {"resources", c.resources}, {"artifacts", c.artifacts},
            {"expect", c.expect}};
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
    return {{"name", m.name},
            {"value", m.value},
            {"unit", m.unit},
            {"samples", m.samples},
            {"better", m.better}};
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
    if (!c.clauses.empty())
        j["clauses"] = c.clauses;
    if (c.quarantined)
        j["quarantined"] = true;
    if (c.attempts > 1)
        j["attempts"] = c.attempts;
    return j;
}
json to_json(const program_result& p) {
    json j = {{"name", p.name},     {"argv", p.argv}, {"cwd", p.cwd}, {"level", p.level},
              {"status", p.status}, {"exit", p.exit}, {"ms", p.ms}};
    if (p.timed_out)
        j["timed_out"] = true;
    if (!p.out_tail.empty())
        j["out_tail"] = p.out_tail;
    if (!p.err_tail.empty())
        j["err_tail"] = p.err_tail;
    if (!p.error.empty())
        j["error"] = p.error;
    j["origin"] = p.origin;
    return j;
}
json to_json(const run_record& r) {
    return {{"schema", "mtest.run/1"},
            {"id", r.id},
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
            {"programs", r.programs.size()},
            {"cases", r.cases.size()}};
}

case_info case_info_from(const json& j) {
    case_info c;
    c.id = str(j, "id");
    c.name = str(j, "name");
    c.file = str(j, "file");
    c.kind = j.value("kind", std::string("unit"));
    c.expect = str(j, "expect");
    c.line = j.value("line", 0);
    c.tags = strings<std::string>(j, "tags");
    c.clauses = strings<std::string>(j, "clauses");
    c.resources = strings<std::string>(j, "resources");
    c.artifacts = strings<std::string>(j, "artifacts");
    return c;
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
            if (auto s = m.find("samples"); s != m.end() && s->is_array())
                for (const auto& x : *s)
                    if (x.is_number())
                        mi.samples.push_back(x.get<double>());
            c.metrics.push_back(std::move(mi));
        }
    c.artifacts = strings<std::string>(j, "artifacts");
    c.tags = strings<std::string>(j, "tags");
    c.clauses = strings<std::string>(j, "clauses");
    c.quarantined = j.value("quarantined", false);
    c.attempts = j.value("attempts", 1);
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
    p.out_tail = str(j, "out_tail");
    p.err_tail = str(j, "err_tail");
    p.error = str(j, "error");
    p.origin = j.value("origin", std::string("config"));
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
    r.commit = str(j, "commit");
    r.set = str(j, "set");
    r.selection = str(j, "selection");
    r.ms = j.value("ms", 0.0);
    r.jobs = j.value("jobs", 1);
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
