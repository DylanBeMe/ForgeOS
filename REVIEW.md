# ForgeOS 0.6.3 code, performance, regression, and UX review

## Scope

This review covers ForgeShell shared C code, the Q90 and generic Linux adapters,
configuration and manifest parsers, maintenance scripts, build and release
tooling, GitHub Actions, documentation, generated assets, and update packaging.

## Correctness and recovery fixes

- Made platform, emulator, maintenance, theme, and user-configuration parsing
  strict and consistent. Duplicate keys, unknown fields, malformed sections,
  unresolved variables, invalid values, incomplete records, overlong lines, and
  capacity overflows now fail with an error instead of producing partial state.
- Added transactional theme loading. A bad custom theme can no longer combine
  custom colors with built-in defaults; ForgeShell restores the complete
  Midnight Mint fallback palette.
- Added atomic multi-key configuration updates for onboarding.
- Restored the previous in-memory per-game override when persistence fails.
- Treat failure to write the first-frame recovery handshake as a recovery exit,
  preventing a boot from being incorrectly treated as healthy.
- Rejected unmatched quotes in emulator parameter strings before invoking the
  shell.
- Required complete runtime device profiles instead of silently inheriting
  omitted fields from generic defaults.
- Treat critical environment-export failures as recovery conditions.
- Detected duplicate maintenance IDs even when a previous entry is hidden by a
  disabled capability.

## Compatibility and regression fixes

- Alternate emulators must use the same normalized ROM directory and support the
  selected file. This prevents generic extensions such as `.zip` from offering
  an unrelated arcade, console, or computer emulator.
- The portable validator and runtime now enforce the same systems-manifest key
  set and aliases.
- Emulator catalogs that exceed the bounded device capacity fail explicitly
  instead of silently importing a partial set.
- The Q90 Start/Options alias remains the only intentional duplicate control;
  generated ports must provide reachable semantic actions.
- Existing GMenu2X definitions, recovery launcher, overlay-preservation rules,
  and cache compatibility remain intact.

## Performance review

- Normal game launches no longer create and remove a CPU-state temporary file.
  That path is used only when a non-default profile is requested and an
  executable CPU helper is available.
- Artwork is loaded once when Game Options opens and released when the panel
  closes, avoiding repeated SD-card reads during navigation.
- Shared-folder ROM deduplication uses a bounded temporary hash table.
- Cached startup avoids ROM-directory walks; scanning and UI work remain bounded
  per iteration; idle operation remains event/timer driven.
- Persistent writes remain atomic and are limited to meaningful state changes.

## UX and theme review

- Home, Library, Search, Activity, Maintenance, Settings, Power, onboarding,
  Game Options, modals, empty states, toasts, and console maintenance dialogs
  use the same Midnight Mint roles and typography contract.
- Device profiles supply the button labels shown by the UI.
- Onboarding and Game Options no longer display page-navigation hints because
  those controls are inactive there.
- Search adapts to the Q90 Start/Options key alias while preserving separate
  Play and Clear actions on devices with distinct buttons.
- State and manifest failures are surfaced instead of appearing as unexplained
  empty lists.

## Release engineering review

- CI, Q90 image, and tagged release workflows parse and retain least-purpose job
  separation.
- Tag-to-version validation, deterministic source and overlay packaging,
  checksums, release-note generation, compressed images, and optional artifact
  attestations remain enabled.
- The duplicated local Clang analyzer command was removed.
- Source manifests exclude object files, analyzer output, bytecode, ROMs, BIOS
  files, build output, and generated release directories.

## External validation still required

A GitHub-hosted networked Buildroot run is required to produce and verify the
complete Q90 image. Physical Q90 testing is still required for framebuffer,
controls, audio restoration, battery and brightness paths, CPU scaling, SD-card
timing, emulator return behavior, shutdown, and recovery under power loss.
Until that checklist passes, releases should remain prereleases.
