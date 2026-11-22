#!/bin/sh
set -eu

DYLD_PRINT_LIBRARIES=1 "$1" > first-frame.stdout 2> first-frame.stderr &
app=$!
trap 'kill "$app" 2>/dev/null || :; wait "$app" 2>/dev/null || :' EXIT
deadline=$(($(date +%s) + 45))
sampledA=false
sampledB=false
sample=none

while kill -0 "$app" 2>/dev/null; do
    if [ "$(date +%s)" -ge "$deadline" ]; then
        cat first-frame.stdout first-frame.stderr
        echo "AppKit first frame timed out (A=$sampledA B=$sampledB last=$sample)" >&2
        exit 1
    fi
    if [ "$sampledA" = false ] && grep -Eq '^first-frame present-request-progress=([6-9][0-9]|[1-9][0-9][0-9]+)$' first-frame.stdout; then
        sample=$("$2" "$app")
        if [ "$sample" = '#3F99C8' ]; then
            sampledA=true
            echo "A: window center $sample (sRGB RGB8)"
            touch second-color
        fi
    elif [ "$sampledA" = true ] && [ "$sampledB" = false ] && grep -Eq '^first-frame present-request-progress=(1[2-9][0-9]|[2-9][0-9][0-9]|[1-9][0-9][0-9][0-9]+)$' first-frame.stdout; then
        sample=$("$2" "$app")
        if [ "$sample" = '#FF0000' ]; then
            sampledB=true
            echo "B: window center $sample (sRGB RGB8)"
            touch stop
        fi
    fi
    sleep 0.05
done

result=0
wait "$app" || result=$?
trap - EXIT
cat first-frame.stdout first-frame.stderr
if [ "$result" -eq 1 ] &&
    grep -q 'Failed to enumerate any physical Metal devices' first-frame.stdout &&
    grep -q '^Pangu Application: error domain=70672e72656e64 code=261$' first-frame.stderr &&
    ! grep -q '^first-frame ' first-frame.stdout; then
    echo "Metal unavailable; Open failure and exit verified"
    exit 0
fi

if [ "$sampledA" != true ] || [ "$sampledB" != true ]; then
    echo "AppKit stopped before both colors were sampled (A=$sampledA B=$sampledB last=$sample)" >&2
    exit 1
fi
[ "$result" -eq 0 ] &&
    grep -Eq '^first-frame present-requests=(1[2-9][0-9]|[2-9][0-9][0-9]|[1-9][0-9][0-9][0-9]+) skipped=[0-9]+ failures=0$' first-frame.stdout &&
    echo "Metal window colors, present calls and normal exit verified"
