# ForgeOS release checklist

## Shared source and portability

- [ ] `VERSION`, `FS_VERSION`, README, changelog, previews, and markers agree.
- [ ] `python3 tools/validate-platform.py` passes every included adapter.
- [ ] `./forge-build <target> --check` passes every advertised target.
- [ ] Semantic actions are completely mapped without required-key collisions.
- [ ] Emulator and maintenance manifests parse without duplicate IDs.
- [ ] Unsupported capabilities hide dependent controls and tools.
- [ ] 320x240, 480x272, 640x360, and 640x480 layouts stay in the safe area.
- [ ] No target-specific paths, key constants, or power commands enter shared core.
- [ ] No host objects, analyzer reports, bytecode, ROMs, or BIOS files are packaged.
- [ ] `./tests/run.sh` passes from a fresh source extraction.
- [ ] All archives pass integrity and SHA-256 verification.

## Q90 image

- [ ] Pinned MiyooCFW Buildroot revision is intentional.
- [ ] Clean full cross-build succeeds and the emulator manifest is complete.
- [ ] Emulator manifest reports `Compatibility baseline: PASS`.
- [ ] At least 20 representative physical-Q90 game tests are complete and
      `tools/compatibility-report.py --enforce --minimum-games 20 --minimum-playable 90` passes.
- [ ] Any emulator/core revision change has no documented compatibility or performance regression.
- [ ] Full `HARDWARE-TEST.md` passes on at least one Q90 and spare card.
- [ ] Boot, shutdown, audio, input, framebuffer, battery, brightness, performance,
      save persistence, and unexpected-power-loss tests pass.
- [ ] GMenu2X, SELECT/RESET, Safe Mode, last-known-good, missing-binary, and
      three-failure recovery paths pass.

## New physical adapters

- [ ] Adapter contains a profile, tools manifest, launcher source, build backend,
      recovery path, README, and hardware checklist.
- [ ] Simulator validation is complete before flashing hardware.
- [ ] Physical controls, display, audio, storage, power, suspend, and recovery pass.
- [ ] Capability claims match observed hardware behavior.
- [ ] Device-specific code remains inside its adapter/backend.

## UX and publication

- [ ] Every page, state, dialog, and target uses the same shared theme.
- [ ] First-run setup and recovery are understandable without coaching.
- [ ] Long and Unicode titles remain readable at every supported resolution.
- [ ] Empty, error, progress, safe-mode, and confirmation states are clear.
- [ ] Release notes distinguish simulator-tested, build-tested, and hardware-tested targets.
- [ ] Flashing, recovery, original-card, and spare-card warnings are prominent.

## GitHub publication

- [ ] Default branch requires the CI check before merging.
- [ ] Tag exactly matches `v$(cat VERSION)`.
- [ ] Q90 workflow produced the image, upstream revision, and emulator manifest.
- [ ] Packaged `.img.xz`, source ZIP, overlay ZIP, and `SHA256SUMS` verify cleanly.
- [ ] Release notes match the current changelog section.
- [ ] Provenance attestation succeeded, or its repository-plan limitation is documented.
- [ ] Release is marked prerelease until the physical Q90 checklist passes.
