/* mtest.h -- the mtest protocol (spec/protocol.md) for any language with a C foreign-function
 * interface.
 *
 * Each call writes one event of the protocol when a runner asked for them (MTEST_PROTOCOL,
 * MTEST_REPORT), and does nothing otherwise. A program declares its cases, runs the selected ones,
 * and ends with mtest_done(), whose result is the program's exit status:
 *
 *     mtest_init("my-framework", 3);
 *     if (mtest_listing()) { mtest_case("fs/reads", __FILE__, __LINE__, "unit"); return
 * mtest_done(); } if (mtest_selected("fs/reads")) { mtest_begin("fs/reads"); if (!ok)
 * mtest_failure("fs/reads", __FILE__, __LINE__, "the file is missing", NULL, NULL);
 *         mtest_end("fs/reads", ok ? "pass" : "fail", NULL);
 *     }
 *     return mtest_done();
 *
 * Strings are UTF-8. A program that links this library carries the protocol's marker (P2.3). */
#ifndef MTEST_H
#define MTEST_H

#ifdef __cplusplus
extern "C" {
#endif

/* Writes the `program` event: the framework's name and its level (1 to 3). Call it first. */
void mtest_init(const char* framework, int level);
/* Whether the runner asked for the list of cases (MTEST_MODE=list). */
int mtest_listing(void);
/* A case of the list (P3). `kind` is unit, e2e, cts or bench; NULL is unit. */
void mtest_case(const char* id, const char* file, int line, const char* kind);
/* An attribute of the case last listed: a tag, a clause, a resource, an artifact (level 3). */
void mtest_case_tag(const char* tag);
void mtest_case_clause(const char* clause);
void mtest_case_resource(const char* resource);
void mtest_case_artifact(const char* artifact);
/* The case last listed is expected to fail (level 3). */
void mtest_case_expect_fail(void);
/* Whether the case runs (MTEST_SELECT); every case runs when the runner selected none (P4). */
int mtest_selected(const char* id);
void mtest_begin(const char* id);
/* An expectation that did not hold; `expected` and `actual` may be NULL. */
void mtest_failure(const char* id, const char* file, int line, const char* message,
                   const char* expected, const char* actual);
/* A metric (P6); `samples` may be NULL with `count` 0. */
void mtest_metric(const char* id, const char* name, double value, const char* unit,
                  const double* samples, int count);
/* The case's end: pass, fail, skip, xfail, xpass or invalid; `reason` may be NULL. */
void mtest_end(const char* id, const char* status, const char* reason);
/* The `missing` events of selected identifiers no case had, the `done` event, and the exit status
 * (P5.5). */
int mtest_done(void);

#ifdef __cplusplus
}
#endif

#endif /* MTEST_H */
