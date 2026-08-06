# Porting ForgeOS and ForgeShell

ForgeShell 0.6.3 separates the reusable launcher from hardware integration. A
new port should normally add a directory under `platforms/`; it should not edit
library scanning, metadata, favorites, history, overrides, or page navigation.

## 1. Generate an adapter

```sh
python3 tools/new-platform.py my-device \
  --name "My Device" --resolution 640x480 --provider forge-manifest
```

The generator creates a profile, maintenance manifest, emulator-manifest
example, build-backend placeholder, and adapter README. It refuses to overwrite
an existing adapter.

## 2. Complete `platform.ini`

The profile owns these device decisions:

- physical screen size, bit depth, and fullscreen mode;
- data, ROM, state, and maintenance-tool locations;
- launcher provider and recovery command;
- semantic input mappings and matching short on-screen labels;
- reboot and power-off commands;
- CPU-profile helper;
- device capabilities.

ForgeShell renders to a 320×240 logical canvas and letterboxes it into the
physical display without changing the theme or page layout. The tested viewport
sizes include 320×240, 480×272, 640×360, and 640×480.

Profile values can use `${data_root}`, `${rom_root}`, `${home}`, `${tool_root}`,
and `${device_id}`. Runtime overrides are available through command-line flags
and environment variables.

## 3. Choose a launcher provider

`gmenu2x` reads existing GMenu2X link files. Set `launcher.source` to one or more
colon-separated section directories.

`forge-manifest` reads a portable INI file with `[system.<id>]` sections:

```ini
[system.gba]
title=Game Boy Advance
exec=/usr/bin/retroarch
params=-L /usr/lib/libretro/gpsp_libretro.so [rom]
workdir=/usr/bin
romdir=${rom_root}/gba
romexts=.gba,.zip
```

The provider normalizes both formats into the same bounded `FsSystem` model.

## 4. Declare maintenance tools

`tools.tsv` has five tab-separated fields:

```text
id    title    subtitle    command    required-capability
```

ForgeShell filters entries by the device profile. A device without cpufreq,
brightness, or battery support therefore never shows controls that cannot work.
Supported capability names are `always`, `battery`, `cpu_profiles`,
`brightness`, `volume`, `safe_shutdown`, `storage_health`, and `system_info`.

## 5. Implement boot and recovery

Bootloader, kernel, partition, power, and recovery details stay inside the
adapter. A production port needs:

- a boot dispatcher that can return to the original launcher;
- a safe-mode entry;
- a startup-failure counter or equivalent recovery mechanism;
- atomic state storage;
- commands that flush storage before reboot or shutdown;
- an image or package build backend.

The Q90 adapter is the reference implementation. Do not copy its `/mnt` paths,
Miyoo boot hook, sysfs paths, or GMenu2X commands into the shared core.

## 6. Validate before hardware testing

```sh
python3 tools/validate-platform.py platforms/my-device/platform.ini
./forge-build my-device --check
./tests/run.sh
```

Validation checks schema completeness, resolution, bit depth, unknown fields and
variables, input/label accessibility, launcher-provider support, capabilities,
tool manifests, and portable emulator-manifest templates. Run
`python3 tools/validate-theme.py` after changing the shared visual system.

## 7. Use the simulator

```sh
./tools/run-simulator.sh --resolution 480x272 --fake-battery 72
```

The generic target sandboxes state, ROMs, tools, and emulator launches. It lets a
porter review navigation, scaling, themes, metadata, scanning, safe mode, and
configuration before target hardware is available.

## Port acceptance checklist

A port is not ready merely because ForgeShell starts. Verify input, audio return,
framebuffer scaling, brightness, volume, battery reporting, emulator environment,
save paths, power loss, safe shutdown, recovery controls, idle CPU/RAM, SD-card
writes, and the original launcher fallback on real hardware.
