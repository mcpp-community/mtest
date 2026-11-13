"""mtest_report -- the mtest protocol (spec/protocol.md) for a Python test program, level 3.

    #!/usr/bin/env python3
    # mtest-protocol=1                <- the marker (P2.3): a runner reads it from the file
    import mtest_report as mt

    @mt.case("fs/reads a file", tags=["fast"], artifacts=["file:data.txt"])
    def reads():
        mt.check(open("data.txt").read() == "x", "the file holds x")

    mt.report()                       # writes `done' and exits with the run's status

A case fails when a check does not hold, when it calls fail(), or when an exception escapes it;
skip() ends it skipped. With no runner the program prints one line per case. Programs a case starts
receive none of the protocol's variables (P2.2).
"""
import inspect
import json
import os
import platform
import sys
import time

_report = os.environ.get("MTEST_REPORT") if os.environ.get("MTEST_PROTOCOL") else None
_mode = os.environ.get("MTEST_MODE", "run")
_select_file = os.environ.get("MTEST_SELECT")
for _v in ("MTEST_PROTOCOL", "MTEST_MODE", "MTEST_REPORT", "MTEST_SELECT", "MTEST_ISOLATION"):
    os.environ.pop(_v, None)
_select = None
if _report and _select_file:
    with open(_select_file, encoding="utf-8") as f:
        _select = {line.rstrip("\r\n") for line in f if line.strip()}
_started = time.monotonic()
_failed = False
_seen = []
_counts = {}
_current = None


class _Fatal(Exception):
    pass


class _Skip(Exception):
    pass


def _event(**fields):
    if _report:
        with open(_report, "a", encoding="utf-8") as f:
            f.write(json.dumps(fields, ensure_ascii=False) + "\n")


_event(e="program", protocol=1, level=3, framework="mtest_report.py", version="0.2.0", isolation=["process", "case"])


def _where(depth=2):
    frame = inspect.stack()[depth]
    return frame.filename, frame.lineno


def _failure(message, expected=None, actual=None, fatal=False, depth=3):
    global _failed
    file, line = _where(depth)
    fields = dict(e="failure", id=_current["id"] if _current else "", file=file, line=line, message=message)
    if expected is not None or actual is not None:
        fields.update(expected=str(expected), actual=str(actual))
    if fatal:
        fields["fatal"] = True
    _event(**fields)
    print(f"    {file}:{line}: {message}")
    if _current is not None:
        _current["failed"] = True
    else:
        _failed = True


def check(condition, message="the check did not hold"):
    """The case fails when `condition` is false."""
    if not condition:
        _failure(message)
    return bool(condition)


def eq(actual, expected, message=None):
    """The case fails when the two differ, naming both."""
    if actual != expected:
        _failure(message or f"{actual!r} == {expected!r}", expected=repr(expected), actual=repr(actual))
        return False
    return True


def fail(message):
    """The case fails here, and ends."""
    _failure(message, fatal=True)
    raise _Fatal()


def skip(reason):
    """The case ends skipped."""
    raise _Skip(reason)


def metric(name, value, unit, samples=(), better="lower"):
    """A metric of the current case (P6)."""
    if _current is not None:
        _current["metrics"][name] = value
    fields = dict(e="metric", id=_current["id"] if _current else "", name=name, value=value, unit=unit, better=better)
    if samples:
        fields["samples"] = list(samples)
    _event(**fields)


def _bounds_json(at_most, at_least):
    out = [{"metric": m, "max": v} for m, v in at_most.items()]
    out += [{"metric": m, "min": v} for m, v in at_least.items()]
    return out


def _check_bounds(at_most, at_least):
    """P6.5: each bound on the metrics the case reported, as an expectation at its end."""
    reported = _current["metrics"]
    for name, limit, side in [(m, v, "max") for m, v in at_most.items()] + [(m, v, "min") for m, v in at_least.items()]:
        if name not in reported:
            _failure(f"the case reported no metric `{name}`, which it bounds", depth=3)
        elif side == "max" and reported[name] > limit:
            _failure(f"metric {name} = {reported[name]:g}, over its bound {limit:g} (at_most)", depth=3)
        elif side == "min" and reported[name] < limit:
            _failure(f"metric {name} = {reported[name]:g}, under its bound {limit:g} (at_least)", depth=3)


_ARCHITECTURES = ("x86_64", "aarch64", "riscv64", "wasm32")


def _host():
    """This host's platform and architecture, in mcpplibs.os's names."""
    system = {"Linux": "linux", "Darwin": "macos", "Windows": "windows"}.get(platform.system(), platform.system().lower())
    machine = platform.machine()
    arch = {"amd64": "x86_64", "AMD64": "x86_64", "arm64": "aarch64", "ARM64": "aarch64"}.get(machine, machine)
    return system, arch


