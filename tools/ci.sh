#!/bin/sh
# Build and test one arch, as CI does.
#
#   tools/ci.sh arm64|riscv64 [quick]
#
# rpi4 is arm64 built for the Raspberry Pi 4, run on qemu's raspi4b.
# Boots the self-test build, then runs o2tests, xv6's usertests (-q with
# quick), stressfs, ^C, line editing, network and graphics checks and a
# timed grind in the shell, then reboot and halt; on arm64 also o2tests
# with ARM64_ASID=1.
# The network and graphics checks need python3 on the host. Exits non-zero if anything fails or the
# kernel panics. Logs are kept in build/ci/<arch>-<step>.log.

NAME=${1:?usage: tools/ci.sh arm64|riscv64|rpi4 [quick]}
QUICK=${2:-}

cd "$(dirname "$0")/.." || exit 1

# rpi4: arm64 built for the Raspberry Pi 4 (make PLATFORM=rpi4), on
# qemu's raspi4b, which qemu has since 9.0; without it, only built
ARCH=$NAME
if [ "$NAME" = rpi4 ]; then
    ARCH=arm64
    export PLATFORM=rpi4
    if ! qemu-system-aarch64 -M help | grep -q '^raspi4b'; then
        make -s ARCH=arm64 PLATFORM=rpi4 || exit 1
        echo "PASS  rpi4 build (this qemu has no raspi4b to run it on)"
        exit 0
    fi
fi
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
pass() { echo "PASS  $NAME $1"; }
fail() {
    echo "FAIL  $NAME $1: $2"
    echo "----- last lines of $LOGS/$NAME-$1.log"
    tail -n 25 "$LOGS/$NAME-$1.log"
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
    input=$LOGS/$NAME-$name.in
    : > "$input"
    for line in "$@"; do
        printf '%s\n' "$line" >> "$input"
    done
    tools/run.sh -f -a "$ARCH" -t "$timeout" -i "$PWD/$input" -d $DELAY -l $LINE \
        -e "$pattern|panic:" $opts > "$LOGS/$NAME-$name.log" 2>&1
}

# log name has pattern?
has() { grep -qE -- "$2" "$LOGS/$NAME-$1.log"; }

panicked() { has "$1" "panic:"; }

# 1. kernel self-tests: they run at boot, before the shell starts
tools/run.sh -s -f -a "$ARCH" -t 120 -e "init: starting sh|panic:" \
    > "$LOGS/$NAME-selftest.log" 2>&1
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
if [ "$NAME" = arm64 ]; then
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
elif has stressfs "^STRESSFS DONE$" && [ "$(grep -c '^read$' "$LOGS/$NAME-stressfs.log")" -eq 5 ]; then
    pass stressfs
else
    fail stressfs "didn't finish with five readers"
fi

# 6. ^C interrupts a command in the shell (cat, reading the console),
# but not the shell itself, which init would otherwise restart
run ctrlc 60 "^AFTER CTRL-C$" -- cat "$(printf '\003')" "echo AFTER CTRL-C"
if panicked ctrlc; then
    fail ctrlc "kernel panic"
elif has ctrlc "^AFTER CTRL-C$" && [ "$(grep -c 'init: starting sh' "$LOGS/$NAME-ctrlc.log")" -eq 1 ]; then
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
elif [ "$(grep -c '^HIST-ONE$' "$LOGS/$NAME-lineedit.log")" -eq 2 ] && has lineedit "^HIST-TWO$" &&
     has lineedit "^TAB-DONE$"; then
    pass lineedit
else
    fail lineedit "history, editing or completion didn't work"
fi

# 8. the network: UDP to an echo server on the host (10.0.2.2 in qemu's
# user-mode network is the host's loopback), and ping. Not on the Pi:
# raspi4b has no network card o2 drives.
if [ "$NAME" != rpi4 ]; then
    python3 tools/udpecho.py > "$LOGS/$NAME-udpecho.port" &
    echopid=$!
    sleep 1
    port=$(cat "$LOGS/$NAME-udpecho.port")
    run net 120 "received$" -- "nettest 10.0.2.2 $port 50" "ping -c 3 10.0.2.2"
    kill $echopid 2>/dev/null
    if panicked net; then
        fail net "kernel panic"
    elif has net "nettest: 50 of 50 echoed" && has net "3 sent, 3 received"; then
        pass net
    else
        fail net "datagrams or pings lost"
    fi
