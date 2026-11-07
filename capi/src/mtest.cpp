// The C ABI of the mtest protocol (include/mtest.h): what a program of another language links to
// report by the protocol. The case list a program declares is kept to answer `missing` (P4.1).
#include "../include/mtest.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>

extern "C" const char mtest_protocol_marker[] = "mtest-protocol=1";

namespace {

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

std::string json_string(const char* text) {
    std::string out = "\"";
    for (const unsigned char* p = reinterpret_cast<const unsigned char*>(text ? text : ""); *p;
         ++p) {
        switch (*p) {
            case '"':
                out += "\\\"";
                break;
            case '\\':
                out += "\\\\";
                break;
            case '\n':
                out += "\\n";
                break;
            case '\r':
                out += "\\r";
                break;
            case '\t':
                out += "\\t";
                break;
            default:
                if (*p < 0x20) {
                    char buf[8];
                    std::snprintf(buf, sizeof buf, "\\u%04x", *p);
                    out += buf;
                } else {
                    out += static_cast<char>(*p);
                }
        }
    }
    return out + "\"";
}

struct pending_case {
    std::string body;
    std::vector<std::string> tags, clauses, resources, artifacts;
    bool expect_fail = false;
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
    void event(const std::string& body) {
        if (!on || !out)
            return;
        out << "{" << body << "}\n";
        out.flush();
    }
    void flush_pending() {
        for (auto& p : pending) {
            auto list = [](const std::vector<std::string>& v) {
                std::string s = "[";
                for (std::size_t i = 0; i < v.size(); ++i)
                    s += (i ? "," : "") + json_string(v[i].c_str());
                return s + "]";
            };
            event(p.body + ",\"tags\":" + list(p.tags) + ",\"clauses\":" + list(p.clauses) +
                  ",\"resources\":" + list(p.resources) + ",\"artifacts\":" + list(p.artifacts) +
                  (p.expect_fail ? ",\"expect\":\"fail\"" : ""));
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
    [[maybe_unused]] volatile char keep = mtest_protocol_marker[0];
    s.event("\"e\":\"program\",\"protocol\":1,\"level\":" +
            std::to_string(level < 1   ? 1
                           : level > 3 ? 3
                                       : level) +
            ",\"framework\":" + json_string(framework) +
            ",\"version\":\"0.1.0\",\"isolation\":[\"process\",\"case\"]");
}

int mtest_listing(void) {
    return the().listing ? 1 : 0;
}

void mtest_case(const char* id, const char* file, int line, const char* kind) {
    auto& s = the();
    std::lock_guard g(s.lock);
    s.flush_pending();
    s.seen.insert(id ? id : "");
    s.pending.push_back({"\"e\":\"case\",\"id\":" + json_string(id) +
                         ",\"name\":" + json_string(id) + ",\"file\":" + json_string(file) +
                         ",\"line\":" + std::to_string(line) +
                         ",\"kind\":" + json_string(kind ? kind : "unit")});
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
    s.event("\"e\":\"begin\",\"id\":" + json_string(id) +
            ",\"t\":" + std::to_string(ms_since(s.began)));
}

void mtest_failure(const char* id, const char* file, int line, const char* message,
                   const char* expected, const char* actual) {
    auto& s = the();
    std::lock_guard g(s.lock);
    std::string body = "\"e\":\"failure\",\"id\":" + json_string(id) +
                       ",\"file\":" + json_string(file) + ",\"line\":" + std::to_string(line) +
                       ",\"message\":" + json_string(message);
    if (expected || actual)
        body += ",\"expected\":" + json_string(expected) + ",\"actual\":" + json_string(actual);
    s.event(body);
}

void mtest_metric(const char* id, const char* name, double value, const char* unit,
                  const double* samples, int count) {
    auto& s = the();
    std::lock_guard g(s.lock);
    std::string body = "\"e\":\"metric\",\"id\":" + json_string(id) +
                       ",\"name\":" + json_string(name) + ",\"value\":" + std::to_string(value) +
                       ",\"unit\":" + json_string(unit);
    if (samples && count > 0) {
        body += ",\"samples\":[";
        for (int i = 0; i < count; ++i)
            body += (i ? "," : "") + std::to_string(samples[i]);
        body += "]";
    }
    s.event(body);
}

void mtest_end(const char* id, const char* status, const char* reason) {
    auto& s = the();
    std::lock_guard g(s.lock);
    const std::string st = status ? status : "fail";
    if (st == "fail" || st == "xpass")
        s.failed = true;
    s.counts[st]++;
    double ms = 0;
    if (auto it = s.started.find(id ? id : ""); it != s.started.end())
        ms = ms_since(it->second);
    std::string body = "\"e\":\"end\",\"id\":" + json_string(id) +
                       ",\"status\":" + json_string(st.c_str()) + ",\"ms\":" + std::to_string(ms);
    if (reason)
        body += ",\"reason\":" + json_string(reason);
    s.event(body);
}

int mtest_done(void) {
    auto& s = the();
    std::lock_guard g(s.lock);
    s.flush_pending();
    bool missing = false;
    if (!s.listing && s.selecting)
        for (const auto& want : s.selection)
            if (!s.seen.contains(want)) {
                s.event("\"e\":\"missing\",\"id\":" + json_string(want.c_str()));
                missing = true;
            }
    std::string counts = "{";
    for (const auto& [k, v] : s.counts)
        counts += (counts.size() > 1 ? "," : "") + json_string(k.c_str()) + ":" + std::to_string(v);
    const bool failed = s.failed || missing;
    s.event(std::string("\"e\":\"done\",\"status\":\"") + (failed ? "fail" : "pass") +
            "\",\"counts\":" + counts + "}");
    return s.listing ? 0 : (failed ? 1 : 0);
}

}  // extern "C"
