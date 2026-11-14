// A copy of this program that does something else: a child, entered by mtest's entry before any
// case. The file declares no `main`; the package's entry begins it.
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;

using namespace mcpplibs::mtest;

child echo{"echo", [](std::vector<std::string> args) {
               for (const auto& a : args)
                   std::print("[{}]", a);
               return static_cast<int>(args.size()) + 3;
           }};

// Writes its first argument's count of bytes to standard error, and one line to standard output.
child loud{"loud", [](std::vector<std::string> args) {
               std::print(std::cerr, "{}", std::string(std::stoul(args.at(0)), 'e'));
               std::println("done");
               return 1;
           }};

UTest children{"children", [] {
                   "a child runs instead of the cases, with the arguments after its name"_test =
                       [] {
                           const std::vector<std::string> args{"a", "b c"};
                           auto o = e2e::run({.argv = child_command("echo", args)});
                           expect(fatal(o.started));
                           expect(eq(o.status, 5));
                           expect(eq(o.out, std::string("[a][b c]")));
                       };
                   "an output past its limit is kept to it, and the outcome says so"_test = [] {
                       const std::vector<std::string> bytes{"65536"};
                       auto o = e2e::run({.argv = child_command("loud", bytes), .err_limit = 1024});
                       expect(fatal(o.started));
                       expect(eq(o.err.size(), std::size_t{1024}));
                       expect(o.err_truncated);
                       expect(!o.out_truncated);
                       expect(o.out.starts_with("done")) << o.out;
                       const auto c = e2e::succeeded(o);
                       expect(!c.held);
                       expect(c.actual.contains("stderr was cut at 1024 bytes")) << c.actual;
                       auto whole = e2e::run({.argv = child_command("loud", bytes)});
                       expect(eq(whole.err.size(), std::size_t{65536}));
                       expect(!whole.err_truncated);
                   };
                   "a name no file declares is refused"_test = [] {
                       auto argv = self_command();
                       argv.insert(argv.end(), {"--mtest-child", "none"});
                       auto o = e2e::run({.argv = argv});
                       expect(eq(o.status, 2));
                       expect(o.err.find("declares no child named none") != std::string::npos)
                           << o.err;
                   };
               }};
