// doctest under the mtest protocol (spec/protocol.md, level 1), and doctest's implementation.
//
// With no runner it is doctest's own main. Under one (MTEST_PROTOCOL and MTEST_REPORT), `list` mode
// writes the registered test cases as cases without running any (P3); a selection (MTEST_SELECT)
// runs exactly the named cases and an identifier no case has is `missing` (P4.1); a reporter writes
// each case's begin, failures and end (P5). A case doctest skips by its decorator is reported
// skipped, with its description as the reason; one `should_fail` or `expected_failures` decorates
// is expected to fail (`xfail`, `xpass`); `timeout` is stated to the runner (O5).
#define DOCTEST_CONFIG_IMPLEMENT
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <doctest/doctest.h>
#include <fstream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <vector>

// P2.3: a runner that reads this program's file finds the protocol here.
extern "C" const char mcpplibs_mtest_doctest_marker[] = "mtest-protocol=1";

namespace {

using event = nlohmann::ordered_json;

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

// P2.2: the programs a case starts do not get the protocol's variables, so a test program a case
// starts is not taken for the runner's; a case that runs one as its runner states them itself.
// Once read, they leave the C library's environment, which `std::system`, `posix_spawn` and a
// started program inherit, before any case runs. A case therefore reads none of them.
constexpr const char* protocol_variables[] = {"MTEST_PROTOCOL",       "MTEST_MODE",
                                              "MTEST_REPORT",         "MTEST_SELECT",
                                              "MTEST_ISOLATION",      "MTEST_TMPDIR",
                                              "MTEST_SEED",           "MTEST_MONITOR",
                                              "MTEST_MONITOR_SAMPLE", "MTEST_SERVE"};

// The C library's own call, as each host names it: mcpplibs.os states no change to a program's own
// environment (mcpplibs/os#29).
void remove_variable(const char* name) {
#if defined(_WIN32)
    _putenv_s(name, "");
#else
    unsetenv(name);
#endif
}

// What the runner stated (P2.1), read once and then removed.
struct handshake {
    std::string protocol, mode, report, select;
};
handshake take_handshake() {
    handshake h{env("MTEST_PROTOCOL"), env("MTEST_MODE"), env("MTEST_REPORT"), env("MTEST_SELECT")};
    for (const char* name : protocol_variables)
        remove_variable(name);
    return h;
}

// The report (P5.1): one event per line, written whole.
struct report_file {
    std::ofstream out;
    void write(const event& e) {
        if (!out)
            return;
        out << e.dump(-1, ' ', false, event::error_handler_t::replace) << "\n";
        out.flush();
    }
};
report_file& report() {
    static report_file r;
    return r;
}

// What a run selected; empty: every case.
std::set<std::string>& selected() {
    static std::set<std::string> s;
    return s;
}
std::map<std::string, int>& counts() {
    static std::map<std::string, int> c;
    return c;
}

std::string name_of(const doctest::TestCaseData& t) {
    const std::string suite = t.m_test_suite ? t.m_test_suite : "";
    return suite.empty() ? std::string(t.m_name) : suite + "/" + t.m_name;
}

// The identifiers of the registered cases (P3.2: distinct in a program). doctest accepts two cases
// of one name; in doctest's order (file, then line) the first is `<suite>/<name>` and each later
// one `<suite>/<name>#<n>`, n from 2.
std::map<const doctest::TestCaseData*, std::string> assign_identifiers() {
    std::vector<const doctest::TestCaseData*> cases;
    for (const auto& t : doctest::detail::getRegisteredTests())
        cases.push_back(&t);
    std::stable_sort(cases.begin(), cases.end(), [](const auto* a, const auto* b) {
        const int file = a->m_file.compare(b->m_file);
        return file != 0 ? file < 0 : a->m_line < b->m_line;
    });
    std::map<const doctest::TestCaseData*, std::string> ids;
    std::set<std::string> taken;
    for (const auto* t : cases) {
        const auto name = name_of(*t);
        auto id = name;
        for (int n = 2; taken.contains(id); ++n)
            id = name + "#" + std::to_string(n);
        taken.insert(id);
        ids.emplace(t, id);
    }
    return ids;
}

std::string id_of(const doctest::TestCaseData& t) {
    static const auto ids = assign_identifiers();
    const auto found = ids.find(&t);
    return found != ids.end() ? found->second : name_of(t);
}

// A case's status (P5.3) from doctest's reasons: `should_fail` and `expected_failures(n)` state an
// expectation, `may_fail` states none.
std::string status_of(const doctest::CurrentTestCaseStats& st) {
    namespace reason = doctest::TestCaseFailureReason;
    if (st.failure_flags & (reason::ShouldHaveFailedAndDid | reason::FailedExactlyNumTimes))
        return "xfail";
    if (st.failure_flags & reason::ShouldHaveFailedButDidnt)
        return "xpass";
    if ((st.failure_flags & reason::DidntFailExactlyNumTimes) &&
        st.numAssertsFailedCurrentTest == 0)
        return "xpass";
    return st.testCaseSuccess ? "pass" : "fail";
}

const auto began = std::chrono::steady_clock::now();
long long ms_since(std::chrono::steady_clock::time_point at) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() -
                                                                 at)
        .count();
}

