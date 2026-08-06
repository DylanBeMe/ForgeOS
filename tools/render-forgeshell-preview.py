#!/usr/bin/env python3
"""Render deterministic ForgeShell UI previews from the shipped theme."""
from __future__ import annotations

import configparser
from pathlib import Path
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
THEME_PATH = ROOT / "overlay/board/miyoo/main/forgeshell/theme.ini"
ASSETS = ROOT / "assets"
VERSION = (ROOT / "VERSION").read_text(encoding="utf-8").strip()
ICON_PATH = ROOT / "overlay/board/miyoo/main/apps/forge-tools/icon-shell.png"
FONT_REGULAR = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf")
FONT_BOLD = Path("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf")
PROFILE_PATH = ROOT / "platforms/q90/platform.ini"


def load_labels() -> dict[str, str]:
    parser = configparser.ConfigParser(interpolation=None)
    parser.read(PROFILE_PATH, encoding="utf-8")
    return dict(parser["labels"])


LABELS = load_labels()


def load_theme() -> dict[str, str | int]:
    values: dict[str, str] = {}
    for raw in THEME_PATH.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line or line.startswith(("#", ";", "[")) or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip()
    theme: dict[str, str | int] = {}
    for key in (
        "background", "panel", "panel_alt", "accent", "accent_soft",
        "text", "muted", "danger", "border",
    ):
        theme[key] = values[key]
    for key in ("radius", "font_small", "font_body", "font_title"):
        theme[key] = int(values[key])
    return theme


def font(size: int, bold: bool = False) -> ImageFont.FreeTypeFont:
    return ImageFont.truetype(str(FONT_BOLD if bold else FONT_REGULAR), size)


def fit(draw: ImageDraw.ImageDraw, text: str, max_width: int, fnt: ImageFont.FreeTypeFont) -> str:
    if draw.textlength(text, font=fnt) <= max_width:
        return text
    value = text
    while len(value) > 3 and draw.textlength(value + "…", font=fnt) > max_width:
        value = value[:-1]
    return value + "…"


def panel(draw: ImageDraw.ImageDraw, box: tuple[int, int, int, int], theme: dict[str, str | int],
          fill: str | None = None, outline: str | None = None, width: int = 1) -> None:
    draw.rounded_rectangle(
        box,
        radius=int(theme["radius"]),
        fill=fill or str(theme["panel"]),
        outline=outline or str(theme["border"]),
        width=width,
    )


def header(draw: ImageDraw.ImageDraw, page: str, subtitle: str, active: int,
           theme: dict[str, str | int]) -> None:
    draw.text((10, 4), page, font=font(int(theme["font_title"]), True), fill=str(theme["text"]))
    draw.text((278, 8), "21:42", font=font(int(theme["font_small"])), fill=str(theme["muted"]))
    draw.text((10, 27), fit(draw, subtitle, 240, font(int(theme["font_small"]))),
              font=font(int(theme["font_small"])), fill=str(theme["muted"]))
    for i in range(7):
        color = str(theme["accent"]) if i == active else str(theme["border"])
        draw.rectangle((268 + i * 7, 30, 271 + i * 7, 33), fill=color)
    panel(draw, (7, 40, 312, 200), theme)


def row(draw: ImageDraw.ImageDraw, index: int, title: str, meta: str = "", selected: bool = False,
        favorite: bool = False, theme: dict[str, str | int] | None = None) -> None:
    assert theme is not None
    y = 48 + index * 29
    panel(draw, (13, y, 306, y + 24), theme,
          fill=str(theme["panel_alt"] if selected else theme["panel"]),
          outline=str(theme["accent"] if selected else theme["panel"]), width=1)
    x = 20
    if favorite:
        draw.text((19, y + 4), "★", font=font(9), fill=str(theme["accent"]))
        x = 32
    body = font(int(theme["font_body"]))
    draw.text((x, y + 2), fit(draw, title, 194 if meta else 274, body), font=body,
              fill=str(theme["text"]))
    if meta:
        small = font(int(theme["font_small"]))
        right = fit(draw, meta, 70, small)
        draw.text((298 - draw.textlength(right, font=small), y + 5), right, font=small,
                  fill=str(theme["muted"]))


def footer(draw: ImageDraw.ImageDraw, labels: tuple[str, str, str | None],
           theme: dict[str, str | int], show_pages: bool = True) -> None:
    small = font(int(theme["font_small"]))
    x = 9
    actions = (("accept", labels[0]), ("back", labels[1]), ("favorite", labels[2]))
    for action, label in actions:
        if label is None:
            continue
        button = LABELS[action]
        chip_width = max(15, int(draw.textlength(button, font=small)) + 8)
        draw.rounded_rectangle((x, 213, x + chip_width - 1, 227), radius=3,
                               fill=str(theme["accent_soft"]))
        draw.text((x + 4, 213), button, font=small, fill=str(theme["text"]))
        draw.text((x + chip_width + 4, 213), label, font=small, fill=str(theme["muted"]))
        x += chip_width + 4 + int(draw.textlength(label, font=small)) + 8
    if show_pages:
        pages = f"{LABELS['page_left']}/{LABELS['page_right']} pages"
        pages_width = int(draw.textlength(pages, font=small))
        if x + pages_width <= 312:
            draw.text((312 - pages_width, 214), pages, font=small, fill=str(theme["muted"]))


