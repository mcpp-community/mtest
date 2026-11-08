// GoogleTest under the mtest protocol (spec/protocol.md, level 1).
//
// With no runner it is GoogleTest's own main. Under one (MTEST_PROTOCOL and MTEST_REPORT), `list`
// mode writes the registered tests as cases without running any (P3); a selection (MTEST_SELECT)
// becomes GoogleTest's filter, and an identifier no test has is `missing` (P4.1); a listener writes
// each test's begin, failures and end (P5).
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <gtest/gtest.h>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

// P2.3: a runner that reads this program's file finds the protocol here.
extern "C" const char mcpplibs_mtest_gtest_marker[] = "mtest-protocol=1";

namespace {

// An event of the report (P5): an object whose fields keep the order they are written in.
using event = nlohmann::ordered_json;

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

class Report {
public:
    explicit Report(const std::string& path) : out_(path, std::ios::app | std::ios::binary) {}
    void write(const event& e) {
        if (!out_)
            return;
        // Text that is not UTF-8 is written with the replacement character.
        out_ << e.dump(-1, ' ', false, event::error_handler_t::replace) << "\n";
        out_.flush();
    }
private:
    std::ofstream out_;
};

std::string id_of(const testing::TestInfo& t) {
    return std::string(t.test_suite_name()) + "/" + t.name();
}

class Listener : public testing::EmptyTestEventListener {
public:
    explicit Listener(Report& r) : report_(r), began_(std::chrono::steady_clock::now()) {}
    void OnTestStart(const testing::TestInfo& t) override {
        started_ = std::chrono::steady_clock::now();
        current_ = id_of(t);
        report_.write({{"e", "begin"}, {"id", current_}, {"t", ms(began_)}});
    }
    void OnTestPartResult(const testing::TestPartResult& r) override {
        if (!r.failed())
            return;
        // The test is the one OnTestStart named: asking UnitTest for it here takes the lock
        // GoogleTest holds while it reports a part's result.
        event e = {{"e", "failure"},
                   {"id", current_},
                   {"file", r.file_name() ? r.file_name() : ""},
                   {"line", r.line_number() < 0 ? 0 : r.line_number()},
                   {"message", r.summary()}};
        if (r.fatally_failed())
            e["fatal"] = true;
        report_.write(e);
    }
    void OnTestEnd(const testing::TestInfo& t) override {
        const auto* r = t.result();
        std::string status = r->Skipped() ? "skip" : r->Failed() ? "fail" : "pass";
        std::string reason;
        if (r->Skipped())
            for (int i = 0; i < r->total_part_count(); ++i)
                if (r->GetTestPartResult(i).skipped())
                    reason = r->GetTestPartResult(i).message();
        while (!reason.empty() &&
               (reason.back() == '\n' || reason.back() == '\r' || reason.back() == ' '))
            reason.pop_back();
        counts_[status]++;
        event e = {{"e", "end"}, {"id", id_of(t)}, {"status", status}, {"ms", ms(started_)}};
        if (!reason.empty())
            e["reason"] = reason;
        report_.write(e);
    }
    const std::map<std::string, int>& counts() const {
        return counts_;
    }
private:
    static long long ms(std::chrono::steady_clock::time_point since) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now() - since)
            .count();
    }
    Report& report_;
    std::chrono::steady_clock::time_point began_, started_;
    std::string current_;
    std::map<std::string, int> counts_;
};

std::vector<const testing::TestInfo*> all_tests() {
    std::vector<const testing::TestInfo*> out;
    auto* u = testing::UnitTest::GetInstance();
    for (int i = 0; i < u->total_test_suite_count(); ++i) {
        const auto* s = u->GetTestSuite(i);
        for (int j = 0; j < s->total_test_count(); ++j)
            out.push_back(s->GetTestInfo(j));
    }
    return out;
}

}  // namespace

// The protocol's main of a GoogleTest program; src/main.cpp (the package's entry) calls it.
extern "C++" int mcpplibs_mtest_gtest_main(int argc, char** argv);

int mcpplibs_mtest_gtest_main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);
    const std::string report_path = env("MTEST_REPORT");
    if (env("MTEST_PROTOCOL").empty() || report_path.empty())
        return RUN_ALL_TESTS();
    [[maybe_unused]] volatile char keep = mcpplibs_mtest_gtest_marker[0];
    Report report(report_path);
    report.write({{"e", "program"},
                  {"protocol", 1},
                  {"level", 1},
                  {"framework", "googletest"},
                  {"version", "0.4.0"},
                  {"isolation", {"process", "case"}},
                  {"guide", "https://github.com/mcpp-community/mtest/blob/main/adapters/gtest/docs/"
                            "writing-tests.md"}});
    const auto tests = all_tests();
    if (env("MTEST_MODE") == "list") {
        for (const auto* t : tests) {
            event e = {{"e", "case"},       {"id", id_of(*t)},
                       {"name", t->name()}, {"file", t->file() ? t->file() : ""},
                       {"line", t->line()}, {"kind", "unit"}};
            if (!t->should_run())
                e["expect"] = "skip";
            report.write(e);
        }
        report.write({{"e", "done"}, {"status", "pass"}, {"counts", event::object()}});
        return 0;
    }
    bool missing = false;
    if (const auto select = env("MTEST_SELECT"); !select.empty()) {
        std::set<std::string> known;
        for (const auto* t : tests)
            known.insert(id_of(*t));
        std::ifstream in(select);
        std::string filter;
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (!known.contains(line)) {
                report.write({{"e", "missing"}, {"id", line}});
                missing = true;
                continue;
            }
            // GoogleTest's filter names `<suite>.<name>`; the protocol's identifier is
            // `<suite>/<name>`.
            const auto slash = line.find('/');
            filter +=
                (filter.empty() ? "" : ":") + line.substr(0, slash) + "." + line.substr(slash + 1);
        }
        GTEST_FLAG_SET(filter, filter.empty() ? "-*" : filter);
    }
    auto* listener = new Listener(report);
    testing::UnitTest::GetInstance()->listeners().Append(listener);
    const int status = RUN_ALL_TESTS();
    const bool failed = status != 0 || missing;
    report.write(
        {{"e", "done"}, {"status", failed ? "fail" : "pass"}, {"counts", listener->counts()}});
    return failed ? 1 : 0;
}