class mtest_reporter : public doctest::IReporter {
public:
    explicit mtest_reporter(const doctest::ContextOptions&) {}
    void report_query(const doctest::QueryData& q) override {
        for (unsigned i = 0; i < q.num_data; ++i) {
            const auto& t = *q.data[i];
            event e = {{"e", "case"},      {"id", id_of(t)},
                       {"name", t.m_name}, {"file", t.m_file.c_str()},
                       {"line", t.m_line}, {"kind", "unit"}};
            if (t.m_skip) {
                e["expect"] = "skip";
                e["reason"] = t.m_description && *t.m_description ? t.m_description : "skipped";
            } else if (t.m_should_fail || t.m_expected_failures > 0)
                e["expect"] = "fail";
            if (t.m_timeout > 0)
                e["timeout"] = t.m_timeout;
            report().write(e);
        }
    }
    void test_run_start() override {}
    void test_run_end(const doctest::TestRunStats&) override {}
    void test_case_start(const doctest::TestCaseData& t) override {
        case_ = &t;
        current_ = id_of(t);
        started_ = std::chrono::steady_clock::now();
        report().write({{"e", "begin"}, {"id", current_}, {"t", ms_since(began)}});
    }
    void test_case_reenter(const doctest::TestCaseData&) override {}
    void test_case_end(const doctest::CurrentTestCaseStats& st) override {
        const std::string status = status_of(st);
        ++counts()[status];
        report().write(
            {{"e", "end"}, {"id", current_}, {"status", status}, {"ms", ms_since(started_)}});
    }
    // doctest gives an exception or a crash no place; the case's own is its place.
    void test_case_exception(const doctest::TestCaseException& e) override {
        report().write({{"e", "failure"},
                        {"id", current_},
                        {"file", case_ ? case_->m_file.c_str() : ""},
                        {"line", case_ ? case_->m_line : 0},
                        {"message", std::string(e.is_crash ? "crashed: " : "threw: ") +
                                        e.error_string.c_str()},
                        {"fatal", true}});
    }
    void subcase_start(const doctest::SubcaseSignature&) override {}
    void subcase_end() override {}
    void log_assert(const doctest::AssertData& a) override {
        if (!a.m_failed)
            return;
        std::string message = a.m_expr ? a.m_expr : "";
        if (a.m_threw)
            message += std::string(" threw: ") + a.m_exception.c_str();
        else if (a.m_decomp.size())
            message += std::string(" (") + a.m_decomp.c_str() + ")";
        event e = {{"e", "failure"},
                   {"id", current_},
                   {"file", a.m_file ? a.m_file : ""},
                   {"line", a.m_line},
                   {"message", message}};
        if (a.m_at & doctest::assertType::is_require)
            e["fatal"] = true;
        report().write(e);
    }
    void log_message(const doctest::MessageData&) override {}
    void test_case_skipped(const doctest::TestCaseData& t) override {
        const auto id = id_of(t);
        // A case a selection left out is not reported; one its decorator skipped is.
        if (!selected().empty() && !selected().contains(id))
            return;
        const std::string reason =
            t.m_description && *t.m_description ? t.m_description : "skipped";
        ++counts()["skip"];
        report().write({{"e", "begin"}, {"id", id}, {"t", ms_since(began)}});
        report().write(
            {{"e", "end"}, {"id", id}, {"status", "skip"}, {"ms", 0}, {"reason", reason}});
    }
private:
    const doctest::TestCaseData* case_ = nullptr;
    std::string current_;
    std::chrono::steady_clock::time_point started_;
};

}  // namespace