def page_image(page: str, index: int, theme: dict[str, str | int]) -> Image.Image:
    img = Image.new("RGB", (320, 240), str(theme["background"]))
    draw = ImageDraw.Draw(img)
    if page == "Home":
        header(draw, page, "Fast access to what matters", index, theme)
        row(draw, 0, "Continue", "Metroid Fusion", True, theme=theme)
        row(draw, 1, "Favorites", "12 saved", favorite=True, theme=theme)
        row(draw, 2, "Recent activity", "18 sessions", theme=theme)
        row(draw, 3, "Browse library", "247 games", theme=theme)
        draw.text((20, 174), "Library cache is ready", font=font(10), fill=str(theme["muted"]))
        footer(draw, ("Open", "Home", None), theme)
    elif page == "Library":
        header(draw, page, "Choose a system", index, theme)
        row(draw, 0, "All games", "247 games", theme=theme)
        row(draw, 1, "Game Boy Advance", "61", True, theme=theme)
        row(draw, 2, "Genesis / Mega Drive", "42", theme=theme)
        row(draw, 3, "Super Nintendo", "53", theme=theme)
        row(draw, 4, "PlayStation", "19", theme=theme)
        footer(draw, ("Browse", "Home", None), theme)
    elif page == "Search":
        header(draw, page, "Query: MARIO", index, theme)
        panel(draw, (14, 48, 305, 71), theme, fill=str(theme["panel_alt"]),
              outline=str(theme["accent_soft"]))
        draw.text((25, 53), f"< O >  {LABELS['accept']} add  {LABELS['favorite']} erase  {LABELS['options']} play",
                  font=font(10), fill=str(theme["text"]))
        row(draw, 1, "Super Mario World", "SNES", True, favorite=True, theme=theme)
        row(draw, 2, "Mario Kart: Super Circuit", "GBA", theme=theme)
        row(draw, 3, "Mario's Picross", "GB", theme=theme)
        footer(draw, ("Add", "Back", "Erase"), theme)
    elif page == "Activity":
        header(draw, page, "Recent play sessions", index, theme)
        row(draw, 0, "Metroid Fusion", "38m 12s", True, theme=theme)
        row(draw, 1, "Chrono Trigger", "1h 04m", theme=theme)
        row(draw, 2, "Sonic the Hedgehog 2", "22m 47s", theme=theme)
        row(draw, 3, "Castlevania: SOTN", "47m 31s", theme=theme)
        footer(draw, ("Play again", "Home", None), theme)
    elif page == "Maintenance":
        header(draw, page, "Diagnostics, backups and device care", index, theme)
        row(draw, 0, "System Overview", "Hardware", True, theme=theme)
        row(draw, 1, "Emulator Overview", "Versions", theme=theme)
        row(draw, 2, "Storage Check", "SD health", theme=theme)
        row(draw, 3, "CPU Profile", "Performance", theme=theme)
        row(draw, 4, "Save Backup", "Protection", theme=theme)
        footer(draw, ("Run", "Home", None), theme)
    elif page == "Settings":
        header(draw, page, "Simple defaults and accessibility", index, theme)
        row(draw, 0, "Default launcher", "GMenu2X", True, theme=theme)
        row(draw, 1, "Scan each startup", "Off", theme=theme)
        row(draw, 2, "Large text", "Off", theme=theme)
        row(draw, 3, "High contrast", "Off", theme=theme)
        row(draw, 4, "Metadata and artwork", "On", theme=theme)
        footer(draw, ("Change", "Home", None), theme)
    elif page == "Power":
        header(draw, page, "Safe exits and recovery", index, theme)
        row(draw, 0, "Return to GMenu2X", "Recovery", True, theme=theme)
        row(draw, 1, "Restart", "Flush first", theme=theme)
        row(draw, 2, "Shut down", "Flush first", theme=theme)
        draw.text((20, 146), f"{LABELS['select']} opens recovery. {LABELS['power']} exits ForgeShell.",
                  font=font(10), fill=str(theme["muted"]))
        footer(draw, ("Choose", "Home", None), theme)
    elif page == "First-run Setup":
        draw.text((14, 10), "Welcome to ForgeShell", font=font(18, True), fill=str(theme["text"]))
        panel(draw, (10, 42, 310, 184), theme)
        draw.text((24, 58), "Choose the boot launcher", font=font(13, True), fill=str(theme["accent"]))
        panel(draw, (28, 92, 292, 132), theme, fill=str(theme["panel_alt"]),
              outline=str(theme["accent_soft"]))
        draw.text((116, 102), "ForgeShell", font=font(13), fill=str(theme["text"]))
        draw.text((70, 145), "Left / Right changes the choice", font=font(10), fill=str(theme["muted"]))
        for dot in range(5):
            draw.ellipse((135 + dot * 10, 188, 141 + dot * 10, 194),
                         fill=str(theme["accent"] if dot == 3 else theme["border"]))
        footer(draw, ("Next", "Back", None), theme, show_pages=False)
    else:
        draw.text((10, 5), "Game Options", font=font(18, True), fill=str(theme["text"]))
        draw.text((10, 29), "Golden Sun", font=font(10), fill=str(theme["muted"]))
        panel(draw, (7, 40, 313, 200), theme)
        options = (("Emulator", "Game Boy Advance"), ("CPU profile", "Balanced"),
                   ("Aspect", "Original"), ("Scaling", "Nearest"), ("Frameskip", "1"))
        for number, (label, value) in enumerate(options):
            y = 48 + number * 29
            panel(draw, (13, y, 228, y + 25), theme,
                  fill=str(theme["panel_alt"] if number == 0 else theme["panel"]),
                  outline=str(theme["accent"] if number == 0 else theme["panel"]))
            draw.text((20, y + 3), label, font=font(13), fill=str(theme["text"]))
            draw.text((137, y + 6), fit(draw, value, 83, font(10)), font=font(10), fill=str(theme["muted"]))
        panel(draw, (237, 48, 301, 128), theme, fill=str(theme["panel_alt"]))
        draw.text((248, 80), "NO ART", font=font(10), fill=str(theme["muted"]))
        footer(draw, ("Change", "Back", "Reset"), theme, show_pages=False)
    return img


