#!/bin/sh
# Sourced by MiyooCFW /etc/main. Keep a direct GMenu2X recovery path.
if [ -x /mnt/forgeshell/boot-dispatch.sh ]; then
    exec /mnt/forgeshell/boot-dispatch.sh
fi
export FRONTEND=gmenu2x
cd /mnt/gmenu2x || exit 1
exec ./gmenu2x
