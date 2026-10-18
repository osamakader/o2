#!/bin/sh
# Build and test one arch, as CI does.
#
#   tools/ci.sh arm64|riscv64 [quick]
#
# Boots the self-test build, then runs o2tests, xv6's usertests (-q with
# quick), stressfs, ^C, line editing and network checks and a timed
# grind in the shell; on arm64 also o2tests with ARM64_ASID=1. The
# network check needs python3 on the host. Exits non-zero if anything fails or the
# kernel panics. Logs are kept in build/ci/<arch>-<step>.log.

ARCH=${1:?usage: tools/ci.sh arm64|riscv64 [quick]}
QUICK=${2:-}

cd "$(dirname "$0")/.." || exit 1
LOGS=build/ci
mkdir -p "$LOGS"

# boot delay before typing, and per-line delay: the console only
# buffers 128 characters of unread input
DELAY=8
LINE=2

if [ -n "$QUICK" ]; then
    UTARGS="-q"
    GRIND=60
else
    UTARGS=""
    GRIND=300
fi

failed=0

# step name: record the outcome of a step
pass() { echo "PASS  $ARCH $1"; }
fail() {
    echo "FAIL  $ARCH $1: $2"
    echo "----- last lines of $LOGS/$ARCH-$1.log"
    tail -n 25 "$LOGS/$ARCH-$1.log"
    echo "-----"
    failed=1
}

# run name timeout pattern [run.sh options...] -- input lines...
# Boots a fresh fs.img, types the input lines into the shell and waits
# for pattern (or panic, or the timeout).
run() {
    name=$1 timeout=$2 pattern=$3
    shift 3
    opts=
    while [ $# -gt 0 ] && [ "$1" != "--" ]; do
        opts="$opts $1"
        shift
    done
    shift
    input=$LOGS/$ARCH-$name.in
    : > "$input"
    for line in "$@"; do
        printf '%s\n' "$line" >> "$input"
    done
    tools/run.sh -f -a "$ARCH" -t "$timeout" -i "$PWD/$input" -d $DELAY -l $LINE \
        -e "$pattern|panic:" $opts > "$LOGS/$ARCH-$name.log" 2>&1
}

# log name has pattern?
has() { grep -qE -- "$2" "$LOGS/$ARCH-$1.log"; }

panicked() { has "$1" "panic:"; }

# 1. kernel self-tests: they run at boot, before the shell starts
tools/run.sh -s -f -a "$ARCH" -t 120 -e "init: starting sh|panic:" \
    > "$LOGS/$ARCH-selftest.log" 2>&1
if panicked selftest; then
    fail selftest "kernel panic"
elif has selftest "fs write ok" && has selftest "uvm ok" && has selftest "init: starting sh"; then
    pass selftest
else
    fail selftest "self-tests didn't complete"
fi

# 2. o2's own user tests
run o2tests 300 "o2tests: (ALL|SOME) TESTS" -- o2tests
if panicked o2tests; then
    fail o2tests "kernel panic"
elif has o2tests "o2tests: ALL TESTS PASSED"; then
    pass o2tests
else
    fail o2tests "not all tests passed"
fi

# 3. on arm64, o2tests again with per-process ASIDs
if [ "$ARCH" = arm64 ]; then
    run o2tests-asid 300 "o2tests: (ALL|SOME) TESTS" -A -- o2tests
    if panicked o2tests-asid; then
        fail o2tests-asid "kernel panic"
    elif has o2tests-asid "o2tests: ALL TESTS PASSED"; then
        pass o2tests-asid
    else
        fail o2tests-asid "not all tests passed"
    fi
fi

# 4. xv6's usertests
run usertests 3600 "(ALL TESTS PASSED|SOME TESTS FAILED)" -- "usertests $UTARGS"
if panicked usertests; then
    fail usertests "kernel panic"
elif has usertests "ALL TESTS PASSED"; then
    pass usertests
else
    fail usertests "not all tests passed"
fi

# 5. stressfs: the shell runs the echo once stressfs has finished
run stressfs 300 "^STRESSFS DONE$" -- stressfs "echo STRESSFS DONE"
if panicked stressfs; then
    fail stressfs "kernel panic"
elif has stressfs "^STRESSFS DONE$" && [ "$(grep -c '^read$' "$LOGS/$ARCH-stressfs.log")" -eq 5 ]; then
    pass stressfs
else
    fail stressfs "didn't finish with five readers"
fi

# 6. ^C interrupts a command in the shell (cat, reading the console),
# but not the shell itself, which init would otherwise restart
run ctrlc 60 "^AFTER CTRL-C$" -- cat "$(printf '\003')" "echo AFTER CTRL-C"
if panicked ctrlc; then
    fail ctrlc "kernel panic"
elif has ctrlc "^AFTER CTRL-C$" && [ "$(grep -c 'init: starting sh' "$LOGS/$ARCH-ctrlc.log")" -eq 1 ]; then
    pass ctrlc
else
    fail ctrlc "command not interrupted, or the shell was"
fi

# 7. the shell's line editor: the up key brings back the last command,
# left and typing edit it, tab completes a command name
run lineedit 60 "^TAB-DONE$" -- "echo HIST-ONE" "$(printf '\033[A')" \
    "$(printf '\033[A\033[D\033[D\033[DTWO\033[F\177\177\177')" \
    "$(printf 'ec\tTAB-DONE')"
if panicked lineedit; then
    fail lineedit "kernel panic"
elif [ "$(grep -c '^HIST-ONE$' "$LOGS/$ARCH-lineedit.log")" -eq 2 ] && has lineedit "^HIST-TWO$" &&
     has lineedit "^TAB-DONE$"; then
    pass lineedit
else
    fail lineedit "history, editing or completion didn't work"
fi

# 8. the network: UDP to an echo server on the host (10.0.2.2 in qemu's
# user-mode network is the host's loopback), and ping
python3 tools/udpecho.py > "$LOGS/$ARCH-udpecho.port" &
echopid=$!
sleep 1
port=$(cat "$LOGS/$ARCH-udpecho.port")
run net 120 "received$" -- "nettest 10.0.2.2 $port 50" "ping -c 3 10.0.2.2"
kill $echopid 2>/dev/null
if panicked net; then
    fail net "kernel panic"
elif has net "nettest: 50 of 50 echoed" && has net "3 sent, 3 received"; then
    pass net
else
    fail net "datagrams or pings lost"
fi

# 9. grind runs until stopped: no errors or panic within the time limit
run grind $((GRIND + DELAY)) "^grind:" -- grind
if panicked grind; then
    fail grind "kernel panic"
elif has grind "^grind:"; then
    fail grind "grind reported an error"
elif ! has grind "[AB]{4}"; then
    fail grind "no progress"
else
    pass grind
fi

exit $failed
