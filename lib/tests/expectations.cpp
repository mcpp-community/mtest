// The expectations, in this program: what holds, what does not, and what a failure says.
import std;
import mcpplibs.mtest;

using namespace mcpplibs::mtest;

int main() {
    "a comparison holds or names both sides"_test = [] {
        expect(eq(1 + 1, 2));
        auto c = eq(std::string("a"), std::string("b"));
        expect(!c.held);
        expect(eq(c.actual, std::string(R"("a")")));
        expect(eq(c.expected, std::string(R"("b")")));
        expect(contains(c.expression, "=="));
    };
    "the ordering comparisons"_test = [] {
        expect(lt(1, 2));
        expect(le(2, 2));
        expect(gt(3, 2));
        expect(ge(3, 3));
        expect(ne(1, 2));
        expect(near(0.1 + 0.2, 0.3, 1e-12));
        expect(!near(1.0, 1.1, 0.01).held);
    };
    "contains"_test = [] {
        expect(contains("hello world", "o w"));
        expect(!contains("hello", "x").held);
    };
    "throws and nothrow"_test = [] {
        expect(throws<std::runtime_error>([] { throw std::runtime_error("x"); }));
        expect(throws([] { throw std::logic_error("x"); }));
        expect(!throws<std::runtime_error>([] {}).held);
        expect(!throws<std::runtime_error>([] { throw 1; }).held);
        expect(nothrow([] {}));
        expect(!nothrow([] { throw std::runtime_error("x"); }).held);
    };
    "values are shown as text"_test = [] {
        expect(eq(detail::show(true), std::string("true")));
        expect(eq(detail::show(42), std::string("42")));
        expect(eq(detail::show("x"), std::string(R"("x")")));
        enum class colour { red = 3 };
        expect(eq(detail::show(colour::red), std::string("3")));
    };
    "a case of several values"_test = values{1, 2, 3} | [](int v) { expect(gt(v, 0)); };
    "a nested case"_test = [] {
        expect(eq(current_case(), std::string("a nested case")));
        "inside"_test = [] { expect(eq(current_case(), std::string("a nested case/inside"))); };
    };
    "a temporary directory is the case's own"_test = [] {
        const auto dir = temp_dir();
        expect(std::filesystem::is_directory(dir));
        expect(eq(temp_dir(), dir));
    };
    "a skipped case"_test[skip_if{true, "it is not this case's turn"}] = [] {
        fail("a skipped case ran");
    };
    "skip from inside"_test = [] { skip("decided while running"); };
    "a case expected to fail"_test[expect_failure{"the expectation is false on purpose"}] = [] {
        expect(false);
    };
    return report();
}
