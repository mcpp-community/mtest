# mtest.sh -- the mtest protocol (spec/protocol.md) for a Bash test script, level 3.
#
#   #!/usr/bin/env bash
#   # mtest-protocol=1          <- the marker (P2.3): a runner reads it from the script
#   . "$(dirname "$0")/mtest.sh"
#   reads_a_file() { mtest_check "the file is there" test -f data.txt; }
#   mtest_case "fs/reads a file" reads_a_file --tag fast --artifact file:data.txt
#   mtest_report                  # writes `done' and exits with the run's status
#
# A case is a function; it fails when it calls mtest_fail, when an mtest_check does not hold, or when
# it returns non-zero. mtest_skip ends it as skipped. With no runner the script prints one line per
# case. Programs a case starts receive none of the protocol's variables (P2.2).

_mtest_report="${MTEST_REPORT:-}"
_mtest_mode="${MTEST_MODE:-run}"
[ -n "${MTEST_PROTOCOL:-}" ] || _mtest_report=""
_mtest_select="${MTEST_SELECT:-}"
_mtest_failed=0
_mtest_seen=()
_mtest_case=""
_mtest_metrics=""  # `name=value` words of the case that runs, for its bounds
_mtest_case_failed=0
_mtest_started=$(date +%s%N 2>/dev/null || echo 0)
_mtest_counts=""   # `status=n` words: Bash 3.2 (macOS) has no associative arrays
# P2.2: what this script starts is not taken for the runner's.
unset MTEST_PROTOCOL MTEST_MODE MTEST_REPORT MTEST_SELECT MTEST_ISOLATION

_mtest_json() {   # a JSON string of $1 (RFC 8259 §7)
    local s="$1"
    s="${s//\\/\\\\}"; s="${s//\"/\\\"}"
    s="${s//$'\n'/\\n}"; s="${s//$'\r'/\\r}"; s="${s//$'\t'/\\t}"
    printf '"%s"' "$s"
}
_mtest_list() {   # a JSON array of the arguments
    local out="[" first=1 x
    for x in "$@"; do [ $first = 1 ] || out+=","; out+="$(_mtest_json "$x")"; first=0; done
    printf '%s]' "$out"
}
_mtest_event() { [ -n "$_mtest_report" ] && printf '{%s}\n' "$1" >> "$_mtest_report"; return 0; }
_mtest_ms() { local now; now=$(date +%s%N 2>/dev/null || echo 0); echo $(( (now - _mtest_started) / 1000000 )); }

_mtest_event '"e":"program","protocol":1,"level":3,"framework":"mtest.sh","version":"0.4.0","isolation":["process","case"]'

_mtest_selected() {
    [ -z "$_mtest_select" ] && return 0
    local line
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%$'\r'}"
        [ "$line" = "$1" ] && return 0
    done < "$_mtest_select"
    return 1
}

