#!/bin/sh
set -u
export FRONTEND=gmenu2x
cd /mnt/gmenu2x || exit 1
exec ./gmenu2x
