#!/usr/bin/env python3
# mtest-protocol=1
"""The conformance fixture of mtest_report.py (spec/conformance.md)."""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import mtest_report as mt  # noqa: E402


@mt.case("passes", tags=["fast"])
def passes():
    pass


@mt.case("fails")
def fails():
    mt.eq(1, 2)


@mt.case("skips")
def skips():
    mt.skip("not here")


@mt.case("expected to fail", expect_fail="on purpose")
def xfails():
    mt.fail("on purpose")


@mt.case("group/inner")
def inner():
    pass


@mt.case("measures", kind="bench")
def measures():
    mt.metric("elapsed", 0.5, "s", [0.4, 0.5, 0.6])


@mt.case("covers", kind="cts", clauses=["P3.1"])
def covers():
    pass


@mt.case("holds", resources=["serial:db"], artifacts=["file:data/x.txt"])
def holds():
    pass


@mt.case("placed", only=["wasm32"])
def placed():
    pass


@mt.case("bounded", at_most={"elapsed": 0.1})
def bounded():
    mt.metric("elapsed", 0.5, "s")


mt.report()
