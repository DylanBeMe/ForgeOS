# Emulator compatibility policy

ForgeOS treats emulator currency as a target-compatibility problem, not a desktop
version-number race. The Q90 is a low-memory, low-clock ARM handheld, so the
preferred build is the newest **MiyooCFW-integrated and Q90-tested** snapshot
rather than an arbitrary upstream nightly that may be newer but slower or less
stable on this hardware.

## Q90 source baseline

The Q90 adapter pins immutable MiyooCFW component revisions in
`platforms/q90/build.env`. A full build records its selected emulator packages
and discoverable version metadata in `forgeos-q90-<version>.img.emulators.txt`.
The build fails if the expected Q90 emulator baseline disappears from the pinned
Buildroot configuration.

The baseline covers the principal bundled systems through RetroArch/libretro and
keeps target-provided standalone fallbacks where MiyooCFW supplies them. In
particular, ForgeOS expects the Q90 build to retain target-appropriate cores for
NES, GB/GBC, GBA, Mega Drive/Genesis and related Sega systems, SNES, PS1,
PC Engine, arcade, Atari 2600, and WonderSwan.

Do not replace these cores automatically with unrelated desktop/nightly builds.
For the systems where MiyooCFW publishes target guidance, ForgeOS follows the
low-end-device recommendations: gpSP for GBA, Gambatte for GB/GBC, PicoDrive for
Mega Drive/Genesis, and Snes9x 2005 as the higher-performance SNES alternative.
An emulator upgrade is accepted only after it builds for the Q90 and performs at
least as well in the compatibility and performance checks below.

## Compatibility score

`docs/compatibility-matrix.csv` is the evidence source. Record results with:

```sh
python3 tools/record-compatibility.py docs/compatibility-matrix.csv \
  --help
```

Summarize current evidence with:

```sh
python3 tools/compatibility-report.py docs/compatibility-matrix.csv
```

Before publishing a high-compatibility claim, run a representative sample on
physical Q90 hardware and enforce the release floor:

```sh
python3 tools/compatibility-report.py docs/compatibility-matrix.csv \
  --enforce --minimum-games 20 --minimum-playable 90
```

A game counts as playable only when boot, gameplay, and return-to-ForgeShell pass
and neither frame pacing nor audio has a hard failure. The report also publishes
a stricter full-feature rate and per-system playable rates. Empty or partial
matrices never produce a fabricated percentage.

Twenty games is only the minimum release gate, not a claim of statistical
completeness. Prefer several titles per advertised system and include difficult
or commonly problematic games, not just easy-to-emulate examples.

## BIOS and game data

ForgeOS does not distribute commercial ROMs or proprietary BIOS files. Use
legally obtained game dumps and BIOS files where an emulator requires them.
Compatibility reports should contain titles/results only; the ROM or BIOS bytes
do not need to be committed or shared.

## Upgrade procedure

For an emulator/core change:

1. Update the pinned Q90-integrated source rather than dropping an untracked
   binary into the image.
2. Run `./forge-build q90 --check` and the full local test suite.
3. Build the complete Q90 image and review its `.emulators.txt` manifest.
4. Re-run the representative compatibility matrix on physical hardware.
5. Compare startup time, frame pacing, audio, save behavior, and exit/return with
   the previous build.
6. Keep the upgrade only when compatibility is not regressed; document any
   per-system exception or fallback explicitly.
