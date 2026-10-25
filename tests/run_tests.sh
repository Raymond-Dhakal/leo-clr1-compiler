#!/usr/bin/env bash
#
# Leo test runner.  Usage:  tests/run_tests.sh [group ...]     (or: make test)
#
# A test case is a Leo source file  tests/cases/<group>/<name>.leo  plus
# optional sidecar files with the same base name:
#
#   <name>.src      path (relative to the repo root) of the source to compile
#                   instead of <name>.leo (lets a test reuse a file from examples/)
#   <name>.flags    extra leoc command-line flags (default: none)
#   <name>.in       text fed to standard input
#   <name>.out      expected standard output, compared exactly
#   <name>.outhas   lines that must each appear in standard output
#   <name>.err      lines that must each appear in standard error
#   <name>.code     expected exit status (default 0)
#
# Groups whose name starts with "e2e" are executed three times: normally, with
# --no-opt, and with --interp (the built in IR interpreter instead of GCC); all
# runs must produce the same expected output.
# Nothing is reported as passed unless it was actually executed.
#
set -u
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"
LEOC="${LEOC:-$ROOT/leoc}"
CLR="${CLR:-$ROOT/clr_parser}"
TMP="$(mktemp -d "${TMPDIR:-/tmp}/novatest.XXXXXX")"
trap 'rm -rf "$TMP"' EXIT

pass=0; fail=0; failed=()

report() {   # report <ok:0|1> <label> <detail>
    if [ "$1" = 0 ]; then pass=$((pass+1)); [ "${VERBOSE:-0}" = 1 ] && echo "  PASS  $2"
    else fail=$((fail+1)); failed+=("$2"); echo "  FAIL  $2"; [ -n "${3:-}" ] && printf '%s\n' "$3" | sed 's/^/        /'; fi
}

run_case() {   # run_case <group> <base path without .leo> <extra flags>
    local group="$1" base="$2" extra="$3" name; name="$(basename "$base")"
    local src="$base.leo"
    [ -f "$base.src" ] && src="$ROOT/$(tr -d '\n' < "$base.src")"
    local flags=""; [ -f "$base.flags" ] && flags="$(tr '\n' ' ' < "$base.flags")"
    local input=/dev/null; [ -f "$base.in" ] && input="$base.in"
    local want_code=0; [ -f "$base.code" ] && want_code="$(tr -d '[:space:]' < "$base.code")"
    local label="$group/$name${extra:+ [$extra]}"

    # shellcheck disable=SC2086
    # a hung program must fail the test, not the whole run (timeout signals the process group)
  timeout -k 2 60 "$LEOC" "$src" $flags $extra --outdir "$TMP/out" < "$input" > "$TMP/stdout" 2> "$TMP/stderr"
    local code=$? problems=""

    [ "$code" = "$want_code" ] || problems+="exit status $code, expected $want_code"$'\n'
    if [ -f "$base.out" ] && ! diff -u "$base.out" "$TMP/stdout" > "$TMP/diff"; then
        problems+="stdout differs from $base.out:"$'\n'"$(head -20 "$TMP/diff")"$'\n'
    fi
    if [ -f "$base.outhas" ]; then
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            grep -qF -- "$line" "$TMP/stdout" || problems+="stdout lacks: $line"$'\n'
        done < "$base.outhas"
    fi
    if [ -f "$base.err" ]; then
        while IFS= read -r line; do
            [ -z "$line" ] && continue
            grep -qF -- "$line" "$TMP/stderr" || problems+="stderr lacks: $line"$'\n'"  (stderr was: $(head -5 "$TMP/stderr" | tr '\n' '|'))"$'\n'
        done < "$base.err"
    fi
    if [ -z "$problems" ]; then report 0 "$label"; else report 1 "$label" "$problems"; fi
}

check() {   # check <label> <condition-command...>   (for the generator tests below)
    local label="$1"; shift
    if "$@" > "$TMP/check.out" 2>&1; then report 0 "$label"; else report 1 "$label" "$(head -5 "$TMP/check.out")"; fi
}

[ -x "$LEOC" ] || { echo "leoc not built (run 'make' first)"; exit 2; }

