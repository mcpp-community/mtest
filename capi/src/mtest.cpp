// The C ABI of the mtest protocol (include/mtest.h): what a program of another language links to
// report by the protocol. The case list a program declares is kept to answer `missing` (P4.1).
#include "../include/mtest.h"

import std;
import nlohmann.json;

extern "C" const char mtest_protocol_marker[] = "mtest-protocol=1";

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// An event of the report (P5): an object whose fields keep the order they are written in.
using event = nlohmann::ordered_json;

std::string text(const char* value) {
    return value ? value : "";
}

struct pending_case {
    event body;
    std::vector<std::string> tags, clauses, resources, artifacts, only, except;
    bool expect_fail = false;
    double timeout = 0;
    bool isolated = false;
    int retries = 0;
    std::optional<event> diagnose;
};

struct state {
    std::mutex lock;
    std::ofstream out;
    bool on = false, listing = false;
    std::set<std::string> selection;
    bool selecting = false;
    std::set<std::string> seen;
    std::map<std::string, int> counts;
    bool failed = false;
    std::chrono::steady_clock::time_point began = std::chrono::steady_clock::now();
    std::map<std::string, std::chrono::steady_clock::time_point> started;
    std::vector<pending_case> pending;
    // P6.5: the bounds a case states, and the metrics it reported, by case.
    std::map<std::string, event> bounds;
    std::map<std::string, std::map<std::string, double>> metrics;

    state() {
        const auto report = env("MTEST_REPORT");
        on = !env("MTEST_PROTOCOL").empty() && !report.empty();
        if (!on)
            return;
        out.open(report, std::ios::app | std::ios::binary);
        listing = env("MTEST_MODE") == "list";
        if (const auto select = env("MTEST_SELECT"); !select.empty()) {
            selecting = true;
            std::ifstream in(select);
            for (std::string line; std::getline(in, line);) {
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (!line.empty())
                    selection.insert(line);
            }
        }
    }
    void write(const event& e) {
        if (!on || !out)
            return;
        // Text that is not UTF-8 is written with the replacement character.
        out << e.dump(-1, ' ', false, event::error_handler_t::replace) << "\n";
        out.flush();
    }
    void flush_pending() {
        for (auto& p : pending) {
            p.body["tags"] = p.tags;
            p.body["clauses"] = p.clauses;
            p.body["resources"] = p.resources;
            p.body["artifacts"] = p.artifacts;
            if (!p.only.empty())
                p.body["only"] = p.only;
            if (!p.except.empty())
                p.body["except"] = p.except;
            if (p.expect_fail)
                p.body["expect"] = "fail";
            if (p.timeout > 0)
                p.body["timeout"] = p.timeout;
            if (p.isolated)
                p.body["isolation"] = "case";
            if (p.retries > 0)
                p.body["retries"] = p.retries;
            if (p.diagnose)
                p.body["diagnose"] = *p.diagnose;
            write(p.body);
        }
        pending.clear();
    }
};

state& the() {
    static state s;
    return s;
}

double ms_since(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
}

}  // namespace

