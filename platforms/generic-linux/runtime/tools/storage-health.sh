#!/bin/sh
root=${FORGE_DATA_ROOT:-runtime/generic-linux}
df -h "$root" 2>/dev/null || df -h .
printf '\nPress Enter to return.\n'
read _answer