REGISTER_REPORTER("mtest", 1, mtest_reporter);

// The protocol's main of a doctest program; src/main.cpp (the package's entry) calls it.
extern "C++" int mcpplibs_mtest_doctest_main(int argc, char** argv);

int mcpplibs_mtest_doctest_main(int argc, char** argv) {
    doctest::Context context;
    context.applyCommandLine(argc, argv);
    const auto runner = take_handshake();
    const std::string& path = runner.report;
    if (runner.protocol.empty() || path.empty())
        return context.run();
    const char* volatile keep = mcpplibs_mtest_doctest_marker;
    (void)keep;
    report().out.open(path, std::ios::app | std::ios::binary);
    report().write(
        {{"e", "program"},
         {"protocol", 1},
         {"level", 1},
         {"framework", "doctest"},
         {"version", "0.11.4"},
         {"isolation", {"process", "case"}},
         {"guide", "https://github.com/mcpp-community/mtest/blob/main/adapters/doctest/docs/"
                   "writing-tests.md"}});
    // The list is the registry's: doctest's own listing leaves out a case its decorator skips.
    if (runner.mode == "list") {
        std::vector<const doctest::TestCaseData*> all;
        for (const auto& t : doctest::detail::getRegisteredTests())
            all.push_back(&t);
        doctest::QueryData q;
        q.data = all.data();
        q.num_data = static_cast<unsigned>(all.size());
        mtest_reporter(doctest::ContextOptions{}).report_query(q);
        report().write({{"e", "done"}, {"status", "pass"}, {"counts", event::object()}});
        return 0;
    }
    // doctest's options as its command line spells them: setOption does not set every option.
    const char* options[] = {argc > 0 ? argv[0] : "test", "--reporters=mtest"};
    context.applyCommandLine(2, options);
    bool missing = false;
    if (const auto& select = runner.select; !select.empty()) {
        std::set<std::string> known;
        for (const auto& t : doctest::detail::getRegisteredTests())
            known.insert(id_of(t));
        std::ifstream in(select);
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty())
                continue;
            if (!known.contains(line)) {
                report().write({{"e", "missing"}, {"id", line}});
                missing = true;
                continue;
            }
            selected().insert(line);
        }
        // A case the selection leaves out is skipped: doctest runs no case marked so, and its
        // filters match names by patterns, which an identifier with `*` or `,` would defeat.
        for (const auto& t : doctest::detail::getRegisteredTests())
            if (!selected().contains(id_of(t)))
                const_cast<doctest::detail::TestCase&>(t).m_skip = true;
    }
    const int status = context.run();
    const bool failed =
        status != 0 || missing || counts()["fail"] > 0 || counts().contains("xpass");
    report().write({{"e", "done"}, {"status", failed ? "fail" : "pass"}, {"counts", counts()}});
    return failed ? 1 : 0;
}
