#!/usr/bin/env bash
set -euo pipefail
sudo apt-get update
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y --no-install-recommends \
  bc bison build-essential busybox btrfs-progs clang cmake cpio file flex git \
  fonts-urw-base35 imagemagick libncurses-dev libpython3-dev libsdl-ttf2.0-dev \
  libsdl1.2-dev \
  libssl-dev mercurial patch pkg-config python3 rsync shellcheck subversion \
  swig unzip wget xz-utils zip
