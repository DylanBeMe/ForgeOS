# ForgeOS Q90 0.6.4 hardware acceptance plan

Use a spare microSD card. Keep the original card unchanged and available for
recovery. Record the card brand, size, Q90 hardware revision if known, battery
state, and image checksum before testing.

## 1. Flash and first boot

1. Verify the image checksum.
2. Flash the complete image to the spare card.
3. Cold boot with no USB cable attached.
4. Confirm GMenu2X starts by default.
5. Start ForgeShell Beta manually from the ForgeOS section.
6. Complete setup once with GMenu2X selected, then run it again and select
   ForgeShell.
7. Confirm the next cold boot enters the chosen launcher.

Record boot duration, blank-screen duration, orientation, tearing, color, font
legibility, and any console messages.

## 2. Input map

On every page verify D-pad, A, B, X, Y, START, SELECT, L, R, and RESET. Check for
stuck repeats, double activation, swapped buttons, missed releases, and input
latency. Confirm SELECT always reaches GMenu2X and RESET reaches recovery.

## 3. Display and UX

Inspect Home, Library, Search, Activity, Maintenance, Settings, Power,
first-run setup, Game Options, confirmation modals, empty states, long titles,
large text, high contrast, Safe Mode, and maintenance dialogs.

Acceptance criteria:

- no clipped controls or unreadable text at 320×240;
- focus is always visible;
- A/B behavior is consistent;
- left/right option changes are reversible;
- every page uses the Midnight Mint theme;
- no rapid redraw, flicker, or visible idle animation;
- long UTF-8 names truncate cleanly.

## 4. Library and storage

Test libraries containing approximately 0, 100, 500, and 2,000 games. Include
nested folders, mixed-case extensions, spaces, apostrophes, Unicode names,
archives, an inaccessible folder, a symlink, and a removed ROM.

Measure first scan, cached startup, manual rescan, search response, and return
from a game. Confirm the cache is reused on ordinary boots and rebuilt when an
emulator definition or ROM root changes.

## 5. Emulator matrix

Test at least one easy and one demanding title for GB, GBC, GBA, NES, SNES,
Genesis, PC Engine, PS1, Arcade, and any installed ports/ScummVM packages.
Record emulator/core, BIOS, boot, gameplay, frame pacing, audio, SRAM,
save-state behavior, exit/return, CPU profile, and notes in
`docs/compatibility-matrix.csv` or with `tools/record-compatibility.py`.

Confirm per-game overrides affect only the selected game and that Reset restores
system defaults. Verify CPU settings are restored after normal exit, failed
launch, and emulator crash.

## 6. Audio, brightness, and power

- Change volume before, during, and after an emulator.
- Confirm audio returns after every emulator and maintenance tool.
- Test minimum/maximum brightness and persistence over reboot.
- Test normal shutdown, restart, power loss while idle, and power loss shortly
  after a settings/favorite/override write.
- Check battery percentage/status and charging behavior.

Do not intentionally remove power during firmware writes or SD filesystem
repair.

## 7. Recovery tests

1. Select ForgeShell as default and confirm SELECT returns to GMenu2X.
2. Start ForgeShell Safe Mode from GMenu2X.
3. Arm Safe Mode for next boot and confirm it is consumed once.
4. Corrupt `config.ini` and confirm last-known-good settings are restored or
   GMenu2X starts.
5. Use Reset ForgeShell to restore defaults.
6. Force GMenu2X for one boot.
7. Temporarily rename the ForgeShell binary and confirm recovery.
8. Simulate three starts that fail before the first frame and confirm automatic
   GMenu2X fallback.
9. View the boot log after each failure.

## 8. Performance targets

Run Performance Snapshot before and after ten minutes of browsing and after a
large scan. Record:

| Metric | Beta target |
|---|---:|
| Idle ForgeShell VmRSS | under 8 MiB |
| Cached startup | under 3 seconds |
| Menu input | no perceptible delay |
| Idle CPU | close to zero |
| Normal navigation writes | none |
| Return from emulator | reliable every time |
| Recovery success | 100% |

Watch `state/` file sizes. They should change only after settings, favorites,
sessions, scans, or overrides—not continuously while idle.

## 9. Reports to retain

Collect:

- Hardware Check report;
- Performance Snapshot before/after reports;
- `/mnt/forgeshell/state/forgeshell.log` after failures;
- compatibility matrix;
- usability results;
- exact image checksum and emulator manifest;
- photos or video of display/input defects.

A beta is acceptable only when all recovery paths work and no test can strand
the device without GMenu2X or an untouched recovery card.
