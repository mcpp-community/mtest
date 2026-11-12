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

namespace {

// The subject of the cases being declared: set while the body `against` declared runs, on the
// thread that declares.
thread_local const subject* declaring = nullptr;

bool lacks(const subject& s, std::string_view capability) {
    return s.has && !s.has(capability);
}

}  // namespace

const subject* current_subject() {
    return declaring;
}

void against(subject s, std::function<void()> declare, std::source_location where) {
    std::string name = s.name;
    // Braces: with parentheses this would declare an array named `name`.
    test{name}[kind::cts][tag{"subject:" + name}] = body(
        [s = std::move(s), declare = std::move(declare), where] {
            // Restored however the declarations end.
            struct scope {
                const subject* outer;
                ~scope() {
                    declaring = outer;
                }
            } restore{std::exchange(declaring, &s)};
            // What this run requires and the subject lacks: a failure, stated once each.
            for (const auto& capability : s.required)
                if (lacks(s, capability))
                    test("requires " + capability)[tag{"needs:" + capability}] = body(
                        [&s, capability] {
                            fail(std::format("{} lacks `{}`, which this run requires", s.name,
                                             capability));
                        },
                        where);
            if (declare)
                declare();
        },
        where);
}

void apply(attributes& a, needs n) {
    for (auto& capability : n.capabilities) {
        if (declaring && !a.skip && lacks(*declaring, capability))
            a.skip = "unsupported: " + capability;
        a.tags.push_back("needs:" + std::move(capability));
    }
}

}  // namespace mcpplibs::mtest::cts
