#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
set -eu
test_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
repo_dir=$(CDPATH= cd -- "$test_dir/../.." && pwd)
sdk_dir="$repo_dir/components/mybot_stack/mybot_sdk/mybot"
test_build=$(mktemp -d "${TMPDIR:-/tmp}/mybot-translation-client.XXXXXX")
trap 'rm -rf "$test_build"' EXIT HUP INT TERM
# Apple's headers deprecate sprintf in the unchanged cJSON-derived parser.
# Suppress that diagnostic only for the existing parser, not the client under test.
"${CC:-cc}" -std=c99 -Wall -Wextra -Werror -fsanitize="${SANITIZERS:-undefined}" \
    -fno-omit-frame-pointer -Wno-deprecated-declarations \
    -I"$test_dir/mocks" -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
    -c "$sdk_dir/src/support/mybot_json.c" -o "$test_build/json.o"
for feature_state in 1 0; do
    "${CC:-cc}" -std=c99 -Wall -Wextra -Werror -fsanitize="${SANITIZERS:-undefined}" \
        -fno-omit-frame-pointer -Dtime=mybot_test_time \
        -DMYBOT_CLOUD_AEC="$feature_state" -DMYBOT_AI_QOS="$feature_state" \
        -I"$test_dir/mocks" -I"$sdk_dir/src/internal" -I"$sdk_dir/include" \
        "$test_dir/test_client.c" "$sdk_dir/src/service/mybot_device_client.c" \
        "$test_build/json.o" -lm -o "$test_build/test-client"
    "$test_build/test-client"
done
