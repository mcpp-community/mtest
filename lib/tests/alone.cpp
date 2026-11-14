// The program run alone, by a person: its arguments select what runs.
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.e2e;

using namespace mcpplibs::mtest;

namespace {

e2e::outcome alone(std::vector<std::string> arguments) {
    auto argv = self_command();
    argv.insert(argv.end(), arguments.begin(), arguments.end());
    return e2e::run({.argv = argv});
}

bool ran(const e2e::outcome& o, std::string_view id) {
    return o.out.find(std::format("ok   {}\n", id)) != std::string::npos;
}

}  // namespace

UTest alone_subject{"alone.subject", [] {
                        "near/one"_test = [] {};
                        "near/two"_test = [] {};
                        "far/one"_test = [] {};
                    }};

UTest
    alone_cases{"alone", [] {
                    "--list prints every case's identifier and runs none"_test = [] {
                        const auto o = alone({"--list"});
                        expect(eq(o.status, 0));
                        expect(o.out.find("alone.subject\n") != std::string::npos) << o.out;
                        expect(o.out.find("ok ") == std::string::npos) << o.out;
                    };
                    "an identifier selects that case"_test = [] {
                        const auto o = alone({"alone.subject/near/one"});
                        expect(eq(o.status, 0)) << o.out;
                        expect(ran(o, "alone.subject/near/one")) << o.out;
                        expect(!ran(o, "alone.subject/near/two")) << o.out;
                    };
                    "a name selects the cases under it"_test = [] {
                        const auto o = alone({"alone.subject/near"});
                        expect(eq(o.status, 0)) << o.out;
                        expect(ran(o, "alone.subject/near/one") && ran(o, "alone.subject/near/two"))
                            << o.out;
                        expect(!ran(o, "alone.subject/far/one")) << o.out;
                        expect(o.out.find("no such case") == std::string::npos) << o.out;
                    };
                    "a name is whole: part of one selects nothing and fails"_test = [] {
                        const auto o = alone({"alone.subject/ne"});
                        expect(eq(o.status, 1));
                        expect(o.out.find("FAIL alone.subject/ne -- no such case") !=
                               std::string::npos)
                            << o.out;
                    };
                    "a pattern selects the cases it matches"_test = [] {
                        const auto o = alone({"alone.subject/*/one"});
                        expect(eq(o.status, 0)) << o.out;
                        expect(ran(o, "alone.subject/near/one") && ran(o, "alone.subject/far/one"))
                            << o.out;
                        expect(!ran(o, "alone.subject/near/two")) << o.out;
                    };
                    "a pattern beside an identifier: both"_test = [] {
                        const auto o = alone({"alone.subject/f*", "alone.subject/near/two"});
                        expect(eq(o.status, 0)) << o.out;
                        expect(ran(o, "alone.subject/far/one") && ran(o, "alone.subject/near/two"))
                            << o.out;
                        expect(!ran(o, "alone.subject/near/one")) << o.out;
                    };
                    "a pattern nothing matches fails"_test = [] {
                        const auto o = alone({"alone.subject/*/three"});
                        expect(eq(o.status, 1));
                        expect(o.out.find("FAIL alone.subject/*/three -- no such case") !=
                               std::string::npos)
                            << o.out;
                    };
                }};
