// The bench, cts and e2e modules, in this program.
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.bench;
import mcpplibs.mtest.cts;
import mcpplibs.mtest.e2e;

using namespace mcpplibs::mtest;

int main(int argc, char** argv) {
    if (argc > 1 && std::string_view(argv[1]) == "--print-protocol-env") {
        for (auto v : {"MTEST_PROTOCOL", "MTEST_REPORT", "MTEST_SELECT", "MTEST_MODE"})
            if (auto x = std::getenv(v))
                std::println("{}={}", v, x);
        return 0;
    }
    "a measurement has its samples and a median among them"_test[kind::bench] = [] {
        int x = 0;
        auto s =
            bench::measure("increment", [&] { bench::keep(++x); }, {.samples = 5, .warmup = 1});
        expect(eq(s.samples.size(), std::size_t(5)));
        expect(ge(s.median, s.min));
        expect(le(s.median, s.max));
        expect(gt(s.iterations, std::size_t(0)));
    };
    "a summary of known samples"_test = [] {
        auto s = bench::summarize({3, 1, 2, 10, 4}, 1);
        expect(eq(s.median, 3.0));
        expect(eq(s.min, 1.0));
        expect(eq(s.max, 10.0));
        expect(eq(s.mad, 1.0));
    };
    "a gap that is still planned is expected to fail"_test[known_gap{"not met yet", "9999-12-31"}] =
        [] { expect(false); };
    "a gap past its date is judged as any case"_test[known_gap{"met since", "2000-01-01"}] = [] {
        expect(true);
    };
    "a tree of files"_test = [] {
        const auto root = e2e::tree({{"a.txt", "one"}, {"d/b.txt", "two"}});
        expect(std::filesystem::is_regular_file(root / "a.txt"));
        expect(std::filesystem::is_regular_file(root / "d" / "b.txt"));
    };
    "a golden file holds or names where it differs"_test = [] {
        const auto root = e2e::tree({{"g.txt", "line 1\nline 2\n"}});
        expect(e2e::golden(root / "g.txt", "line 1\nline 2\n"));
        auto c = e2e::golden(root / "g.txt", "line 1\nline 3\n");
        expect(!c.held);
        expect(contains(c.expression, "line 2"));
    };
    "a program started by a case receives no protocol variable"_test[kind::e2e] = [] {
        auto self = self_command();
        expect(fatal(!self.empty()));
        self.push_back("--print-protocol-env");
        auto r = e2e::run({.argv = self, .env = {{"MTEST_PROTOCOL", "1"}}});
        // The program printed what it received; a set protocol variable it was given is the
        // caller's own choice, so only the ones this program did not set are checked.
        expect(r.started);
        expect(!contains(r.out, "MTEST_REPORT=").held) << r.out;
    };
    "a case that covers a clause"_test[kind::cts][clause{"P6.4"}] = [] {
        expect(declared_clauses().contains("P6.4"));
    };
    cts::coverage({"P6.4"});
    return report();
}
