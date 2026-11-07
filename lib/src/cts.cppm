// mcpplibs.mtest.cts: conformance to a specification (spec/protocol.md P6.4).
//
//   "P3.1 the list is written"_test[kind::cts][clause{ "P3.1" }] = [] { ... };
//   "P5.4 a crash is reported"_test[kind::cts][clause{ "P5.4" }][known_gap{ "not yet", "2026-12-31"
//   }] = ...; cts::coverage({ "P3.1", "P5.4" });   // a case that fails while a clause has no case
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

}  // namespace mcpplibs::mtest::cts
