module mcpplibs.mtest.cts;

import std;

namespace mcpplibs::mtest::cts {

void coverage(std::span<const std::string> clauses, std::source_location where) {
    std::vector<std::string> want(clauses.begin(), clauses.end());
    test("coverage")[kind::cts] = body(
        [want] {
            const auto have = declared_clauses();
            std::string missing;
            for (const auto& c : want)
                if (!have.contains(c))
                    missing += (missing.empty() ? "" : ", ") + c;
            expect(missing.empty()) << "no case covers " << missing;
        },
        where);
}

void coverage(std::initializer_list<std::string_view> clauses, std::source_location where) {
    std::vector<std::string> v(clauses.begin(), clauses.end());
    coverage(std::span<const std::string>(v), where);
}

}  // namespace mcpplibs::mtest::cts
