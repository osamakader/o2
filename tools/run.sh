#!/bin/sh
# Boot the kernel non-interactively and print its serial output.
#
#   tools/run.sh [-a arch] [-c cpus] [-t seconds] [-i input] [-d delay]
#
# -i feeds the file to the console, -d seconds after boot (default 3).
# Uses the QEMU command line from the Makefile (make print-qemu).

ARCH=arm64 CPUS=3 TIMEOUT=10 INPUT= DELAY=3
while getopts a:c:t:i:d: o; do
    case $o in
    a) ARCH=$OPTARG ;;
    c) CPUS=$OPTARG ;;
    t) TIMEOUT=$OPTARG ;;
    i) INPUT=$OPTARG ;;
    d) DELAY=$OPTARG ;;
    *) exit 2 ;;
    esac
done

cd "$(dirname "$0")/.." || exit 1
make -s ARCH="$ARCH" || exit 1
CMD=$(make -s ARCH="$ARCH" CPUS="$CPUS" print-qemu) || exit 1

# -nographic would multiplex the monitor onto stdio; use a plain serial
# port on stdio instead.
CMD=$(echo "$CMD" | sed 's/ -nographic/ -display none -monitor none -serial stdio/')

if [ -n "$INPUT" ]; then
    (sleep "$DELAY"; cat "$INPUT"; sleep "$TIMEOUT") | timeout "$TIMEOUT" $CMD 2>&1 | tr -d '\r'
else
    timeout "$TIMEOUT" $CMD < /dev/null 2>&1 | tr -d '\r'
fi
exit 0
