#!/bin/sh
printf 'ForgeShell generic Linux simulator\n'
printf 'Kernel: '; uname -sr
printf 'Resolution: %s\n' "${FORGESHELL_RESOLUTION:-profile default}"
printf 'Data root: %s\n' "${FORGE_DATA_ROOT:-runtime/generic-linux}"
printf '\nPress Enter to return.\n'
read _answer
