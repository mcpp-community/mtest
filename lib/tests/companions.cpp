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
    // A program that prompts, answers each line it reads, and ends with the status it is told.
    if (argc > 1 && std::string_view(argv[1]) == "--echo-loop") {
        std::string line;
        std::cout << "> ";
        std::cout.flush();
        while (std::getline(std::cin, line)) {
            if (line.starts_with("exit "))
                return std::stoi(line.substr(5));
            std::cout << "got: " << line << "\n> ";
            std::cout.flush();
        }
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
    "each call measured by itself: where the calls lie, less what a call paused for"_test
        [kind::bench] = [] {
            std::size_t calls = 0, warm = 0;
            const auto spin = [](std::chrono::microseconds d) {
                const auto end = std::chrono::steady_clock::now() + d;
                while (std::chrono::steady_clock::now() < end) {}
            };
            auto d =
                bench::each("step",
                            [&](bench::lap& l) {
                                l.warmup() ? ++warm : ++calls;
                                l.pause();
                                spin(std::chrono::microseconds{2000});  // not the call's
                                l.resume();
                                spin(std::chrono::microseconds{l.index() % 10 == 9 ? 400 : 100});
                                l.items(50);
                            },
                            {.least = 40, .most = 40, .warmup = 2});
            expect(eq(d.samples.size(), std::size_t(40)) && eq(calls, std::size_t(40)) &&
                   eq(warm, std::size_t(2)));
            expect(le(d.min, d.p50) && le(d.p50, d.p95) && le(d.p95, d.p99) && le(d.p99, d.max));
            expect(ge(d.min, 0.0001) && lt(d.p50, 0.002)) << "the pause is not counted: " << d.p50;
            expect(ge(d.max, 0.0004)) << "the slow calls are seen: " << d.max;
            expect(eq(d.items, std::uint64_t(50)) && gt(d.per_item, 0.0) && lt(d.per_item, d.mean));
        };
    "a call says what it was made of: parts, quantities, and the slowest calls one by one"_test
        [kind::bench] = [] {
            const auto spin = [](std::chrono::microseconds d) {
                const auto end = std::chrono::steady_clock::now() + d;
                while (std::chrono::steady_clock::now() < end) {}
            };
            auto d = bench::each("frame",
                                 [&](bench::lap& l) {
                                     const bool slow = !l.warmup() && l.index() == 2 + 7;
                                     {
                                         auto encoding = l.time("encode");
                                         spin(std::chrono::microseconds{slow ? 900 : 100});
                                     }
                                     l.part("upload", std::chrono::microseconds{30});
                                     l.part("upload", std::chrono::microseconds{20});  // added to
                                     l.value("glyphs", slow ? 24570 : 922);
                                     if (l.index() % 2 == 0)
                                         l.value("lists", 11);  // some calls say it
                                     if (slow)
                                         l.say("zoom 0.43");
                                 },
                                 {.least = 20, .most = 20, .warmup = 2, .slowest = 2});
            expect(eq(d.parts.size(), std::size_t(2)) && eq(d.values.size(), std::size_t(2)));
            expect(eq(d.parts[0].name, std::string("encode")) &&
                   eq(d.parts[0].calls, std::size_t(20)));
            expect(ge(d.parts[0].max, 0.0009) && lt(d.parts[0].p50, 0.0005)) << d.parts[0].max;
            expect(eq(d.parts[1].name, std::string("upload")) && eq(d.parts[1].max, 0.00005));
            expect(eq(d.values[0].name, std::string("glyphs")) && eq(d.values[0].max, 24570.0) &&
                   eq(d.values[0].min, 922.0));
            expect(eq(d.values[1].calls, std::size_t(10)) && eq(d.values[1].mean, 11.0))
                << "over the calls that said it";
            // Which calls a loaded machine slows is not this test's to say: each stated call
            // carries what that call said, the slowest first.
            expect(eq(d.slowest.size(), std::size_t(2)) &&
                   ge(d.slowest[0].seconds, d.slowest[1].seconds));
            for (const bench::slow_call& c : d.slowest) {
                const bool slow = c.index == 7;
                expect(eq(c.parts.size(), std::size_t(2)) && eq(c.parts[1].second, 0.00005))
                    << c.index;
                expect(eq(c.values[0].second, slow ? 24570.0 : 922.0) &&
                       eq(c.said, std::string(slow ? "zoom 0.43" : "")))
                    << c.index;
                expect(eq(c.values.size(), std::size_t(c.index % 2 == 0 ? 2 : 1)))
                    << "the calls of an even turn said lists";
            }
        };
    "a limit a call: how many passed it, and those are the ones stated; a body ends the run"_test
        [kind::bench] = [] {
            const auto spin = [](std::chrono::microseconds d) {
                const auto end = std::chrono::steady_clock::now() + d;
                while (std::chrono::steady_clock::now() < end) {}
            };
            std::size_t calls = 0;
            auto d = bench::each("sweep",
                                 [&](bench::lap& l) {
                                     ++calls;
                                     spin(std::chrono::microseconds{
                                         l.index() == 4 || l.index() == 9 ? 700 : 50});
                                     if (l.index() == 11)
                                         l.last();  // the sweep has run out
                                 },
                                 {.least = 100,
                                  .most = 100,
                                  .warmup = 0,
                                  .limit = std::chrono::microseconds{400}});
            expect(eq(calls, std::size_t(12)) && eq(d.samples.size(), std::size_t(12)));
            // The two that spun past the limit are over it, and stated; a loaded machine may put
            // others over.
            expect(ge(d.over, std::size_t(2)) &&
                   eq(d.slowest.size(), std::min<std::size_t>(d.over, 8)) && eq(d.limit, 0.0004));
            if (d.over <= 8)
                for (std::size_t late : {std::size_t(4), std::size_t(9)})
                    expect(std::ranges::contains(d.slowest, late, &bench::slow_call::index))
                        << late;
        };
    "the same as a loop: the calls between one next and the following one"_test[kind::bench] = [] {
        std::size_t calls = 0, warm = 0;
        std::uint64_t state = 1;  // kept in the function the loop runs in
        bench::laps run{"loop", {.least = 30, .most = 30, .warmup = 2, .slowest = 1}};
        while (run.next()) {
            run.warmup() ? ++warm : ++calls;
            run.pause();
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;  // not the call's
            run.resume();
            bench::keep(state);
            run.value("turn", double(run.index()));
            if (run.index() == 2 + 19)
                run.last();  // the 20th measured call is the last
        }
        expect(!run.next()) << "and it stays ended";
        const bench::distribution& d = run.result();
        expect(eq(warm, std::size_t(2)) && eq(calls, std::size_t(20)) &&
               eq(d.samples.size(), std::size_t(20)));
        expect(le(d.min, d.p50) && le(d.p50, d.p99) && le(d.p99, d.max));
        expect(eq(d.values.size(), std::size_t(1)) && eq(d.values[0].min, 2.0) &&
               eq(d.values[0].max, 21.0))
            << "a warm-up call's is not kept";
        expect(eq(d.slowest.size(), std::size_t(1)) &&
               eq(d.slowest[0].values[0].second, double(d.slowest[0].index + 2)));
    };
    "calls that say nothing keep nothing"_test[kind::bench] = [] {
        auto d = bench::each("quiet", [](bench::lap&) {},
                             {.least = 5, .most = 5, .warmup = 1, .slowest = 1});
        expect(d.parts.empty() && d.values.empty() && eq(d.slowest.size(), std::size_t(1)) &&
               d.slowest[0].parts.empty() && d.slowest[0].said.empty());
    };
    "a program that counts its allocations: a call's, less what it paused for"_test[kind::bench] =
        [] {
            auto d = bench::each("alloc",
                                 [](bench::lap& l) {
                                     l.pause();
                                     bench::allocated(1000);  // not the call's
                                     l.resume();
                                     bench::allocated(64);
                                     if (l.index() == 3)
                                         bench::allocated(32);
                                 },
                                 {.least = 6, .most = 6, .warmup = 1});
            expect(eq(d.values.size(), std::size_t(2)) &&
                   eq(d.values[0].name, std::string("allocations")));
            expect(eq(d.values[0].min, 1.0) && eq(d.values[0].max, 2.0) &&
                   eq(d.values[1].max, 96.0) && eq(d.values[1].min, 64.0));
        };
    "a body that takes no lap is measured the same"_test[kind::bench] = [] {
        int x = 0;
        auto d =
            bench::each("plain", [&] { bench::keep(++x); }, {.least = 5, .most = 5, .warmup = 0});
        expect(eq(d.samples.size(), std::size_t(5)) && eq(x, 5) && eq(d.items, std::uint64_t(0)) &&
               eq(d.per_item, 0.0));
    };
    "a distribution of known samples"_test = [] {
        auto d = bench::distribute({5, 1, 3, 2, 4}, 2);
        expect(eq(d.min, 1.0) && eq(d.p50, 3.0) && eq(d.max, 5.0) && eq(d.mean, 3.0) &&
               eq(d.per_item, 1.5));
        expect(eq(d.p95, 4.8) && eq(d.samples.front(), 5.0))
            << "between the two nearest; the samples stay in order";
        const std::array<double, 2> two{10, 20};
        expect(eq(bench::percentile(two, 0.5), 15.0) && eq(bench::percentile({}, 0.5), 0.0));
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
    "a matcher names both sides"_test = [] {
        expect(e2e::contains("hello world", "lo w"));
        expect(!e2e::contains("hello", "bye").held);
        expect(e2e::matches("version 1.2.3", R"(\d+\.\d+\.\d+)"));
        auto bad = e2e::matches("x", "(");
        expect(!bad.held);
        expect(contains(bad.expected, "valid pattern"));
    };
    "a command is a line a shell repeats"_test = [] {
        e2e::command c{.argv = {"prog", "two words", "it's"},
                       .cwd = "/tmp/a b",
                       .env = {{"K", "v 1"}, {"GONE", std::nullopt}}};
        expect(eq(e2e::shell_line(c),
                  std::string("cd '/tmp/a b' && K='v 1' unset GONE; prog 'two words' 'it'\\''s'")));
    };
    "an outcome that failed carries its command and outputs"_test[kind::e2e] = [] {
        auto self = self_command();
        expect(fatal(!self.empty()));
        self.push_back("--echo-loop");
        auto r = e2e::run({.argv = self, .input = "exit 3\n"});
        expect(e2e::exited(r, 3));
        auto c = e2e::succeeded(r);
        expect(!c.held);
        expect(contains(c.actual, "--echo-loop"));
        expect(contains(c.actual, "status 3"));
    };
    "a session expects and sends"_test[kind::e2e] = [] {
        auto self = self_command();
        expect(fatal(!self.empty()));
        self.push_back("--echo-loop");
        auto s = e2e::session::start({.argv = self});
        expect(fatal(s.started()));
        expect(s.saw("> "));
        s.send_line("one");
        auto got = s.expect(R"(got: (\w+)\n> )", std::chrono::seconds{10});
        expect(fatal(got.has_value()));
        expect(contains(*got, "got: one"));
        expect(!s.expect("never printed", std::chrono::milliseconds{300}).has_value());
        expect(s.timed_out());
        s.send_line("exit 7");
        auto r = s.finish();
        expect(eq(r.status, 7));
        expect(contains(s.transcript(), "[sent] one"));
    };
    "a session whose program ended says so"_test[kind::e2e] = [] {
        auto self = self_command();
        expect(fatal(!self.empty()));
        self.push_back("--echo-loop");
        auto s = e2e::session::start({.argv = self, .input = "exit 0\n"});
        expect(!s.expect("not there", std::chrono::seconds{10}).has_value());
        expect(s.ended());
        expect(!s.timed_out());
    };
    "a case that covers a clause"_test[kind::cts][clause{"P6.4"}] = [] {
        expect(declared_clauses().contains("P6.4"));
    };
    cts::coverage({"P6.4"});
    return report();
}
