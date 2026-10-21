#!/usr/bin/env bash
# Runs DemoApp scenes' smoke runs (FENRIR_DEMO_SCENE with FENRIR_DEMO_KEYS=1) all at once and reports each one.
# Each scene presses its keys, stops itself three seconds after the last, and is killed if it outlives the timeout.
# The demos run with FENRIR_DEMO_FAST=4 (four ticks a frame, as fast as it renders) and FENRIR_DEMO_HIDDEN=1 (no
# window shown); set either to 0 to run in real time or to watch.
#
# Usage: scripts/demo-smoke.sh [build dir] [scene...]
#   build dir  defaults to out/build/clang-x64-debug; the DemoApp must be built in it.
#   scene...   defaults to every physics scene.
# FENRIR_SMOKE_TIMEOUT sets the per-scene timeout in seconds (default 900). Logs go to <build dir>/demo-smoke/.
#
# A scene passes when it exits with 0, pressed every one of its keys, and logged no error, exception, fatal or assert
# line. The script exits with 1 if any scene failed.

set -u

repo=$(cd "$(dirname "$0")/.." && pwd)
build=${1:-$repo/out/build/clang-x64-debug}
shift $(($# > 0 ? 1 : 0))
if [ $# -gt 0 ]; then
    scenes=("$@")
else
    scenes=("Physics (C#)" "Joints (C++)" "Joints (C#)" "Character (C++)" "Character (C#)" "Vehicles (C++)"
        "Motorcycle (C++)" "Tank (C++)" "Vehicles (C#)")
fi
limit=${FENRIR_SMOKE_TIMEOUT:-900}
fast=${FENRIR_DEMO_FAST:-4}
hidden=${FENRIR_DEMO_HIDDEN:-1}

demo="$build/examples/DemoApp"
exe="$demo/DemoApp.exe"
[ -x "$exe" ] || exe="$demo/DemoApp"
if [ ! -x "$exe" ]; then
    echo "No DemoApp in $demo; build it first (cmake --build $build --target DemoApp)." >&2
    exit 2
fi
out="$build/demo-smoke"
mkdir -p "$out"
rm -f "$out"/*.log "$out"/*.exit

# A file name for a scene: "Vehicles (C#)" is Vehicles_Cs.
slug() {
    printf '%s' "$1" | sed -e 's/#/s/g' -e 's/+/p/g' -e 's/[^A-Za-z0-9]\{1,\}/_/g' -e 's/_$//'
}

echo "Running ${#scenes[@]} scenes at once (fast $fast, hidden $hidden, timeout ${limit}s each); logs in $out"
start=$(date +%s)
for scene in "${scenes[@]}"; do
    (
        log="$out/$(slug "$scene").log"
        cd "$demo" && FENRIR_DEMO_SCENE="$scene" FENRIR_DEMO_KEYS=1 FENRIR_DEMO_FAST="$fast" FENRIR_DEMO_HIDDEN="$hidden" \
            timeout "$limit" "$exe" > "$log" 2>&1
        code=$?
        echo "$code" > "${log%.log}.exit"
        echo "  finished $scene (exit $code) after $(($(date +%s) - start))s"
    ) &
done
wait

failed=0
echo
for scene in "${scenes[@]}"; do
    log="$out/$(slug "$scene").log"
    code=$(cat "${log%.log}.exit" 2>/dev/null || echo "?")
    pressed=$(grep -c 'FENRIR_DEMO_KEYS: pressing key' "$log")
    # Each press logs "(i/N)"; a scene with no keys pressed has no count to read.
    total=$(grep -o 'FENRIR_DEMO_KEYS: pressing key [0-9]* ([0-9]*/[0-9]*)' "$log" | tail -n 1 | sed 's/.*\/\([0-9]*\))/\1/')
    total=${total:-?}
    errors=$(grep -i -E 'error|exception|fatal|assert' "$log")
    problems=()
    [ "$code" = 0 ] || problems+=("exit $code$([ "$code" = 124 ] && echo ", timed out after ${limit}s")")
    [ "$total" != "?" ] && [ "$pressed" = "$total" ] || problems+=("keys $pressed/$total")
    grep -q 'FENRIR_DEMO_SCENE: no scene' "$log" && problems+=("scene not in this build")
    [ -z "$errors" ] || problems+=("$(printf '%s\n' "$errors" | wc -l) error lines")
    if [ ${#problems[@]} -eq 0 ]; then
        echo "PASS  $scene: exit 0, keys $pressed/$total"
    else
        failed=1
        echo "FAIL  $scene: $(IFS=';'; echo "${problems[*]}" | sed 's/;/; /g')  ($log)"
        [ -z "$errors" ] || printf '%s\n' "$errors" | head -n 10 | sed 's/^/        /'
    fi
done
echo
echo "Took $(($(date +%s) - start))s."
exit $failed
