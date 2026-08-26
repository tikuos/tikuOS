#!/bin/sh
# The board's interpreter on the host, held to its word: a known program
# gives a known transcript, a known effect, and a wrong program a
# bounded refusal -- because a runner that can hang is a runner nobody
# can put in a suite.
set -u
tool="$1"
box=$(mktemp -d)
prog="$box/p.bas"
fails=0
say() { printf '  [%s] basic   %s\n' "$1" "$2"; }

printf '10 PRINT "six by seven is "; 6*7\n20 VFSWRITE "/led", 1\n' > "$prog"
out=$("$tool" "$prog" "$box" 2>&1)
if [ "$out" = "six by seven is 42" ]; then say PASS "a program prints what it computes, once, with no prompt furniture"
else say FAIL "a program prints what it computes -- got: $out"; fails=1; fi
if [ "$(cat "$box/led" 2>/dev/null)" = "1" ]; then say PASS "and its VFSWRITE lands in the sandbox, where a test can hold it"
else say FAIL "VFSWRITE landed nowhere"; fails=1; fi

printf '10 GOTO 10\n' > "$prog"
"$tool" "$prog" "$box" >/dev/null 2>&1
if [ $? -eq 3 ]; then say PASS "an endless program is stopped and SAYS so, rather than sat in"
else say FAIL "an endless program was not refused"; fails=1; fi

rm -rf "$box"
if [ $fails -ne 0 ]; then echo; echo FAILURES; exit 1; fi
echo; echo "the interpreter keeps its word on the host"