want_group() {   # want_group <name>: true if no groups were given or name is listed
    [ $# -ge 1 ] || return 0
    local g="$1"; shift
    [ $# -eq 0 ] && return 0
    for w in "$@"; do [ "$w" = "$g" ] && return 0; done
    return 1
}

GROUPS_REQUESTED=("$@")

for dir in "$ROOT"/tests/cases/*/; do
    group="$(basename "$dir")"
    want_group "$group" "${GROUPS_REQUESTED[@]+"${GROUPS_REQUESTED[@]}"}" || continue
    echo "== $group"
    for leo in "$dir"*.leo "$dir"*.src; do
        [ -e "$leo" ] || continue
        base="${leo%.*}"
        # a case with both .leo and .src is visited once
        [ "$leo" = "$base.src" ] && [ -f "$base.leo" ] && continue
        run_case "$group" "$base" ""
        case "$group" in e2e*) run_case "$group" "$base" "--no-opt"; run_case "$group" "$base" "--interp";; esac
    done
done

# ---------- tests of the original CLR(1) generator and of the Leo grammar ----------
if want_group generator "${GROUPS_REQUESTED[@]+"${GROUPS_REQUESTED[@]}"}"; then
    echo "== generator"
    if [ -x "$CLR" ]; then
        g="$ROOT/tests/grammars"
        check "clr: S->AA, aA|b has 10 canonical states" bash -c "'$CLR' '$g/simple_grammar.txt' | grep -q 'Canonical LR(1) Collection (10 states)'"
        check "clr: expression grammar has 22 states, no conflicts" bash -c "'$CLR' '$g/expression_grammar.txt' | grep -q '(22 states)' && '$CLR' '$g/expression_grammar.txt' | grep -q 'None. Grammar is Canonical-LR(1)'"
        check "clr: epsilon grammar has 7 states" bash -c "'$CLR' '$g/epsilon_grammar.txt' | grep -q '(7 states)'"
        check "clr: dangling-else grammar reports exactly 1 conflict" bash -c "'$CLR' '$g/conflict_grammar.txt' | grep -q 'Conflicts (1)'"
        check "clr: accepts 'id + id * id'" bash -c "'$CLR' '$g/expression_grammar.txt' 'id + id * id' | grep -q 'Result: ACCEPTED'"
        check "clr: accepts '( id + id ) * id'" bash -c "'$CLR' '$g/expression_grammar.txt' '( id + id ) * id' | grep -q 'Result: ACCEPTED'"
        check "clr: rejects 'id id'" bash -c "'$CLR' '$g/expression_grammar.txt' 'id id' 2>/dev/null | grep -q 'Result: REJECTED'"
        check "clr: rejects '( id + id'" bash -c "'$CLR' '$g/expression_grammar.txt' '( id + id' 2>/dev/null | grep -q 'Result: REJECTED'"
        check "clr: epsilon grammar accepts 'b' and 'a a b'" bash -c "'$CLR' '$g/epsilon_grammar.txt' 'b' | grep -q ACCEPTED && '$CLR' '$g/epsilon_grammar.txt' 'a a b' | grep -q ACCEPTED"
        check "clr: verbose trace ends with accept" bash -c "'$CLR' '$g/expression_grammar.txt' 'id + id' -v | grep -q '^0 1 .*accept'"
    else
        report 1 "clr_parser binary missing (run make)"
    fi
    tiny="$TMP/tiny.leo"; printf 'PRINT 1\n' > "$tiny"
    check "Leo grammar: 856 canonical states, 0 conflicts" bash -c "'$LEOC' '$tiny' --dump-states --tokens 2>&1 | grep -q . && '$LEOC' '$tiny' --ast >/dev/null 2>'$TMP/st' ; grep -q '856 LR(1) states, 0 conflicts' '$TMP/st'"
    # a grammar with a duplicated production is ambiguous: leoc must refuse it (exit 5)
    sed 's/^Primary   -> ( Expr ) | id |/Primary   -> ( Expr ) | id | id |/' "$ROOT/language/grammar.txt" > "$TMP/ambiguous.txt"
    check "ambiguous grammar is refused with exit 5 and a reduce-reduce report" bash -c "'$LEOC' '$tiny' --grammar '$TMP/ambiguous.txt' >'$TMP/o' 2>'$TMP/e'; [ \$? = 5 ] && grep -q 'reduce-reduce' '$TMP/o' && grep -q 'not CLR(1)' '$TMP/e'"
    # a production without a semantic action is a grammar/AST-builder mismatch (exit 5)
    { cat "$ROOT/language/grammar.txt"; echo 'Stmt -> PRINT Expr Expr'; } > "$TMP/unbound.txt"
    check "grammar production without semantic action is refused with exit 5" bash -c "'$LEOC' '$tiny' --grammar '$TMP/unbound.txt' >/dev/null 2>'$TMP/e'; [ \$? = 5 ] && grep -q 'no semantic action' '$TMP/e'"
    check "web/grammar.txt is identical to language/grammar.txt" cmp -s "$ROOT/language/grammar.txt" "$ROOT/web/grammar.txt"
    check "missing grammar file is reported" bash -c "'$LEOC' '$tiny' --grammar /nonexistent.txt >/dev/null 2>'$TMP/e'; [ \$? = 5 ] && grep -q 'cannot load grammar' '$TMP/e'"
    check "source file is never overwritten (output dir == source dir, .c input)" bash -c "cp '$tiny' '$TMP/clash.c'; '$LEOC' '$TMP/clash.c' --outdir '$TMP' >/dev/null 2>&1; cmp -s '$TMP/clash.c' '$tiny'"
    check "missing input file gives exit 1" bash -c "'$LEOC' /nonexistent.leo >/dev/null 2>&1; [ \$? = 1 ]"
    check "unknown option gives exit 1" bash -c "'$LEOC' '$tiny' --bogus >/dev/null 2>&1; [ \$? = 1 ]"
    check "file name with spaces and shell characters builds and runs" bash -c "mkdir -p '$TMP/we ird;dir' && printf 'PRINT 41 + 1\n' > '$TMP/we ird;dir/p q\$x.leo' && [ \"\$('$LEOC' '$TMP/we ird;dir/p q\$x.leo' --run -q --outdir '$TMP/we ird;dir/out')\" = 42 ]"
    check "-o places the executable where asked" bash -c "'$LEOC' '$tiny' -q -o '$TMP/custom/bin/prog' --outdir '$TMP/o2' && [ -x '$TMP/custom/bin/prog' ] && [ -f '$TMP/o2/tiny.c' ]"
    check "--emit-c writes the C file but builds no executable" bash -c "rm -rf '$TMP/o3'; '$LEOC' '$tiny' -q --emit-c --outdir '$TMP/o3' >/dev/null && [ -f '$TMP/o3/tiny.c' ] && [ ! -e '$TMP/o3/tiny' ]"
    check "--cc with a missing compiler is reported (exit 6)" bash -c "'$LEOC' '$tiny' --cc /nonexistent/cc --outdir '$TMP/o4' >/dev/null 2>'$TMP/e'; [ \$? = 6 ] && grep -q 'could not run the C compiler' '$TMP/e'"
fi

echo
echo "passed: $pass   failed: $fail"
if [ "$fail" -gt 0 ]; then printf 'failed: %s\n' "${failed[@]}"; exit 1; fi
exit 0
