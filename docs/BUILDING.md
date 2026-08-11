# Building ForgeOS

## Supported host

Use a current 64-bit Ubuntu or Debian Linux system with at least 20 GiB of free
disk space. A clean Q90 build can take substantial CPU, storage, and network time.

## Ubuntu dependencies

```sh
sudo apt-get update
sudo apt-get install -y \
  bc bison build-essential busybox btrfs-progs clang cmake cpio file flex git \
  imagemagick libncurses-dev libpython3-dev libsdl-ttf2.0-dev libsdl1.2-dev \
  libssl-dev mercurial patch pkg-config python3 rsync shellcheck subversion \
  swig unzip wget xz-utils zip
```

## Validation

```sh
python3 tools/validate-platform.py
python3 tools/validate-theme.py
./forge-build q90 --check
./tests/run.sh
```

## Q90 image

```sh
JOBS=2 ./forge-build q90
```

Optional paths:

```sh
WORKDIR=/path/to/q90-buildroot \
DOWNLOAD_DIR=/path/to/buildroot-downloads \
JOBS=4 ./forge-build q90
```

`WORKDIR` must be empty or previously created by ForgeOS. The builder refuses to
reset an unrelated Git checkout.

The core Q90 source revisions are pinned in `platforms/q90/build.env`: the parent
MiyooCFW Buildroot revision plus the kernel, U-Boot, and GMenu2X commits. The builder
rewrites the upstream `origin/master` references before invoking Buildroot.

## Outputs

The `dist/` directory contains the raw image, SHA-256 checksum, exact upstream
revision, and emulator package manifest. `tools/package-release.sh` creates the
compressed release bundle under `release/`.
