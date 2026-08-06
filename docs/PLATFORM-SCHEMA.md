# Platform profile schema

Profiles use standard INI sections. The runtime and `validate-platform.py`
reject unknown sections, unknown keys, overlong values, and incomplete
definitions so a typo cannot silently create an unsafe device configuration.

- `[device]`: `id`, `name`, `family`
- `[ui]`: `screen_width`, `screen_height`, `screen_bpp`, `fullscreen`
- `[storage]`: `data_root`, `rom_root`, `home`, `tool_root`
- `[launcher]`: `provider`, `source`, `fallback_command`, `frontend_value`
- `[maintenance]`: `manifest`
- `[performance]`: `cpu_helper`
- `[power]`: `reboot`, `poweroff`
- `[input]`: `up`, `down`, `left`, `right`, `accept`, `back`, `favorite`,
  `options`, `page_left`, `page_right`, `start`, `select`, `power`
- `[labels]`: short on-screen labels for `accept`, `back`, `favorite`, `options`,
  `page_left`, `page_right`, `start`, `select`, and `power`
- `[capabilities]`: `battery`, `cpu_profiles`, `brightness`, `volume`,
  `safe_shutdown`, `storage_health`, `system_info`

Input values may be known SDL 1.2 key names or numeric SDL key codes. Mappings
must be unique except for the supported Start/Options alias used by the Q90.
ForgeShell detects that alias and removes the otherwise unreachable Search Clear
action. Labels must be nonempty, single-line strings of at most 11 UTF-8 bytes.

## Forge emulator manifest

The `forge-manifest` provider reads `systems.ini` sections named
`[system.<id>]`. IDs must start with an alphanumeric character and may contain
letters, numbers, `.`, `_`, or `-`.

Supported keys are:

- required: `exec` and either `romdir` or its alias `selectordir`;
- optional: `title`, `params`, `workdir`, and either `romexts` or its alias
  `selectorfilter`.

A section cannot define both members of an alias pair. Unknown keys, duplicate
IDs or keys, unresolved variables, incomplete systems, and catalogs larger than
the bounded runtime capacity are rejected rather than partially imported.

## Maintenance manifest

`tools.tsv` contains exactly five tab-separated fields per non-comment line:

```text
id    title    subtitle    command    required-capability
```

IDs must be unique across the entire manifest, including entries hidden on the
current device. Commands may use `${tool_root}` and `${home}` only. The required
capability must be one of `always`, `battery`, `cpu_profiles`, `brightness`,
`volume`, `safe_shutdown`, `storage_health`, or `system_info`. Unknown
capabilities and unresolved variables are errors.

## Theme contract

`theme.ini` is transactional. Every supplied role must be known, unique, and
valid. When any line is malformed, ForgeShell discards the complete custom theme
and uses the built-in Midnight Mint defaults, preventing mixed page styles.
Run `python3 tools/validate-theme.py` after changing theme assets or console
`dialogrc` files.