fi

# 9. graphics (a GPU=1 build): the boot animation ends with the logo on
# the screen; gfx draws its test picture
export GPU=1
rm -f "$LOGS/$NAME-splash.ppm"
run splash 60 "init: starting sh" -D "$PWD/$LOGS/$NAME-splash.ppm" --
if panicked splash; then
    fail splash "kernel panic"
elif python3 tools/checkshot.py "$LOGS/$NAME-splash.ppm" splash >> "$LOGS/$NAME-splash.log" 2>&1; then
    pass splash
else
    fail splash "the boot animation didn't end with the logo"
fi

rm -f "$LOGS/$NAME-gfx.ppm"
run gfx 60 "gfx: .*done" -D "$PWD/$LOGS/$NAME-gfx.ppm" -- gfx
if panicked gfx; then
    fail gfx "kernel panic"
elif python3 tools/checkshot.py "$LOGS/$NAME-gfx.ppm" >> "$LOGS/$NAME-gfx.log" 2>&1; then
    pass gfx
else
    fail gfx "the screen doesn't show the picture"
fi

# 10. input (virt only: raspi4b has no virtio): a key, a mouse move
# and a click from the QEMU monitor reach /input
if [ "$NAME" != rpi4 ]; then
    printf '%s\n' "sendkey a" "mouse_move 100 50" "mouse_button 1" "mouse_button 0" \
        > "$LOGS/$NAME-input.mon"
    run input 60 "code 272 value 0" -m "$PWD/$LOGS/$NAME-input.mon" -- "evtest 12"
    if panicked input; then
        fail input "kernel panic"
    elif has input "type 1 code 30 value 1" && has input "type 2 code 0 value 100" &&
         has input "code 272 value 0"; then
        pass input
    else
        fail input "the events didn't arrive"
    fi

    # 11. the desktop: Files opens README in view; a command typed into
    # the terminal runs; the screen shows them and the taskbar. The
    # pointer starts in the middle of the 1280x800 screen.
    {
        echo "mouse_move -600 -400"
        echo "mouse_move 70 787"       # the Files button
        echo "mouse_button 1"
        echo "mouse_button 0"
        echo "mouse_move 0 0"          # (time for it to start)
        echo "mouse_move 0 0"
        echo "mouse_move 0 0"
        echo "mouse_move 30 -688"      # README, its first entry
        echo "mouse_button 1"
        echo "mouse_button 0"
        echo "sendkey ret"
        echo "mouse_move 0 0"
        echo "mouse_move 0 0"
        echo "mouse_move 0 0"
        echo "mouse_move -110 200"     # the terminal again
        echo "mouse_button 1"
        echo "mouse_button 0"
        for k in e c h o spc d e s k o k spc shift-dot spc c o n s o l e ret; do
            echo "sendkey $k"
        done
    } > "$LOGS/$NAME-desk.mon"
    rm -f "$LOGS/$NAME-desk.ppm"
    run desk 60 "^deskok" -m "$PWD/$LOGS/$NAME-desk.mon" -D "$PWD/$LOGS/$NAME-desk.ppm" -- desk
    if panicked desk; then
        fail desk "kernel panic"
    elif ! has desk "^deskok"; then
        fail desk "the typed command didn't run"
    elif python3 tools/checkshot.py "$LOGS/$NAME-desk.ppm" desk >> "$LOGS/$NAME-desk.log" 2>&1; then
        pass desk
    else
        fail desk "the screen doesn't show the desktop"
    fi
fi
unset GPU

# 12. grind runs until stopped: no errors or panic within the time limit
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

# 13. reboot comes back to the shell; halt makes qemu exit (run.sh
# stops it itself otherwise, and says so)
run reboot 40 "zzz" -- reboot
if panicked reboot; then
    fail reboot "kernel panic"
elif [ "$(grep -c "init: starting sh" "$LOGS/$NAME-reboot.log")" -lt 2 ]; then
    fail reboot "it didn't come back"
else
    pass reboot
fi
run halt 20 "zzz" -- halt
if panicked halt; then
    fail halt "kernel panic"
elif ! has halt "o2: powering off" || has halt "terminating on signal"; then
    fail halt "qemu didn't power off"
else
    pass halt
fi

exit $failed
