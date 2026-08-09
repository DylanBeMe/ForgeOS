#!/bin/sh
# This file is sourced by the platform /etc/main script. Do not use exec here:
# replacing that shell makes a frontend exit look like a full boot restart.
status=0
main_root=${FORGE_MAIN_ROOT:-/mnt}
if [ -x "$main_root/forgeshell/boot-dispatch.sh" ]; then
    "$main_root/forgeshell/boot-dispatch.sh" || status=$?
else
    export FRONTEND=gmenu2x
    if cd "$main_root/gmenu2x"; then
        ./gmenu2x || status=$?
    else
        status=1
    fi
fi

# Return to /etc/main so it can mark the frontend active and handle its normal
# respawn path. The exit fallback keeps this file safe to run directly too.
return "$status" 2>/dev/null || exit "$status"
