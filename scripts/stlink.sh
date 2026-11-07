#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
action=${1:-probe}
case "$action" in probe|program|bridge) ;; *) echo "Unknown action: $action" >&2; exit 1 ;; esac
DOCKER=${DOCKER:-docker}
IMAGE=${IMAGE:-hp21xx-firmware:1.1}
mkdir -p build/stlink

native_openocd() {
    case "$(uname -m)" in
        arm64) arch=arm64; checksum=4f458dea82af3529d4dc71544abfd8c8c96ebe6c521868f87c6697b0866c9e99 ;;
        x86_64) arch=x64; checksum=aa5052790b003f18e64691a3b8b7a650d4ce54f72fbbb23f1aed2d26a5795b76 ;;
        *) echo 'Unsupported macOS CPU' >&2; exit 1 ;;
    esac
    tools="$(pwd)/build/stlink/openocd"
    if [ ! -x "$tools/bin/openocd" ]; then
        archive="build/stlink/openocd-darwin-$arch.tar.gz"
        curl -fL "https://github.com/xpack-dev-tools/openocd-xpack/releases/download/v0.12.0-6/xpack-openocd-0.12.0-6-darwin-$arch.tar.gz" -o "$archive"
        echo "$checksum  $archive" | shasum -a 256 -c -
        mkdir -p "$tools"
        tar -xzf "$archive" -C "$tools" --strip-components=1
    fi
    OPENOCD="$tools/bin/openocd"
}

if [ "$action" = bridge ]; then
    native_openocd
    exec "$OPENOCD" -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c 'bindto 0.0.0.0; adapter speed 1000; tcl_port disabled; telnet_port disabled; init'
fi

bridge_pid=
cleanup() {
    if [ -n "$bridge_pid" ]; then kill "$bridge_pid" 2>/dev/null || true; wait "$bridge_pid" 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM

if [ -z "${PROGRAM_HOST:-}" ] && [ "$(uname -s)" = Darwin ]; then
    native_openocd
    if [ "$action" = probe ]; then
        exec "$OPENOCD" -f interface/stlink.cfg -f target/stm32f1x.cfg \
            -c 'adapter speed 1000; init; targets; shutdown'
    fi
    "$OPENOCD" -f interface/stlink.cfg -f target/stm32f1x.cfg \
        -c 'bindto 0.0.0.0; adapter speed 1000; tcl_port disabled; telnet_port disabled; init' \
        >build/stlink/bridge.log 2>&1 &
    bridge_pid=$!
    PROGRAM_HOST=host.docker.internal
    ready=0
    for attempt in 1 2 3 4 5 6 7 8 9 10; do
        if ! kill -0 "$bridge_pid" 2>/dev/null; then cat build/stlink/bridge.log; exit 1; fi
        if grep -q 'Listening on port 3333' build/stlink/bridge.log; then ready=1; break; fi
        sleep 1
    done
    if [ "$ready" != 1 ]; then cat build/stlink/bridge.log; exit 1; fi
fi

if [ -n "${PROGRAM_HOST:-}" ]; then
    if [ "$action" = probe ]; then
        exec "$DOCKER" run --rm -e PROGRAM_HOST="$PROGRAM_HOST" "$IMAGE" \
            sh -c 'gdb-multiarch -batch -ex "target extended-remote $PROGRAM_HOST:3333" -ex "monitor targets" -ex disconnect'
    fi
    "$DOCKER" run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -e PROGRAM_HOST="$PROGRAM_HOST" \
        -v "$(pwd):/workspace" -w /workspace "$IMAGE" \
        gdb-multiarch -batch -x scripts/program.gdb build/firmware/hp21xx_papertape_emulator.ino.elf
else
    if [ "$action" = probe ]; then
        exec "$DOCKER" run --rm --device /dev/bus/usb "$IMAGE" openocd \
            -f interface/stlink.cfg -f target/stm32f1x.cfg -c 'adapter speed 1000; init; targets; shutdown'
    fi
    "$DOCKER" run --rm --device /dev/bus/usb -e PROGRAM_HOST=localhost \
        -v "$(pwd):/workspace" -w /workspace "$IMAGE" sh -ec '
        openocd -f interface/stlink.cfg -f target/stm32f1x.cfg -c "adapter speed 1000; init" >build/stlink/bridge.log 2>&1 &
        bridge_pid=$!
        trap "kill $bridge_pid 2>/dev/null || true" EXIT
        ready=0
        for attempt in 1 2 3 4 5 6 7 8 9 10; do
            kill -0 "$bridge_pid" || { cat build/stlink/bridge.log; exit 1; }
            if grep -q "Listening on port 3333" build/stlink/bridge.log; then ready=1; break; fi
            sleep 1
        done
        test "$ready" = 1 || { cat build/stlink/bridge.log; exit 1; }
        gdb-multiarch -batch -x scripts/program.gdb build/firmware/hp21xx_papertape_emulator.ino.elf'
fi
