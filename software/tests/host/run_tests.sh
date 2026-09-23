#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")"
ROOT="../.."

if ! command -v gcc >/dev/null 2>&1 && [ -d "$HOME/scoop/shims" ]; then
    export PATH="$HOME/scoop/shims:$PATH"
fi

CFLAGS=(-std=c11 -Wall -Wextra -Werror
        -I shim
        -I "$ROOT/components/telemetry/include"
        -I "$ROOT/components/app_flow/include")

gcc "${CFLAGS[@]}" "$ROOT/components/telemetry/telemetry_codec.c" test_codec.c -o test_codec
gcc "${CFLAGS[@]}" "$ROOT/components/app_flow/app_flow.c" test_app_flow.c -o test_app_flow

./test_codec
./test_app_flow

echo "ALL HOST TESTS PASSED"
