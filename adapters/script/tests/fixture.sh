#!/usr/bin/env bash
# mtest-protocol=1
# The conformance fixture of mtest.sh (spec/conformance.md).
. "$(dirname "$0")/../mtest.sh"
passes() { :; }
fails() { mtest_eq 1 2; }
skips() { mtest_skip "not here"; }
xfails() { mtest_fail "on purpose"; }
measures() { mtest_metric elapsed 0.5 s 0.4 0.5 0.6; }
nothing() { :; }
mtest_case "passes" passes --tag fast
mtest_case "fails" fails
mtest_case "skips" skips
mtest_case "expected to fail" xfails --expect-fail "on purpose"
mtest_case "group/inner" nothing
mtest_case "measures" measures --kind bench
mtest_case "covers" nothing --kind cts --clause P3.1
mtest_case "holds" nothing --resource serial:db --artifact file:data/x.txt
slow() { mtest_metric elapsed 0.5 s; }
mtest_case "bounded" slow --at-most elapsed=0.1
mtest_report
