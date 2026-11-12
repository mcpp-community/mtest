// mcpplibs.mtest.cts: conformance to a specification (spec/protocol.md P6.4).
//
//   "P3.1 the list is written"_test[kind::cts][clause{ "P3.1" }] = [] { ... };
//   "P5.4 a crash is reported"_test[kind::cts][clause{ "P5.4" }][known_gap{ "not yet", "2026-12-31"
//   }] = ...; cts::coverage({ "P3.1", "P5.4" });   // a case that fails while a clause has no case
//
// A suite run against an implementation, which may lack what the specification leaves optional:
//
//   cts::against({.name = "lavapipe", .has = has_capability, .required = {"compute"}}, [] {
//       "copies are exact"_test[clause{"R2.1"}] = [] { ... };
//       "indirect draws"_test[clause{"R4.3"}][cts::needs{"indirect"}] = [] { ... };
//   });
//   // lavapipe/copies are exact: pass or fail. lavapipe/indirect draws, where it has none:
//   // skip, "unsupported: indirect" -- never a pass. Without compute: "lavapipe/requires
//   // compute" fails.
export module mcpplibs.mtest.cts;

import std;
export import mcpplibs.mtest;

export namespace mcpplibs::mtest::cts {

// Declares the case `coverage`: it fails naming each of `clauses` that no case declared so far
// covers.
void coverage(std::initializer_list<std::string_view> clauses,
              std::source_location where = std::source_location::current());
void coverage(std::span<const std::string> clauses,
              std::source_location where = std::source_location::current());

// What a suite is run against: an implementation, and what it can do of what the specification
// leaves to implementations.
struct subject {
    std::string name;
    // Whether it has a capability, by the specification's name for it. None given: it has every
    // one.
    std::function<bool(std::string_view)> has;
    // The capabilities this run requires of it. One it lacks is a failure of the run (the case
    // `requires <capability>`), where a capability only some cases need leaves those unsupported.
    std::vector<std::string> required;
};

// Runs a suite against `s`. The cases `declare` declares are the parts of a case named after the
// subject and carry the tag `subject:<name>`, so one program may run the same suite against
// several implementations and a runner selects and reports each. A case that `needs` what the
// subject lacks is not run: its status is `skip` and its reason `unsupported: <capability>` --
// said from what the subject states it can do, never from a run that went wrong, and never a pass.
void against(subject s, std::function<void()> declare,
             std::source_location where = std::source_location::current());

// The subject of the cases `against` declares, while they are declared and run (a case runs where
// it is declared, so its body may ask); none outside `against`.
const subject* current_subject();

// What a case needs of the subject it is run against, written with its other attributes:
//   "indirect draws"_test[cts::needs{"indirect"}] = [] { ... };
// Each is also the tag `needs:<capability>`. Outside `against` nothing is known of a subject, and
// the case runs.
struct needs {
    std::vector<std::string> capabilities;
    needs(std::initializer_list<std::string_view> names)
        : capabilities(names.begin(), names.end()) {}
    explicit needs(std::vector<std::string> names) : capabilities(std::move(names)) {}
};
void apply(attributes& a, needs n);

}  // namespace mcpplibs::mtest::cts
