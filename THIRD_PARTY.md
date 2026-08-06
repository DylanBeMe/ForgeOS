# Third-party components

ForgeOS-authored C code, scripts, documentation, and artwork are separate from
the software used to build a complete firmware image.

## Full-image base

The default builder fetches MiyooCFW Buildroot commit:

`8087b52311da5c1e2fa1c50b0b064c07fd174a36`

MiyooCFW Buildroot, Buildroot itself, the Linux kernel, U-Boot, GMenu2X,
RetroArch, libretro cores, standalone emulators, SDL, SDL_ttf, DejaVu fonts,
libraries, and utilities keep their upstream licenses and notices. ForgeOS does
not relicense them.

ForgeShell is ForgeOS-authored and licensed under MIT; its license is stored in
`src/forgeshell/src/LICENSE`.

A successful full build creates `forgeos-q90-0.6.3.img.emulators.txt`, recording
each selected emulator package's source revision and package definition. The
same report is installed as `/mnt/forgeos-emulators.txt` in the image.

The preview renderer uses host-installed fonts only to rasterize screenshots.
No font files are redistributed by this package; the target image obtains its
fonts through the selected Buildroot package.

## ROMs and BIOS

ForgeOS does not include commercial ROMs or proprietary BIOS images. Users are
responsible for supplying only content they are legally entitled to use. Open
firmware files supplied by an upstream emulator package remain governed by that
package's license.
