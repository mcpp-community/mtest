// A program that counts its allocations (bench::allocated from its own operator new): a call's are
// the body's -- what the lap allocates to keep what the call said, and what a call paused for, are
// not. A program of its own, since it replaces the global operator new.
import std;
import mcpplibs.mtest;
import mcpplibs.mtest.bench;

void* operator new(std::size_t size) {
    mcpplibs::mtest::bench::allocated(size);
    if (void* memory = std::malloc(size ? size : 1))
        return memory;
    std::abort();
}
void* operator new[](std::size_t size) {
    return ::operator new(size);
}
void operator delete(void* memory) noexcept {
    std::free(memory);
}
void operator delete[](void* memory) noexcept {
    std::free(memory);
}
void operator delete(void* memory, std::size_t) noexcept {
    std::free(memory);
}
void operator delete[](void* memory, std::size_t) noexcept {
    std::free(memory);
}

using namespace mcpplibs::mtest;

int main() {
    "a call that allocates nothing reports none, though it states parts and quantities"_test
        [kind::bench] = [] {
            std::uint64_t turn = 0;
            bench::laps run{"frame", {.least = 40, .most = 40, .warmup = 2}};
            while (run.next()) {
                bench::keep(++turn);
                run.part("draw", std::chrono::microseconds{3});
                run.value("nodes", double(turn));
                run.value(turn % 2 ? "odd" : "even",
                          1);  // a name first said late: a column made then
            }
            const bench::distribution& d = run.result();
            const auto counted =
                std::ranges::find(d.values, std::string("allocations"), &bench::quantity::name);
            expect(counted != d.values.end()) << "the program counts, so each call's are stated";
            if (counted != d.values.end())
                expect(eq(counted->max, 0.0)) << "what the lap keeps is not the call's";
        };
    "a call's own allocations are counted, and what it paused for is not"_test[kind::bench] = [] {
        bench::laps run{"work", {.least = 10, .most = 10, .warmup = 1}};
        while (run.next()) {
            run.pause();
            auto setup = std::make_unique<int>(1);  // not the call's
            run.resume();
            auto made = std::make_unique<std::array<int, 8>>();
            bench::keep(made);
            bench::keep(setup);
        }
        const bench::distribution& d = run.result();
        const auto counted =
            std::ranges::find(d.values, std::string("allocations"), &bench::quantity::name);
        const auto bytes =
            std::ranges::find(d.values, std::string("allocated"), &bench::quantity::name);
        expect(counted != d.values.end() && bytes != d.values.end());
        if (counted != d.values.end() && bytes != d.values.end())
            expect(eq(counted->min, 1.0) && eq(counted->max, 1.0) &&
                   eq(bytes->max, double(sizeof(std::array<int, 8>))));
    };
    return report();
}