def render_icon(theme: dict[str, str | int]) -> None:
    img = Image.new("RGBA", (48, 48), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    draw.rounded_rectangle((5, 5, 42, 42), radius=9, fill=str(theme["panel"]),
                           outline=str(theme["accent"]), width=2)
    draw.rounded_rectangle((12, 12, 35, 31), radius=4, fill=str(theme["background"]),
                           outline=str(theme["text"]), width=2)
    draw.rectangle((15, 15, 32, 18), fill=str(theme["accent"]))
    draw.line((18, 25, 29, 25), fill=str(theme["muted"]), width=2)
    draw.line((18, 29, 25, 29), fill=str(theme["muted"]), width=2)
    draw.ellipse((21, 35, 26, 40), fill=str(theme["accent"]))
    ICON_PATH.parent.mkdir(parents=True, exist_ok=True)
    img.save(ICON_PATH)


def render_splash(theme: dict[str, str | int]) -> None:
    img = Image.new("RGB", (320, 240), str(theme["background"]))
    draw = ImageDraw.Draw(img)
    for x in range(0, 321, 16):
        draw.line((x, 0, x, 240), fill=str(theme["panel"]), width=1)
    for y in range(0, 241, 16):
        draw.line((0, y, 320, y), fill=str(theme["panel"]), width=1)
    draw.rounded_rectangle((49, 56, 271, 181), radius=18, fill=str(theme["panel"]),
                           outline=str(theme["accent"]), width=3)
    draw.rectangle((69, 75, 251, 80), fill=str(theme["accent"]))
    draw.text((80, 93), "ForgeOS", font=font(31, True), fill=str(theme["text"]))
    draw.text((94, 131), "FORGESHELL BETA", font=font(12, True), fill=str(theme["accent"]))
    draw.text((103, 151), f"POWKIDDY Q90  •  {VERSION}", font=font(9), fill=str(theme["muted"]))
    draw.text((96, 205), "GMenu2X recovery remains available", font=font(9), fill=str(theme["muted"]))
    img.save(ASSETS / "forgeos-splash.png")
    img.save(ASSETS / "forgeos-splash.bmp")
    img.save(ROOT / "overlay/board/miyoo/boot/miyoo-splash.bmp")


def main() -> None:
    ASSETS.mkdir(parents=True, exist_ok=True)
    theme = load_theme()
    names = ("Home", "Library", "Search", "Activity", "Maintenance", "Settings", "Power",
             "First-run Setup", "Game Options")
    pages = [page_image(name, min(index, 6), theme) for index, name in enumerate(names)]
    pages[0].save(ASSETS / "theme-preview.png")
    montage = Image.new("RGB", (1600, 480), str(theme["background"]))
    for index, page in enumerate(pages):
        montage.paste(page, ((index % 5) * 320, (index // 5) * 240))
    montage.save(ASSETS / "forgeshell-pages.png")
    render_icon(theme)
    render_splash(theme)


if __name__ == "__main__":
    main()
