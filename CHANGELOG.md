# Changelog

## Unreleased

## 0.6.4 — 2026-08-09

### Changed

- Boot branding now uses ForgeOS artwork at both the early framebuffer splash and the runtime boot-logo stage, with a short ForgeOS startup chime.
- Full-image generation replaces the upstream version-stamped splash with the unannotated ForgeOS splash and rewrites user-facing first-boot MiyooCFW labels as ForgeOS Setup.
- Full-image builds validate that the generated BOOT tree still contains the ForgeOS first-boot repair hook and no user-facing MiyooCFW first-boot branding.
- ForgeShell startup normalizes its writable state directory to mode 0755 and reports a targeted recovery error if the directory cannot be written. Build and overlay paths also normalize ForgeOS-owned directories/scripts so host umasks or setgid extraction do not leak into the firmware.
- The Q90 runtime profile is now installed directly from the canonical platform profile so displayed controls, capabilities, and build-time previews cannot drift apart.
- Long onboarding and empty-state guidance wraps across readable lines instead of silently truncating, and the Home footer no longer advertises a no-op Back action.
- Theme validation now enforces minimum text/accent contrast ratios in addition to palette/schema consistency.
- Library redraws precompute per-system counts and visible filtered rows, and ROM extension matching no longer copies/tokenizes the complete filter for every candidate file.

### Fixed

- First boot repairs an image-sized backup GPT with `sgdisk -e` before the ROMS expansion step, with a `parted` fallback for compatible environments, preventing partition 5 from disappearing when an image is flashed to a larger SD card.
- The generated BOOT staging tree explicitly receives `firstboot.custom.sh`, preventing upstream image-generation steps from silently dropping the GPT repair hook.
- `autoexec.sh` no longer uses `exec` while sourced by the platform `/etc/main`; frontend exits now return to the supervising boot script instead of replaying the intro in a respawn loop.
- Full-image builds and update-safe overlay installs now include the runtime `logo.png`, `logobg.png`, and `logo.wav` assets; overlay installs back up existing branding first.
- Shared maintenance helpers no longer leave the caller at `umask 077` after creating a private temporary file, preventing unrelated later files from becoming unexpectedly owner-only.
- Manual ForgeShell launches no longer masquerade as system boots or consume next-boot safe-mode semantics, while a successful manual recovery launch can still clear the startup-failure counter.
- One-shot safe-mode config rewriting now removes duplicate stale keys and writes the replacement with deterministic permissions.
- Restart confirmation text now says restart rather than power-off.

## 0.6.3 — 2026-08-06

### Changed

- Game launches no longer create CPU-state temporary files unless a non-default CPU profile is actually requested and a compatible helper is executable.
- Emulator alternatives are limited to definitions that use the same normalized ROM root and support the selected file, preventing cross-system `.zip` matches.
- Theme loading is transactional: malformed custom themes now fall back entirely to the built-in Midnight Mint palette rather than leaving a mixed interface.
- First-run completion writes related settings in one atomic update, and failed per-game-option saves restore the previous in-memory value.
- Onboarding and Game Options no longer advertise page controls that are inactive in those states.
- GitHub Actions dependencies now use Node.js 24-native releases (`checkout@v7`, `cache@v6`, and `upload-artifact@v7`).

### Fixed

- Unmatched quotes in emulator parameters being passed to `/bin/sh` as malformed commands.
- Duplicate, unknown, malformed, overlong, incomplete, or unresolved device, emulator, and maintenance manifest fields being accepted inconsistently between runtime and validation tools.
- A startup-success recovery marker failure being ignored after the first rendered frame.
- Emulator catalog overflow being treated as a successful partial import.
- Duplicate maintenance IDs being missed when the first entry was hidden by a capability filter.
- Invalid configuration values silently reverting to defaults instead of activating last-known-good recovery.
- Critical runtime environment failures being ignored during startup.
- Overlong state or manifest lines being silently skipped.
- A duplicated Clang analyzer invocation in the local test harness.
- CI failures and deprecation warnings caused by actions still running on the retired Node.js 20 runtime.

### Performance

- Default launches avoid two temporary-file operations and a helper-path workflow.
- Game Options retains artwork in memory for the lifetime of the panel.
- Shared-ROM deduplication uses bounded hashing rather than quadratic comparisons.

## 0.6.2 — 2026-08-06

### Added

- Separate pull-request CI, Q90 image, and tagged-release GitHub Actions workflows.
- Automated compressed Q90 image assets, release checksums, notes, and provenance attestations.
- Contributor, security, support, code-of-conduct, issue, pull-request, Dependabot, and release-note configuration.
- Dedicated building, releasing, and GitHub repository setup documentation.
- Release verification and changelog-to-release-notes tools.

