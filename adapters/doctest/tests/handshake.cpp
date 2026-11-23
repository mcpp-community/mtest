// The adapter under its own runner (P2.2): a case and a program it starts see none of the
// protocol's variables, which the runner stated to this program.
#include <cstdlib>
#include <doctest/doctest.h>

namespace {

// A command of the host's shell that fails when the program it starts has MTEST_PROTOCOL or
// MTEST_REPORT: `cmd` where the host has one (COMSPEC), `sh` elsewhere.
const char* command_seeing_protocol() {
    return std::getenv("COMSPEC")
               ? "if defined MTEST_PROTOCOL (exit 1) else if defined MTEST_REPORT (exit 1)"
               : "test -z \"${MTEST_PROTOCOL}${MTEST_REPORT}\"";
}

}  // namespace

TEST_SUITE("handshake") {
    TEST_CASE("a case sees no protocol variable") {
        CHECK(std::getenv("MTEST_PROTOCOL") == nullptr);
        CHECK(std::getenv("MTEST_MODE") == nullptr);
        CHECK(std::getenv("MTEST_REPORT") == nullptr);
        CHECK(std::getenv("MTEST_SELECT") == nullptr);
    }
    TEST_CASE("a program a case starts sees no protocol variable") {
        CHECK(std::system(command_seeing_protocol()) == 0);
    }
}