extern "C" {

void mtest_init(const char* framework, int level) {
    auto& s = the();
    std::lock_guard g(s.lock);
    const char* volatile keep = mtest_protocol_marker;
    (void)keep;
    s.write({{"e", "program"},
             {"protocol", 1},
             {"level", std::clamp(level, 1, 3)},
             {"framework", text(framework)},
             {"version", "0.8.3"},
             {"isolation", {"process", "case"}}});
}

int mtest_listing(void) {
    return the().listing ? 1 : 0;
}

void mtest_case(const char* id, const char* file, int line, const char* kind) {
    auto& s = the();
    std::lock_guard g(s.lock);
    s.flush_pending();
    s.seen.insert(id ? id : "");
    event body = {{"e", "case"},
                  {"id", text(id)},
                  {"name", text(id)},
                  {"file", text(file)},
                  {"line", line}};
    // A kind not stated is the runner's to give (the program's, by its directory).
    if (kind)
        body["kind"] = kind;
    s.pending.push_back({std::move(body)});
}

namespace {
void attribute(std::vector<std::string> pending_case::* field, const char* value) {
    auto& s = the();
    std::lock_guard g(s.lock);
    if (!s.pending.empty())
        (s.pending.back().*field).push_back(value ? value : "");
}
}  // namespace

void mtest_case_tag(const char* value) {
    attribute(&pending_case::tags, value);
}
void mtest_case_clause(const char* value) {
    attribute(&pending_case::clauses, value);
}
void mtest_case_resource(const char* value) {
    attribute(&pending_case::resources, value);
}
void mtest_case_artifact(const char* value) {
    attribute(&pending_case::artifacts, value);
}
void mtest_case_only(const char* value) {
    attribute(&pending_case::only, value);
}
void mtest_case_except(const char* value) {
    attribute(&pending_case::except, value);
}

void mtest_case_timeout(double seconds) {
    auto& s = the();
    std::lock_guard g(s.lock);
    if (!s.pending.empty())
        s.pending.back().timeout = seconds;
}

void mtest_case_isolate(void) {
    auto& s = the();
    std::lock_guard g(s.lock);
    if (!s.pending.empty())
        s.pending.back().isolated = true;
}

void mtest_case_retries(int count) {
    auto& s = the();
    std::lock_guard g(s.lock);
    if (!s.pending.empty())
        s.pending.back().retries = count;
}

void mtest_case_diagnose(const char* name, const char* value, double seconds) {
    auto& s = the();
    std::lock_guard g(s.lock);
    if (s.pending.empty())
        return;
    auto& d = s.pending.back().diagnose;
    if (!d)
        d = event{{"env", event::object()}};
    if (name)
        (*d)["env"][name] = text(value);
    if (seconds > 0)
        (*d)["timeout"] = seconds;
}

void mtest_case_expect_fail(void) {
    auto& s = the();
    std::lock_guard g(s.lock);
    if (!s.pending.empty())
        s.pending.back().expect_fail = true;
}

int mtest_selected(const char* id) {
    auto& s = the();
    std::lock_guard g(s.lock);
    s.seen.insert(id ? id : "");
    return !s.selecting || s.selection.contains(id ? id : "") ? 1 : 0;
}

void mtest_begin(const char* id) {
    auto& s = the();
    std::lock_guard g(s.lock);
    s.started[id ? id : ""] = std::chrono::steady_clock::now();
    s.write({{"e", "begin"}, {"id", text(id)}, {"t", ms_since(s.began)}});
}

void mtest_failure(const char* id, const char* file, int line, const char* message,
                   const char* expected, const char* actual) {
    auto& s = the();
    std::lock_guard g(s.lock);
    event e = {{"e", "failure"},
               {"id", text(id)},
               {"file", text(file)},
               {"line", line},
               {"message", text(message)}};
    if (expected || actual) {
        e["expected"] = text(expected);
        e["actual"] = text(actual);
    }
    s.write(e);
}

void mtest_metric(const char* id, const char* name, double value, const char* unit,
                  const double* samples, int count) {
    auto& s = the();
    std::lock_guard g(s.lock);
    event e = {{"e", "metric"},
               {"id", text(id)},
               {"name", text(name)},
               {"value", value},
               {"unit", text(unit)}};
    if (samples && count > 0)
        e["samples"] = std::vector<double>(samples, samples + count);
    s.metrics[text(id)][text(name)] = value;
    s.write(e);
}

void mtest_bound(const char* id, const char* metric, double min, double max) {
    auto& s = the();
    std::lock_guard g(s.lock);
    event b = {{"metric", text(metric)}};
    if (!std::isnan(max))
        b["max"] = max;
    if (!std::isnan(min))
        b["min"] = min;
    auto& list = s.bounds[text(id)];
    if (!list.is_array())
        list = event::array();
    list.push_back(b);
    for (auto& p : s.pending)
        if (p.body.value("id", std::string{}) == text(id))
            p.body["bounds"] = list;
}

void mtest_end(const char* id, const char* status, const char* reason) {
    auto& s = the();
    std::lock_guard g(s.lock);
    std::string st = status ? status : "fail";
    // P6.5: a bound the case's metrics do not keep is a failure of it.
    if (auto b = s.bounds.find(text(id)); b != s.bounds.end() && (st == "pass" || st == "xfail")) {
        const auto& reported = s.metrics[text(id)];
        for (const auto& one : b->second) {
            const auto name = one.value("metric", std::string{});
            const auto it = reported.find(name);
            std::string message;
            if (it == reported.end())
                message = std::format("the case reported no metric `{}`, which it bounds", name);
            else if (one.contains("max") && it->second > one["max"].get<double>())
                message = std::format("metric {} = {:g}, over its bound {:g} (at_most)", name,
                                      it->second, one["max"].get<double>());
            else if (one.contains("min") && it->second < one["min"].get<double>())
                message = std::format("metric {} = {:g}, under its bound {:g} (at_least)", name,
                                      it->second, one["min"].get<double>());
            if (message.empty())
                continue;
            s.write({{"e", "failure"},
                     {"id", text(id)},
                     {"file", ""},
                     {"line", 0},
                     {"message", message}});
            if (st == "pass")
                st = "fail";
        }
    }
    if (st == "fail" || st == "xpass")
        s.failed = true;
    s.counts[st]++;
    double ms = 0;
    if (auto it = s.started.find(id ? id : ""); it != s.started.end())
        ms = ms_since(it->second);
    event e = {{"e", "end"}, {"id", text(id)}, {"status", st}, {"ms", ms}};
    if (reason)
        e["reason"] = reason;
    s.write(e);
}

int mtest_done(void) {
    auto& s = the();
    std::lock_guard g(s.lock);
    s.flush_pending();
    bool missing = false;
    if (!s.listing && s.selecting)
        for (const auto& want : s.selection)
            if (!s.seen.contains(want)) {
                s.write({{"e", "missing"}, {"id", want}});
                missing = true;
            }
    const bool failed = s.failed || missing;
    s.write({{"e", "done"}, {"status", failed ? "fail" : "pass"}, {"counts", s.counts}});
    return s.listing ? 0 : (failed ? 1 : 0);
}

}  // extern "C"