### Changed

- Reworked the README around installation choices, reproducible builds, porting, safety, and release automation.
- Release packages now use deterministic file ordering and publish the full image as `.img.xz`.
- Packaging cleans stale output, exposes GitHub Actions outputs, and verifies every archive and checksum before publication.

### Fixed

- The previous workflow referenced a nonexistent package-step output when naming artifacts.
- Tag builds could publish a version that did not match `VERSION`.
- Release directories could retain stale assets from older packaging runs.
- Full raw images were uploaded without release-oriented compression or provenance metadata.

## 0.6.1 — 2026-08-06

### Added

- Device-profile button labels, so every page presents the controls that exist
  on the current handheld instead of Q90-only A/B/X wording.
- A theme validator that keeps `theme.ini`, the C fallback palette, preview
  renderer, and console `dialogrc` consistent.
- Temporary hash-based ROM deduplication when several emulator entries share a
  library directory.
- Timer-driven clock and toast wakeups that preserve an event-blocking idle
  loop.

### Changed

- Search adapts when Start and Options share one physical button: Q90 uses that
  button to play, while devices with distinct buttons retain a separate Clear
  action.
- Game Options keeps its current artwork in memory while the panel is open
  instead of reading it from the SD card on every redraw.
- Generic fallback input mappings now match their displayed keyboard labels.
- Maintenance manifest errors and persistent-state read errors are surfaced in
  the interface rather than silently appearing as empty data.
- The library cache format is version 4, invalidating older caches that could
  contain duplicate game paths.

### Fixed

- Unreachable Search actions caused by the Q90 Start/Options key alias.
- Hard-coded Q90 control labels on portable targets.
- Repeated artwork I/O and unnecessary redraw polling.
- Partial high-contrast theming that retained normal-mode accent colors.
- Silent favorites, activity, metadata, override, cache, and tool-manifest
  failures.
- Unknown or overlong platform-profile fields being accepted at runtime.
- Inaccessible duplicate input mappings on generated ports, except the
  intentional Q90 Start/Options alias.
- Generic builds embedding a profile path that was not present in the package.
- Duplicate ROM rows when compatible emulator definitions use the same folder.
- Host-side default key mappings disagreeing with the labels shown on screen.

## 0.6.0 — 2026-08-06

### Added

- Declarative device profiles for display, paths, launcher integration, power,
  input mappings, maintenance manifests, and capabilities.
- Semantic input actions that remove Q90 key constants from shared UI code.
- Aspect-preserving logical-canvas scaling for multiple landscape resolutions.
- Capability-filtered maintenance catalogs loaded from `tools.tsv`.
- Portable `forge-manifest` emulator provider alongside GMenu2X discovery.
- Generic Linux simulator target with sandboxed state, ROMs, tools, and harmless
  emulator commands.
- Multi-target `forge-build` entry point and adapter-specific build metadata.
- Platform validator covering schema, variables, input collisions,
  capabilities, manifests, and providers.
- `new-platform.py` adapter generator and detailed porting/schema guides.
- Portability sanitizer tests for profiles, viewport calculations, tool
  filtering, and portable emulator discovery.

### Changed

- ForgeShell source is split into `core`, `ui`, and `platform` modules.
- The Q90 build is now the first platform adapter rather than the application
  default embedded throughout shared code.
- Q90 Buildroot constants are declared in `platforms/q90/build.env`.
- The interface uses neutral recovery and launcher language on non-GMenu2X
  devices while retaining Q90-specific labels where appropriate.
- Startup metrics include device ID and physical resolution.

### Fixed

- Maintenance panels no longer show unsupported controls on devices lacking the
  required capability.
- Non-4:3 displays no longer stretch the ForgeShell interface; unused space is
  letterboxed.
- Runtime data-root overrides now rebase related profile paths instead of
  leaving mixed host/target directories.
- Emulator child processes receive the frontend compatibility value declared by
  the device profile.
- Full-image rebuild invalidation now includes platform-adapter inputs.

## 0.4.0 — 2026-08-06

- Introduced the ForgeShell SDL launcher, bounded library scanner, cache,
  favorites, search, activity, maintenance integration, dual-launcher recovery,
  and unified Midnight Mint theme.

## 0.3.0

- Rebased the full-image build to the May 2026 MiyooCFW Buildroot snapshot and
  added exact emulator package manifests.

## 0.2.0

- Hardened the overlay installer, backups, CPU profiles, diagnostics, build
  reproducibility, and maintenance theme.