# mtest_case ID FUNCTION [--tag T] [--kind K] [--clause C] [--resource R] [--artifact A] [--timeout SECONDS]
#            [--isolate] [--retries N] [--diagnose-env NAME=VALUE] [--diagnose-timeout SECONDS]
#            [--at-most METRIC=VALUE] [--at-least METRIC=VALUE]
#            [--expect-fail REASON] [--skip REASON]
# A case that states no kind is of the program's (its directory's, docs/layout.md).
mtest_case() {
    local id="$1" fn="$2"; shift 2
    local tags=() clauses=() resources=() artifacts=() kind="" limit="" xfail="" skip=""
    local isolate="" retries="" diagnose=() diagnose_limit="" bounds=()
    while [ $# -gt 0 ]; do
        case "$1" in
            --tag) tags+=("$2"); shift 2 ;;
            --kind) kind="$2"; shift 2 ;;
            --clause) clauses+=("$2"); shift 2 ;;
            --resource) resources+=("$2"); shift 2 ;;
            --artifact) artifacts+=("$2"); shift 2 ;;
            --timeout) limit="$2"; shift 2 ;;
            --isolate) isolate=1; shift ;;
            --retries) retries="$2"; shift 2 ;;
            --diagnose-env) diagnose+=("$2"); shift 2 ;;
            --diagnose-timeout) diagnose_limit="$2"; shift 2 ;;
            --at-most) bounds+=("max:$2"); shift 2 ;;
            --at-least) bounds+=("min:$2"); shift 2 ;;
            --expect-fail) xfail="$2"; shift 2 ;;
            --skip) skip="$2"; shift 2 ;;
            *) echo "mtest_case: unknown option $1" >&2; shift ;;
        esac
    done
    _mtest_seen+=("$id")
    if [ "$_mtest_mode" = list ]; then
        local ev
        ev="\"e\":\"case\",\"id\":$(_mtest_json "$id"),\"name\":$(_mtest_json "$id"),\"file\":$(_mtest_json "${BASH_SOURCE[1]:-$0}"),\"line\":${BASH_LINENO[0]:-0},\"tags\":$(_mtest_list "${tags[@]}"),\"clauses\":$(_mtest_list "${clauses[@]}"),\"resources\":$(_mtest_list "${resources[@]}"),\"artifacts\":$(_mtest_list "${artifacts[@]}")"
        [ -n "$kind" ] && ev+=",\"kind\":$(_mtest_json "$kind")"
        [ -n "$limit" ] && ev+=",\"timeout\":$limit"
        [ -n "$isolate" ] && ev+=',"isolation":"case"'
        [ -n "$retries" ] && ev+=",\"retries\":$retries"
        if [ ${#diagnose[@]} -gt 0 ] || [ -n "$diagnose_limit" ]; then
            local env_fields="" kv
            for kv in ${diagnose[@]+"${diagnose[@]}"}; do
                [ -n "$env_fields" ] && env_fields+=","
                env_fields+="$(_mtest_json "${kv%%=*}"):$(_mtest_json "${kv#*=}")"
            done
            ev+=",\"diagnose\":{\"env\":{$env_fields}${diagnose_limit:+,\"timeout\":$diagnose_limit}}"
        fi
        if [ ${#bounds[@]} -gt 0 ]; then
            local list="" b
            for b in "${bounds[@]}"; do
                local side="${b%%:*}" spec="${b#*:}"
                list+="${list:+,}{\"metric\":$(_mtest_json "${spec%%=*}"),\"$side\":${spec#*=}}"
            done
            ev+=",\"bounds\":[$list]"
        fi
        [ -n "$skip" ] && ev+=',"expect":"skip"'
        [ -n "$xfail" ] && [ -z "$skip" ] && ev+=',"expect":"fail"'
        _mtest_event "$ev"
        return 0
    fi
    _mtest_selected "$id" || return 0
    _mtest_event "\"e\":\"begin\",\"id\":$(_mtest_json "$id"),\"t\":$(_mtest_ms)"
    local start status=pass reason=""
    start=$(_mtest_ms)
    if [ -n "$skip" ]; then
        status=skip; reason="$skip"
    else
        _mtest_case="$id"; _mtest_case_failed=0; _mtest_skip_reason=""; _mtest_metrics=""
        local rc=0
        "$fn" || rc=$?
        [ -z "$_mtest_skip_reason" ] && [ ${#bounds[@]} -gt 0 ] && _mtest_check_bounds "${bounds[@]}"
        if [ -n "$_mtest_skip_reason" ]; then status=skip; reason="$_mtest_skip_reason"
        elif [ $_mtest_case_failed -ne 0 ] || [ $rc -ne 0 ]; then
            status=fail
            [ $_mtest_case_failed -eq 0 ] && _mtest_fail_at "the case returned $rc" "${BASH_SOURCE[1]:-$0}" "${BASH_LINENO[0]:-0}"
        fi
        _mtest_case=""
    fi
    if [ -n "$xfail" ]; then
        if [ "$status" = fail ]; then status=xfail; reason="$xfail"; elif [ "$status" = pass ]; then status=xpass; reason="expected to fail: $xfail"; fi
    fi
    case "$status" in fail|xpass) _mtest_failed=1 ;; esac
    _mtest_count "$status"
    local ev="\"e\":\"end\",\"id\":$(_mtest_json "$id"),\"status\":\"$status\",\"ms\":$(( $(_mtest_ms) - start ))"
    [ -n "$reason" ] && ev+=",\"reason\":$(_mtest_json "$reason")"
    _mtest_event "$ev"
    case "$status" in
        pass) echo "  ok   $id" ;; skip) echo "  skip $id -- $reason" ;; xfail) echo "  xfail $id -- $reason" ;;
        *) echo "  FAIL $id${reason:+ -- $reason}" ;;
    esac
}

_mtest_count() {   # one more case of status $1
    local out="" found=0 w
    for w in $_mtest_counts; do
        if [ "${w%%=*}" = "$1" ]; then out+=" $1=$(( ${w#*=} + 1 ))"; found=1; else out+=" $w"; fi
    done
    [ $found = 1 ] || out+=" $1=1"
    _mtest_counts="$out"
}

_mtest_fail_at() {   # message file line [expected actual]
    _mtest_case_failed=1
    [ -n "$_mtest_case" ] || _mtest_failed=1
    local ev="\"e\":\"failure\",\"id\":$(_mtest_json "$_mtest_case"),\"file\":$(_mtest_json "$2"),\"line\":$3,\"message\":$(_mtest_json "$1")"
    [ $# -ge 5 ] && ev+=",\"expected\":$(_mtest_json "$4"),\"actual\":$(_mtest_json "$5")"
    _mtest_event "$ev"
    echo "    $2:$3: $1"
}
# mtest_fail MESSAGE: the case fails here.
mtest_fail() { _mtest_fail_at "$1" "${BASH_SOURCE[1]:-$0}" "${BASH_LINENO[0]:-0}"; }
# mtest_eq ACTUAL EXPECTED [MESSAGE]: the two strings are equal.
mtest_eq() {
    [ "$1" = "$2" ] && return 0
    _mtest_fail_at "${3:-$(printf '%q == %q' "$1" "$2")}" "${BASH_SOURCE[1]:-$0}" "${BASH_LINENO[0]:-0}" "$2" "$1"
    return 1
}
# mtest_check DESCRIPTION COMMAND...: the command succeeds.
mtest_check() {
    local what="$1"; shift
    "$@" && return 0
    _mtest_fail_at "$what (exit $?: $*)" "${BASH_SOURCE[1]:-$0}" "${BASH_LINENO[0]:-0}"
    return 1
}
# mtest_skip REASON: the case ends skipped (return after it).
mtest_skip() { _mtest_skip_reason="$1"; }
# P6.5: each bound (`max:NAME=V`, `min:NAME=V`) on the metrics the case reported.
_mtest_check_bounds() {
    local b side spec name limit value
    for b in "$@"; do
        side="${b%%:*}"; spec="${b#*:}"; name="${spec%%=*}"; limit="${spec#*=}"
        value=$(printf '%s\n' $_mtest_metrics | awk -F= -v n="$name" '$1 == n { v = $2 } END { print v }')
        if [ -z "$value" ]; then
            _mtest_fail_at "the case reported no metric \`$name\`, which it bounds" "${BASH_SOURCE[2]:-$0}" "${BASH_LINENO[1]:-0}"
        elif awk -v v="$value" -v l="$limit" -v s="$side" 'BEGIN { exit !((s == "max" && v > l) || (s == "min" && v < l)) }'; then
            _mtest_fail_at "metric $name = $value, $([ "$side" = max ] && echo over || echo under) its bound $limit ($([ "$side" = max ] && echo at_most || echo at_least))" "${BASH_SOURCE[2]:-$0}" "${BASH_LINENO[1]:-0}"
        fi
    done
}
# mtest_metric NAME VALUE UNIT [SAMPLE...]: a metric of the case (P6).
mtest_metric() {
    local name="$1" value="$2" unit="$3"; shift 3
    _mtest_metrics+=" $name=$value"
    local samples="" x
    for x in "$@"; do samples+="${samples:+,}$x"; done
    _mtest_event "\"e\":\"metric\",\"id\":$(_mtest_json "$_mtest_case"),\"name\":$(_mtest_json "$name"),\"value\":$value,\"unit\":$(_mtest_json "$unit")${samples:+,\"samples\":[$samples]}"
}
# mtest_report: `done', and the script's exit status (P5.5).
mtest_report() {
    if [ "$_mtest_mode" != list ] && [ -n "$_mtest_select" ]; then
        local line seen x
        while IFS= read -r line || [ -n "$line" ]; do
            line="${line%$'\r'}"; [ -n "$line" ] || continue
            seen=0; for x in "${_mtest_seen[@]}"; do [ "$x" = "$line" ] && seen=1; done
            if [ $seen = 0 ]; then _mtest_event "\"e\":\"missing\",\"id\":$(_mtest_json "$line")"; _mtest_failed=1; echo "  FAIL $line -- no such case"; fi
        done < "$_mtest_select"
    fi
    local counts="" w
    for w in $_mtest_counts; do counts+="${counts:+,}\"${w%%=*}\":${w#*=}"; done
    _mtest_event "\"e\":\"done\",\"status\":\"$([ $_mtest_failed = 0 ] && echo pass || echo fail)\",\"counts\":{$counts}"
    [ "$_mtest_mode" = list ] && exit 0
    exit $_mtest_failed
}
