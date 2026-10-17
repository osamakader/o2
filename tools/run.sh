#!/bin/sh
# Boot the kernel non-interactively and print its serial output.
#
#   tools/run.sh [-a arch] [-c cpus] [-t seconds] [-i input] [-d delay]
#                [-l line-delay] [-f] [-s] [-A] [-e pattern]
#
# -i feeds the file to the console, -d seconds after boot (default 3);
# with -l, one line at a time (ending in CR, like a terminal) with
# line-delay seconds in between, since the console buffers only 128
# characters of unread input. -s builds with the kernel self-tests, -A
# with arm64 ASIDs. -e stops as soon as the output contains pattern
# (grep -E), instead of running until the timeout. While running, the
# raw output is in build/run.<arch>.<pid>.log.
# -f starts from a freshly made fs.img (it otherwise persists).
# Uses the QEMU command line from the Makefile (make print-qemu).

ARCH=arm64 CPUS=3 TIMEOUT=10 INPUT= DELAY=3 FRESH= LDELAY= SELFTEST=0 ASID=0
EXITPAT=
while getopts a:c:t:i:d:l:fsAe: o; do
    case $o in
    a) ARCH=$OPTARG ;;
    c) CPUS=$OPTARG ;;
    t) TIMEOUT=$OPTARG ;;
    i) INPUT=$OPTARG ;;
    d) DELAY=$OPTARG ;;
    f) FRESH=1 ;;
    l) LDELAY=$OPTARG ;;
    s) SELFTEST=1 ;;
    A) ASID=1 ;;
    e) EXITPAT=$OPTARG ;;
    *) exit 2 ;;
    esac
done

cd "$(dirname "$0")/.." || exit 1
M="make -s ARCH=$ARCH SELFTEST=$SELFTEST ARM64_ASID=$ASID"
[ -n "$FRESH" ] && rm -f "$($M print-fsimg)"
$M || exit 1
CMD=$($M CPUS="$CPUS" print-qemu) || exit 1

# -nographic would multiplex the monitor onto stdio; use a plain serial
# port on stdio instead.
CMD=$(echo "$CMD" | sed 's/ -nographic/ -display none -monitor none -serial stdio/')

feed() {
    sleep "$DELAY"
    if [ -n "$LDELAY" ]; then
        while IFS= read -r line; do
            printf '%s\r' "$line"
            sleep "$LDELAY"
        done < "$INPUT"
    else
        cat "$INPUT"
    fi
    sleep "$TIMEOUT"
}

LOG=build/run.$ARCH.$$.log
(
    if [ -n "$INPUT" ]; then
        feed | timeout "$TIMEOUT" $CMD
    else
        timeout "$TIMEOUT" $CMD < /dev/null
    fi
) > "$LOG" 2>&1 &
RUN=$!

# wait for QEMU to exit (timeout), or for the pattern
while kill -0 $RUN 2>/dev/null; do
    if [ -n "$EXITPAT" ] && tr -d '\r' < "$LOG" | grep -qE -- "$EXITPAT"; then
        sleep 1   # let the rest of the line arrive
        pkill -TERM -P $RUN
        break
    fi
    sleep 1
done
wait $RUN 2>/dev/null

tr -d '\r' < "$LOG"
rm -f "$LOG"
exit 0
