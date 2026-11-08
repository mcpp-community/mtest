// The static case manifest: the cases a source file declares, read from its text.
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.manifest;

using namespace mcpplibs::mtest;
namespace mf = mcpplibs::mtest::manifest;

int main() {
    "a literal in a named suite, with its place and attributes"_test = [] {
        auto cases = mf::scan("import mcpplibs.mtest;\n"
                              "suite parsing{\"parsing\", [] {\n"
                              "    \"an empty line\"_test[tag{\"fast\"}][kind::e2e] = [] {};\n"
                              "}};\n");
        expect(fatal(eq(cases.size(), std::size_t(1))));
        expect(eq(cases[0].id, std::string("parsing/an empty line")));
        expect(eq(cases[0].line, 3));
        expect(eq(cases[0].column, 5));
        expect(eq(cases[0].kind, std::string("e2e")));
        expect(eq(cases[0].tags, std::vector<std::string>{"fast"}));
    };
    "a case inside a case, and the suite's end"_test = [] {
        auto cases = mf::scan("mtest::suite s{\"s\", [] {\n"
                              "  \"outer\"_test = [] { \"inner\"_test = [](int) {}; };\n"
                              "}};\n"
                              "int main() { \"top\"_test = [] {}; }\n");
        std::vector<std::string> ids;
        for (const auto& c : cases)
            ids.push_back(c.id);
        expect(eq(ids, std::vector<std::string>{"s/outer", "s/outer/inner", "top"}));
    };
    "comments, strings and raw strings are not cases"_test = [] {
        auto cases = mf::scan("// \"a\"_test = [] {};\n"
                              "/* \"b\"_test */\n"
                              "auto t = \"\\\"c\\\"_test\";\n"
                              "auto r = R\"x(\"d\"_test = [] {})x\";\n"
                              "char q = '\"';\n"
                              "\"e\"_test = [] {};\n");
        expect(fatal(eq(cases.size(), std::size_t(1))));
        expect(eq(cases[0].id, std::string("e")));
        expect(eq(cases[0].line, 6));
    };
    "a case given a function, and a suite with no name"_test = [] {
        auto cases =
            mf::scan("void body();\n"
                     "suite plain = [] { \"named\"_test = body; \"after\"_test = [] {}; };\n");
        std::vector<std::string> ids;
        for (const auto& c : cases)
            ids.push_back(c.id);
        expect(eq(ids, std::vector<std::string>{"named", "after"}));
    };
    return report();
}
