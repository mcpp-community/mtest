// A suite run against a subject (mcpplibs.mtest.cts): what the subject lacks is unsupported, not
// passed; what the run requires and it lacks fails. The program runs itself again under a runner
// with the suite declared, and reads the events.
import std;
import nlohmann.json;
import mcpplibs.mtest;
import mcpplibs.mtest.cts;
import mcpplibs.mtest.e2e;

using namespace mcpplibs::mtest;
using json = nlohmann::json;

namespace {

// The suite of a small specification: reading is its core, writing and locking are optional.
void file_suite() {
    "what is written is read back"_test[clause{"F1"}] = [] { expect(true); };
    "a write past the end grows the file"_test[clause{"F2"}][cts::needs{"write"}] = [] {
        // A case runs where it is declared: its subject is there to ask.
        expect(fatal(cts::current_subject() != nullptr));
        expect(!cts::current_subject()->name.empty());
    };
    "two holders of a lock"_test[clause{"F3"}][cts::needs{"write", "lock"}] = [] {
        expect(false) << "never runs where the subject cannot lock";
    };
}

// What the copy started with --declared declares: the suite against two implementations.
void declare_suites() {
    cts::against({.name = "full",
                  .has = [](std::string_view capability) { return capability != "lock"; },
                  .required = {"write"}},
                 file_suite);
    cts::against({.name = "read-only",
                  .has = [](std::string_view) { return false; },
                  .required = {"write"}},
                 file_suite);
    cts::against({.name = "unknown"}, file_suite);
}

std::vector<json> events_of(std::string_view mode) {
    const auto report = temp_dir() / "report.ndjson";
    auto argv = self_command();
    argv.push_back("--declared");
    e2e::command c{.argv = argv};
    c.env = {{"MTEST_PROTOCOL", "1"},
             {"MTEST_MODE", std::string(mode)},
             {"MTEST_REPORT", report.string()}};
    auto r = e2e::run(std::move(c));
    expect(fatal(r.started));
    std::vector<json> out;
    std::ifstream in(report);
    for (std::string line; std::getline(in, line);)
        if (!line.empty())
            out.push_back(json::parse(line));
    return out;
}

std::optional<json> event(const std::vector<json>& events, std::string_view kind,
                          std::string_view id) {
    for (const auto& e : events)
        if (e.value("e", "") == kind && e.value("id", "") == id)
            return e;
    return std::nullopt;
}

std::string status(const std::vector<json>& events, std::string_view id) {
    auto e = event(events, "end", id);
    return e ? e->value("status", "") : "(no end)";
}

bool tagged(const json& listed, std::string_view tag) {
    return listed.contains("tags") && std::ranges::any_of(listed["tags"], [&](const json& t) {
               return t.get<std::string>() == tag;
           });
}

}  // namespace

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--declared") {
        declare_suites();
        return run(argc, argv);
    }
    "a case the subject has everything for runs and is judged"_test = [] {
        const auto events = events_of("run");
        expect(eq(status(events, "full/what is written is read back"), std::string("pass")));
        expect(eq(status(events, "full/a write past the end grows the file"), std::string("pass")));
    };
    "what the subject lacks is unsupported: skipped with the capability, never passed"_test = [] {
        const auto events = events_of("run");
        auto lock = event(events, "end", "full/two holders of a lock");
        expect(fatal(lock.has_value()));
        expect(eq(lock->value("status", ""), std::string("skip")));
        expect(eq(lock->value("reason", ""), std::string("unsupported: lock")));
        auto write = event(events, "end", "read-only/a write past the end grows the file");
        expect(fatal(write.has_value()));
        expect(eq(write->value("status", ""), std::string("skip")));
        expect(eq(write->value("reason", ""), std::string("unsupported: write")))
            << "the first capability it lacks";
    };
    "what the run requires and the subject lacks fails, said once"_test = [] {
        const auto events = events_of("run");
        expect(eq(status(events, "read-only/requires write"), std::string("fail")));
        expect(!event(events, "end", "full/requires write").has_value())
            << "a subject that has it declares no such case";
        expect(eq(status(events, "read-only/what is written is read back"), std::string("pass")))
            << "the cases it can run are still judged";
    };
    "a subject that states nothing is taken to have everything"_test = [] {
        const auto events = events_of("run");
        expect(
            eq(status(events, "unknown/a write past the end grows the file"), std::string("pass")));
        expect(eq(status(events, "unknown/two holders of a lock"), std::string("fail")))
            << "it ran, and its own check judged it";
    };
    "a listing names each subject as a conformance case with its tag"_test = [] {
        // P3.4: the suite's cases are listed through the case that declares them, the subject's.
        const auto events = events_of("list");
        for (std::string_view name : {"full", "read-only", "unknown"}) {
            auto listed = event(events, "case", name);
            expect(fatal(listed.has_value())) << name;
            expect(tagged(*listed, std::format("subject:{}", name)));
            expect(eq(listed->value("kind", ""), std::string("cts")));
        }
    };
    "outside a subject a case that states needs runs"_test[cts::needs{"anything"}] = [] {
        expect(eq(cts::current_subject(), static_cast<const cts::subject*>(nullptr)));
    };
    return report();
}
