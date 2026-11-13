/* The conformance fixture of the C ABI (spec/conformance.md), level 3. */
#include <math.h>
#include <stddef.h>

#include "../include/mtest.h"

static void run(const char* id, const char* status, const char* reason, int fail_line) {
    if (!mtest_selected(id))
        return;
    mtest_begin(id);
    if (fail_line)
        mtest_failure(id, __FILE__, fail_line, "1 == 2", "2", "1");
    if (id[0] == 'm' && id[1] == 'e') {
        const double samples[] = {0.4, 0.5, 0.6};
        mtest_metric(id, "elapsed", 0.5, "s", samples, 3);
    }
    if (id[0] == 'b') {
        mtest_bound(id, "elapsed", NAN, 0.1);
        mtest_metric(id, "elapsed", 0.5, "s", NULL, 0);
    }
    mtest_end(id, status, reason);
}

int main(void) {
    mtest_init("mtest-capi", 3);
    if (mtest_listing()) {
        mtest_case("passes", __FILE__, __LINE__, "unit");
        mtest_case_tag("fast");
        mtest_case("fails", __FILE__, __LINE__, "unit");
        mtest_case("skips", __FILE__, __LINE__, "unit");
        mtest_case("expected to fail", __FILE__, __LINE__, "unit");
        mtest_case_expect_fail();
        mtest_case("group/inner", __FILE__, __LINE__, "unit");
        mtest_case("measures", __FILE__, __LINE__, "bench");
        mtest_case("covers", __FILE__, __LINE__, "cts");
        mtest_case_clause("P3.1");
        mtest_case("holds", __FILE__, __LINE__, "unit");
        mtest_case_resource("serial:db");
        mtest_case_artifact("file:data/x.txt");
        mtest_case("placed", __FILE__, __LINE__, "unit");
        mtest_case_only("wasm32");
        mtest_case("bounded", __FILE__, __LINE__, NULL);
        mtest_bound("bounded", "elapsed", NAN, 0.1);
        return mtest_done();
    }
    run("passes", "pass", NULL, 0);
    run("fails", "fail", NULL, __LINE__);
    run("skips", "skip", "not here", 0);
    run("expected to fail", "xfail", "on purpose", __LINE__);
    run("group/inner", "pass", NULL, 0);
    run("measures", "pass", NULL, 0);
    run("covers", "pass", NULL, 0);
    run("holds", "pass", NULL, 0);
    /* The C ABI lists places and leaves the decision to its program: this one runs nowhere else. */
    run("placed", "skip", "only on wasm32", 0);
    run("bounded", "pass", NULL, 0); /* its bound fails it */
    return mtest_done();
}
