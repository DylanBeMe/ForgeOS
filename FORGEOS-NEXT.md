# ForgeOS roadmap after 0.6.x

The 0.6.x series establishes the portable platform boundary. The next work should prove
that boundary on hardware rather than adding more abstraction speculatively.

1. Complete Q90 bring-up and correct only adapter-level issues found there.
2. Build and run the generic Linux simulator at every supported CI resolution.
3. Select a second real SDL 1.2 handheld with documented boot and input access.
4. Generate its adapter with `tools/new-platform.py` and avoid changing shared
   core code unless the requirement applies to more than one device.
5. Add device-specific framebuffer, input, power, battery, and build backends.
6. Compare the second port against Q90 to eliminate remaining hidden assumptions.
7. Add per-platform hardware test records and publish capability matrices.
8. Promote the platform API to stable only after two physical adapters pass.

A kernel or bootloader rewrite remains out of scope unless a target requires it.
ForgeOS should reuse each device's proven board-support layer while keeping the
launcher, library, metadata, settings, recovery model, and visual system shared.
