# Contributing

Thank you for improving ForgeOS. Keep pull requests focused and preserve the Q90
recovery path.

Before submitting:

```sh
python3 tools/validate-platform.py
python3 tools/validate-theme.py
./forge-build q90 --check
./forge-build generic-linux --check
./tests/run.sh
```

Device-specific paths, controls, power commands, and sysfs behavior belong in a
platform adapter—not in shared ForgeShell code. New UI pages must use the shared
theme, layout helpers, semantic actions, and controller-readable footer labels.

Do not submit ROMs, proprietary BIOS files, copyrighted artwork without a
compatible license, generated binaries, build trees, or host object files.

Bug reports should include the ForgeOS version, device, installation type,
reproduction steps, expected result, actual result, and relevant logs. Hardware
ports should include a recovery plan and completed device test notes.