def _place_skip(only, excepted):
    """Why a case with these places does not run here, or None."""
    system, arch = _host()
    here = f"{system} {arch}"
    platforms = [w for w in only if w not in _ARCHITECTURES]
    archs = [w for w in only if w in _ARCHITECTURES]
    if (platforms and system not in platforms) or (archs and arch not in archs):
        return f"only on {', '.join(only)} (this host: {here})"
    for w in excepted:
        if w in (system, arch):
            return f"not on {w} (this host: {here})"
    return None


def case(id, tags=(), kind=None, clauses=(), resources=(), artifacts=(), expect_fail=None, skip_reason=None,
         timeout=None, isolate=False, retries=None, diagnose=None, at_most=None, at_least=None, only=(), excepted=()):
    """Declares a case: lists it, runs it, or passes over it, as the runner asks. A case that states no
    kind is of its program's (its directory's, docs/layout.md). `timeout` (seconds), `isolate`,
    `retries` and `diagnose` ({"env": {...}, "timeout": seconds}) are how a runner runs it (P3.3);
    `at_most` and `at_least` ({metric: value}) bound its metrics (P6.5). `only` and `excepted` name
    platforms (linux, macos, windows) and architectures (x86_64, aarch64, riscv64): the case runs only
    on one of the platforms and one of the architectures `only` names, when it names any, and on none
    `excepted` names; elsewhere it is skipped."""
    at_most, at_least = dict(at_most or {}), dict(at_least or {})
    skip_reason = skip_reason or _place_skip(list(only), list(excepted))
    def declare(fn):
        global _failed, _current
        _seen.append(id)
        file, line = inspect.getsourcefile(fn) or "", inspect.getsourcelines(fn)[1]
        if _mode == "list":
            fields = dict(e="case", id=id, name=id, file=file, line=line, tags=list(tags), clauses=list(clauses),
                          resources=list(resources), artifacts=list(artifacts))
            if kind:
                fields["kind"] = kind
            if timeout:
                fields["timeout"] = timeout
            if isolate:
                fields["isolation"] = "case"
            if retries:
                fields["retries"] = retries
            if diagnose:
                fields["diagnose"] = diagnose
            if at_most or at_least:
                fields["bounds"] = _bounds_json(at_most, at_least)
            if only:
                fields["only"] = list(only)
            if excepted:
                fields["except"] = list(excepted)
            if skip_reason:
                fields["expect"] = "skip"
                fields["reason"] = skip_reason
            elif expect_fail:
                fields["expect"] = "fail"
            _event(**fields)
            return fn
        if _select is not None and id not in _select:
            return fn
        _event(e="begin", id=id, t=(time.monotonic() - _started) * 1000)
        start = time.monotonic()
        status, reason = "pass", ""
        if skip_reason:
            status, reason = "skip", skip_reason
        else:
            _current = {"id": id, "failed": False, "metrics": {}}
            try:
                fn()
                _check_bounds(at_most, at_least)
            except _Fatal:
                pass
            except _Skip as s:
                status, reason = "skip", str(s)
            except Exception as e:  # noqa: BLE001 -- any exception escaping a case is its failure
                _failure(f"an exception escaped the case: {type(e).__name__}: {e}", depth=2)
            if status != "skip" and _current["failed"]:
                status = "fail"
            _current = None
        if expect_fail:
            if status == "fail":
                status, reason = "xfail", expect_fail
            elif status == "pass":
                status, reason = "xpass", f"expected to fail: {expect_fail}"
        if status in ("fail", "xpass"):
            _failed = True
        _counts[status] = _counts.get(status, 0) + 1
        end = dict(e="end", id=id, status=status, ms=(time.monotonic() - start) * 1000)
        if reason:
            end["reason"] = reason
        _event(**end)
        mark = {"pass": "  ok  ", "skip": "  skip", "xfail": "  xfail"}.get(status, "  FAIL")
        print(f"{mark} {id}{' -- ' + reason if reason else ''}")
        return fn
    return declare


def report():
    """`done', and the program's exit status (P5.5)."""
    global _failed
    if _mode != "list" and _select is not None:
        for want in sorted(_select - set(_seen)):
            _event(e="missing", id=want)
            print(f"  FAIL {want} -- no such case")
            _failed = True
    _event(e="done", status="fail" if _failed else "pass", counts=_counts)
    sys.exit(0 if _mode == "list" else (1 if _failed else 0))
