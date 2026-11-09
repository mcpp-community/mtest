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

UTest children{"children", [] {
                   "a child runs instead of the cases, with the arguments after its name"_test =
                       [] {
                           const std::vector<std::string> args{"a", "b c"};
                           auto o = e2e::run({.argv = child_command("echo", args)});
                           expect(fatal(o.started));
                           expect(eq(o.status, 5));
                           expect(eq(o.out, std::string("[a][b c]")));
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
