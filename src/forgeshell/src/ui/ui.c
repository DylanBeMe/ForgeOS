#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <SDL/SDL.h>
#include <SDL/SDL_ttf.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef enum {
    PAGE_HOME = 0,
    PAGE_LIBRARY,
    PAGE_SEARCH,
    PAGE_ACTIVITY,
    PAGE_TOOLS,
    PAGE_SETTINGS,
    PAGE_POWER,
    PAGE_COUNT
} FsPage;

typedef enum {
    LIBRARY_SYSTEMS = 0,
    LIBRARY_GAMES
} FsLibraryView;

#define LIBRARY_FILTER_FAVORITES (-2)
#define LIBRARY_FILTER_ALL (-1)

typedef struct {
    SDL_Surface *display;
    SDL_Surface *screen;
    TTF_Font *font_small;
    TTF_Font *font_body;
    TTF_Font *font_title;
    const char *home;
    const FsPlatform *platform;
    FsToolCatalog *tools;
    FsConfig *config;
    FsTheme base_theme;
    FsTheme theme;
    FsLibrary *library;
    FsFavorites *favorites;
    FsSessions *sessions;
    FsOverrides *overrides;
    FsMetadata *metadata;
    int safe_mode;
    FsPage page;
    int selected[PAGE_COUNT];
    FsLibraryView library_view;
    int library_system;
    char search_query[32];
    size_t search_indices[FS_MAX_SEARCH_RESULTS];
    size_t search_count;
    int search_char;
    int running;
    int exit_action;
    int needs_redraw;
    char toast[128];
    Uint32 toast_until;
    SDL_TimerID clock_timer;
    SDL_TimerID toast_timer;
    SDL_Surface *game_options_art;
    int modal_active;
    int modal_action;
    int onboarding_active;
    int onboarding_step;
    int onboarding_choice;
    int game_options_active;
    ssize_t game_options_index;
    int game_options_selection;
    int config_needs_lkg;
} FsUi;

static const char *const page_titles[PAGE_COUNT] = {
    "Home", "Library", "Search", "Activity", "Maintenance", "Settings", "Power"
};

static const char *ui_fallback_name(const FsUi *ui) {
    if (ui != NULL && ui->platform != NULL &&
        fs_casecmp(ui->platform->launcher_provider, "gmenu2x") == 0) return "GMenu2X";
    return "System launcher";
}

static const char search_chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -";

static int ui_run_tool(FsUi *ui, const FsToolEntry *tool);
static int ui_write_config(FsUi *ui, const char *key, const char *value);
static int ui_write_config_many(FsUi *ui, const char *const *keys,
                                const char *const *values, size_t count);

static Uint32 ui_wake_timer(Uint32 interval, void *unused) {
    SDL_Event event;
    (void)unused;
    memset(&event, 0, sizeof(event));
    event.type = SDL_USEREVENT;
    (void)SDL_PushEvent(&event);
    return interval;
}


static void ui_write_scan_metrics(const FsUi *ui) {
    char path[FS_MAX_PATH];
    char content[384];
    int written;
    if (ui == NULL || ui->library == NULL ||
        fs_path_join(path, sizeof(path), ui->home, "state/scan-metrics.ini") != 0) return;
    written = snprintf(content, sizeof(content),
                       "version=%s\nscan_ms=%lld\nsystems=%u\ngames=%u\n"
                       "truncated=%d\ncache_save_failed=%d\n",
                       FS_VERSION, ui->library->last_scan_ms,
                       (unsigned)ui->library->system_count,
                       (unsigned)ui->library->game_count,
                       ui->library->scan_truncated ? 1 : 0,
                       ui->library->cache_save_failed ? 1 : 0);
    if (written > 0 && (size_t)written < sizeof(content)) {
        (void)fs_write_atomic(path, content, (size_t)written, 0644);
    }
}


static Uint32 ui_color(SDL_Surface *surface, uint32_t rgb) {
    return SDL_MapRGB(surface->format,
                      (Uint8)((rgb >> 16U) & 0xFFU),
                      (Uint8)((rgb >> 8U) & 0xFFU),
                      (Uint8)(rgb & 0xFFU));
}

static void ui_fill(FsUi *ui, int x, int y, int w, int h, uint32_t color) {
    SDL_Rect rect;
    rect.x = (Sint16)x;
    rect.y = (Sint16)y;
    rect.w = (Uint16)w;
    rect.h = (Uint16)h;
    (void)SDL_FillRect(ui->screen, &rect, ui_color(ui->screen, color));
}

static void ui_rounded_fill(FsUi *ui, int x, int y, int w, int h,
                            int radius, uint32_t color) {
    int r = radius;
    if (r < 0) r = 0;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;
    if (r == 0) {
        ui_fill(ui, x, y, w, h, color);
        return;
    }
    ui_fill(ui, x + r, y, w - (2 * r), h, color);
    ui_fill(ui, x, y + r, w, h - (2 * r), color);
    ui_fill(ui, x + 2, y + 1, w - 4, 1, color);
    ui_fill(ui, x + 1, y + 2, w - 2, h - 4, color);
    ui_fill(ui, x + 2, y + h - 2, w - 4, 1, color);
}

static void ui_panel(FsUi *ui, int x, int y, int w, int h, uint32_t fill, uint32_t border) {
    int radius = ui->theme.radius;
    ui_rounded_fill(ui, x, y, w, h, radius, border);
    if (w > 2 && h > 2) {
        ui_rounded_fill(ui, x + 1, y + 1, w - 2, h - 2,
                        radius > 0 ? radius - 1 : 0, fill);
    }
}

static void ui_theme_accessibility(FsUi *ui) {
    ui->theme = ui->base_theme;
    if (ui->config->high_contrast) {
        ui->theme.background = 0x000000U;
        ui->theme.panel = 0x101010U;
        ui->theme.panel_alt = 0x202020U;
        ui->theme.accent = 0x7CFFD8U;
        ui->theme.accent_soft = 0x174A3CU;
        ui->theme.text = 0xFFFFFFU;
        ui->theme.muted = 0xD0D0D0U;
        ui->theme.danger = 0xFF8C8CU;
        ui->theme.border = 0xFFFFFFU;
    }
}

static TTF_Font *ui_open_font(const char *preferred, int size) {
    static const char *const fallbacks[] = {
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf",
        "/usr/share/fonts/liberation/LiberationSans-Regular.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"
    };
    size_t i;
    TTF_Font *font = NULL;
    if (preferred != NULL && preferred[0] != '\0') {
        font = TTF_OpenFont(preferred, size);
    }
    for (i = 0U; font == NULL && i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
        font = TTF_OpenFont(fallbacks[i], size);
    }
    return font;
}

static void ui_close_fonts(FsUi *ui) {
    if (ui->font_small != NULL) TTF_CloseFont(ui->font_small);
    if (ui->font_body != NULL) TTF_CloseFont(ui->font_body);
    if (ui->font_title != NULL) TTF_CloseFont(ui->font_title);
    ui->font_small = NULL;
    ui->font_body = NULL;
    ui->font_title = NULL;
}

static int ui_open_fonts(FsUi *ui) {
    int delta = ui->config->large_text ? 2 : 0;
    ui_close_fonts(ui);
    ui->font_small = ui_open_font(ui->theme.font_regular, ui->theme.font_small + delta);
    ui->font_body = ui_open_font(ui->theme.font_regular, ui->theme.font_body + delta);
    ui->font_title = ui_open_font(ui->theme.font_bold, ui->theme.font_title + delta);
    if (ui->font_small == NULL || ui->font_body == NULL || ui->font_title == NULL) {
        ui_close_fonts(ui);
        return -1;
    }
    return 0;
}

static int ui_graphics_init(FsUi *ui) {
    Uint32 flags;
    if (ui == NULL || ui->platform == NULL) return -1;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_TIMER) != 0) return -1;
    if (TTF_Init() != 0) {
        SDL_Quit();
        return -1;
    }
    flags = SDL_SWSURFACE | (ui->platform->fullscreen ? SDL_FULLSCREEN : 0U);
    ui->display = SDL_SetVideoMode(ui->platform->screen_width,
                                   ui->platform->screen_height,
                                   ui->platform->screen_bpp, flags);
    if (ui->display == NULL) {
        TTF_Quit();
        SDL_Quit();
        return -1;
    }
    if (ui->platform->screen_width == FS_LOGICAL_W &&
        ui->platform->screen_height == FS_LOGICAL_H) {
        ui->screen = ui->display;
    } else {
        ui->screen = SDL_CreateRGBSurface(SDL_SWSURFACE, FS_LOGICAL_W,
                                          FS_LOGICAL_H, 32, 0U, 0U, 0U, 0U);
        if (ui->screen == NULL) {
            TTF_Quit();
            SDL_Quit();
            ui->display = NULL;
            return -1;
        }
    }
    SDL_ShowCursor(SDL_DISABLE);
    SDL_EnableKeyRepeat(260, 85);
    ui_theme_accessibility(ui);
    if (ui_open_fonts(ui) != 0) {
        if (ui->screen != ui->display) SDL_FreeSurface(ui->screen);
        TTF_Quit();
        SDL_Quit();
        ui->screen = NULL;
        ui->display = NULL;
        return -1;
    }
    ui->clock_timer = SDL_AddTimer(30000U, ui_wake_timer, NULL);
    return 0;
}

static void ui_present(FsUi *ui) {
    if (ui->screen != ui->display) {
        SDL_Rect destination;
        int x = 0;
        int y = 0;
        int scaled_w = FS_LOGICAL_W;
        int scaled_h = FS_LOGICAL_H;
        (void)fs_platform_compute_viewport(ui->platform->screen_width,
                                           ui->platform->screen_height,
                                           FS_LOGICAL_W, FS_LOGICAL_H,
                                           &x, &y, &scaled_w, &scaled_h);
        destination.x = (Sint16)x;
        destination.y = (Sint16)y;
        destination.w = (Uint16)scaled_w;
        destination.h = (Uint16)scaled_h;
        (void)SDL_FillRect(ui->display, NULL, 0U);
        (void)SDL_SoftStretch(ui->screen, NULL, ui->display, &destination);
    }
    (void)SDL_Flip(ui->display);
}

static void ui_graphics_quit(FsUi *ui) {
    if (ui->toast_timer != NULL) {
        (void)SDL_RemoveTimer(ui->toast_timer);
        ui->toast_timer = NULL;
    }
    if (ui->clock_timer != NULL) {
        (void)SDL_RemoveTimer(ui->clock_timer);
        ui->clock_timer = NULL;
    }
    if (ui->game_options_art != NULL) {
        SDL_FreeSurface(ui->game_options_art);
        ui->game_options_art = NULL;
    }
    ui_close_fonts(ui);
    if (ui->screen != NULL && ui->screen != ui->display) SDL_FreeSurface(ui->screen);
    ui->screen = NULL;
    ui->display = NULL;
    if (TTF_WasInit()) TTF_Quit();
    SDL_Quit();
}

static SDL_Color ui_sdl_color(uint32_t rgb) {
    SDL_Color color;
    color.r = (Uint8)((rgb >> 16U) & 0xFFU);
    color.g = (Uint8)((rgb >> 8U) & 0xFFU);
    color.b = (Uint8)(rgb & 0xFFU);
    color.unused = 0U;
    return color;
}

static void ui_draw_text(FsUi *ui, TTF_Font *font, const char *text,
                         int x, int y, uint32_t color) {
    SDL_Surface *rendered;
    SDL_Rect destination;
    if (font == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    rendered = TTF_RenderUTF8_Blended(font, text, ui_sdl_color(color));
    if (rendered == NULL) {
        return;
    }
    destination.x = (Sint16)x;
    destination.y = (Sint16)y;
    destination.w = 0U;
    destination.h = 0U;
    (void)SDL_BlitSurface(rendered, NULL, ui->screen, &destination);
    SDL_FreeSurface(rendered);
}

static size_t ui_utf8_previous(const char *text, size_t length) {
    if (text == NULL || length == 0U) {
        return 0U;
    }
    length--;
    while (length > 0U && (((unsigned char)text[length] & 0xC0U) == 0x80U)) {
        length--;
    }
    return length;
}

static void ui_text_fit(FsUi *ui, TTF_Font *font, const char *text,
                        int x, int y, int max_width, uint32_t color) {
    char source[FS_MAX_VALUE];
    char candidate[FS_MAX_VALUE];
    int width = 0;
    int height = 0;
    size_t length;
    if (font == NULL || text == NULL || fs_copy(source, sizeof(source), text) != 0) {
        return;
    }
    if (TTF_SizeUTF8(font, source, &width, &height) == 0 && width <= max_width) {
        ui_draw_text(ui, font, source, x, y, color);
        return;
    }
    length = strlen(source);
    while (length > 0U) {
        length = ui_utf8_previous(source, length);
        if (length + 4U > sizeof(candidate)) {
            continue;
        }
        memcpy(candidate, source, length);
        memcpy(candidate + length, "...", 4U);
        if (TTF_SizeUTF8(font, candidate, &width, &height) == 0 && width <= max_width) {
            ui_draw_text(ui, font, candidate, x, y, color);
            return;
        }
    }
}

static int ui_text_fits(TTF_Font *font, const char *text, int max_width) {
    int width = 0;
    int height = 0;
    return font != NULL && text != NULL &&
           TTF_SizeUTF8(font, text, &width, &height) == 0 && width <= max_width;
}

static void ui_text_wrap(FsUi *ui, TTF_Font *font, const char *text,
                         int x, int y, int max_width, int line_height,
                         int max_lines, uint32_t color) {
    char source[FS_MAX_VALUE];
    char line[FS_MAX_VALUE];
    char candidate[FS_MAX_VALUE];
    char remainder[FS_MAX_VALUE];
    char *save = NULL;
    char *word;
    int drawn = 0;
    if (font == NULL || text == NULL || max_lines <= 0 ||
        fs_copy(source, sizeof(source), text) != 0) return;
    line[0] = '\0';
    for (word = strtok_r(source, " ", &save); word != NULL;
         word = strtok_r(NULL, " ", &save)) {
        int written = snprintf(candidate, sizeof(candidate), "%s%s%s",
                               line, line[0] == '\0' ? "" : " ", word);
        if (written < 0 || (size_t)written >= sizeof(candidate)) {
            ui_text_fit(ui, font, line[0] == '\0' ? word : line,
                        x, y + drawn * line_height, max_width, color);
            return;
        }
        if (line[0] == '\0' || ui_text_fits(font, candidate, max_width)) {
            (void)fs_copy(line, sizeof(line), candidate);
            continue;
        }
        ui_text_fit(ui, font, line, x, y + drawn * line_height, max_width, color);
        drawn++;
        if (drawn >= max_lines - 1) {
            written = snprintf(remainder, sizeof(remainder), "%s%s%s",
                               word, save != NULL && save[0] != '\0' ? " " : "",
                               save == NULL ? "" : save);
            ui_text_fit(ui, font,
                        written >= 0 && (size_t)written < sizeof(remainder) ? remainder : word,
                        x, y + drawn * line_height, max_width, color);
            return;
        }
        (void)fs_copy(line, sizeof(line), word);
    }
    if (line[0] != '\0' && drawn < max_lines) {
        ui_text_fit(ui, font, line, x, y + drawn * line_height, max_width, color);
    }
}

static void ui_draw_header(FsUi *ui, const char *subtitle) {
    char clock_text[16];
    time_t now = time(NULL);
    struct tm local;
    size_t i;
    ui_fill(ui, 0, 0, FS_LOGICAL_W, FS_LOGICAL_H, ui->theme.background);
    ui_draw_text(ui, ui->font_title, page_titles[ui->page], 10, 5, ui->theme.text);
    if (ui->safe_mode) {
        ui_panel(ui, 205, 5, 66, 18, ui->theme.panel_alt, ui->theme.danger);
        ui_draw_text(ui, ui->font_small, "SAFE MODE", 212, 8, ui->theme.text);
    }
    if (localtime_r(&now, &local) != NULL) {
        (void)strftime(clock_text, sizeof(clock_text), "%H:%M", &local);
        ui_draw_text(ui, ui->font_small, clock_text, 278, 8, ui->theme.muted);
    }
    if (subtitle != NULL) {
        ui_text_fit(ui, ui->font_small, subtitle, 10, 28, 242, ui->theme.muted);
    }
    for (i = 0U; i < PAGE_COUNT; i++) {
        uint32_t color = (i == (size_t)ui->page) ? ui->theme.accent : ui->theme.border;
        ui_fill(ui, 268 + (int)(i * 7U), 30, 4, 4, color);
    }
    ui_panel(ui, 7, 40, 306, 160, ui->theme.panel, ui->theme.border);
}

static int ui_text_width(TTF_Font *font, const char *text) {
    int width = 0;
    int height = 0;
    if (font == NULL || text == NULL || TTF_SizeUTF8(font, text, &width, &height) != 0) {
        return 0;
    }
    return width;
}

static int ui_button(FsUi *ui, int x, int y, const char *button, const char *label) {
    int chip_width = ui_text_width(ui->font_small, button) + 8;
    int label_width = ui_text_width(ui->font_small, label);
    if (chip_width < 15) chip_width = 15;
    ui_rounded_fill(ui, x, y, chip_width, 15, 3, ui->theme.accent_soft);
    ui_draw_text(ui, ui->font_small, button, x + 4, y + 1, ui->theme.text);
    ui_draw_text(ui, ui->font_small, label, x + chip_width + 4, y + 1, ui->theme.muted);
    return chip_width + 4 + label_width;
}

static const char *ui_action_label(const FsUi *ui, FsAction action) {
    const char *label = fs_platform_action_label(ui == NULL ? NULL : ui->platform, action);
    return label == NULL || label[0] == '\0' ? "?" : label;
}

static void ui_draw_footer(FsUi *ui, const char *primary, const char *secondary,
                           const char *tertiary) {
    char pages[2 * FS_MAX_INPUT_LABEL + 8];
    int x = 9;
    int pages_width;
    ui_fill(ui, 0, 205, FS_LOGICAL_W, 35, ui->theme.background);
    x += ui_button(ui, x, 213, ui_action_label(ui, FS_ACTION_ACCEPT),
                   primary == NULL ? "Open" : primary) + 8;
    if (secondary != NULL && secondary[0] != '\0') {
        x += ui_button(ui, x, 213, ui_action_label(ui, FS_ACTION_BACK), secondary) + 8;
    }
    if (tertiary != NULL && tertiary[0] != '\0') {
        x += ui_button(ui, x, 213, ui_action_label(ui, FS_ACTION_FAVORITE), tertiary) + 8;
    }
    if (!ui->game_options_active && !ui->onboarding_active && !ui->modal_active) {
        (void)snprintf(pages, sizeof(pages), "%s/%s pages",
                       ui_action_label(ui, FS_ACTION_PAGE_LEFT),
                       ui_action_label(ui, FS_ACTION_PAGE_RIGHT));
        pages_width = ui_text_width(ui->font_small, pages);
        if (x + pages_width <= FS_LOGICAL_W - 8) {
            ui_draw_text(ui, ui->font_small, pages, FS_LOGICAL_W - pages_width - 8,
                         214, ui->theme.muted);
        }
    }
}

static void ui_list_row(FsUi *ui, int row, int selected, const char *title,
                        const char *meta, int favorite) {
    int y = 48 + row * 29;
    uint32_t fill = selected ? ui->theme.panel_alt : ui->theme.panel;
    uint32_t border = selected ? ui->theme.accent : ui->theme.panel;
    ui_panel(ui, 13, y, 294, 25, fill, border);
    if (favorite) {
        ui_draw_text(ui, ui->font_small, "★", 19, y + 4, ui->theme.accent);
    }
    ui_text_fit(ui, ui->font_body, title, favorite ? 32 : 20, y + 3,
                meta == NULL ? 274 : 202, ui->theme.text);
    if (meta != NULL) {
        ui_text_fit(ui, ui->font_small, meta, 228, y + 6, 70, ui->theme.muted);
    }
}

static void ui_empty(FsUi *ui, const char *title, const char *detail) {
    ui_text_fit(ui, ui->font_body, title, 22, 84, 276, ui->theme.text);
    ui_text_wrap(ui, ui->font_small, detail, 22, 110, 276, 14, 2, ui->theme.muted);
}

static void ui_toast(FsUi *ui, const char *message) {
    if (message == NULL) {
        ui->toast[0] = '\0';
        return;
    }
    (void)snprintf(ui->toast, sizeof(ui->toast), "%s", message);
    ui->toast_until = SDL_GetTicks() + 2200U;
    if (ui->toast_timer != NULL) {
        (void)SDL_RemoveTimer(ui->toast_timer);
        ui->toast_timer = NULL;
    }
    ui->toast_timer = SDL_AddTimer(250U, ui_wake_timer, NULL);
    ui->needs_redraw = 1;
}

static void ui_draw_toast(FsUi *ui) {
    if (ui->toast[0] != '\0' &&
        (int32_t)(ui->toast_until - SDL_GetTicks()) > 0) {
        ui_panel(ui, 22, 168, 276, 25, ui->theme.panel_alt, ui->theme.accent_soft);
        ui_text_fit(ui, ui->font_small, ui->toast, 31, 174, 258, ui->theme.text);
    }
}

static void ui_draw_modal(FsUi *ui) {
    const char *action = ui->modal_action == FS_EXIT_POWEROFF ? "Shut down now?" : "Restart now?";
    if (!ui->modal_active) {
        return;
    }
    ui_fill(ui, 0, 0, FS_LOGICAL_W, FS_LOGICAL_H, ui->theme.background);
    ui_panel(ui, 35, 72, 250, 94, ui->theme.panel, ui->theme.danger);
    ui_draw_text(ui, ui->font_title, action, 55, 88, ui->theme.text);
    ui_draw_text(ui, ui->font_small,
                 ui->modal_action == FS_EXIT_POWEROFF ?
                 "Changes are flushed before shutdown." :
                 "Changes are flushed before restart.",
                 55, 121, ui->theme.muted);
    (void)ui_button(ui, 69, 143, ui_action_label(ui, FS_ACTION_ACCEPT), "Confirm");
    (void)ui_button(ui, 181, 143, ui_action_label(ui, FS_ACTION_BACK), "Cancel");
}

static int ui_game_matches_filter(const FsUi *ui, const FsGame *game) {
    if (ui->library_system == LIBRARY_FILTER_ALL) {
        return 1;
    }
    if (ui->library_system == LIBRARY_FILTER_FAVORITES) {
        return game->favorite;
    }
    return game->system_index == ui->library_system;
}

static size_t ui_filtered_game_count(FsUi *ui) {
    size_t i;
    size_t count = 0U;
    for (i = 0U; i < ui->library->game_count; i++) {
        if (ui_game_matches_filter(ui, &ui->library->games[i])) {
            count++;
        }
    }
    return count;
}

static ssize_t ui_filtered_game_at(FsUi *ui, size_t position) {
    size_t i;
    size_t seen = 0U;
    for (i = 0U; i < ui->library->game_count; i++) {
        if (ui_game_matches_filter(ui, &ui->library->games[i])) {
            if (seen == position) {
                return (ssize_t)i;
            }
            seen++;
        }
    }
    return -1;
}

static void ui_filtered_game_window(FsUi *ui, size_t first,
                                    ssize_t *indices, size_t capacity) {
    size_t i;
    size_t seen = 0U;
    size_t filled = 0U;
    if (indices == NULL || capacity == 0U) return;
    for (i = 0U; i < capacity; i++) indices[i] = -1;
    for (i = 0U; i < ui->library->game_count && filled < capacity; i++) {
        if (!ui_game_matches_filter(ui, &ui->library->games[i])) continue;
        if (seen >= first) indices[filled++] = (ssize_t)i;
        seen++;
    }
}

static ssize_t ui_home_game_at(FsUi *ui, int selection) {
    size_t i;
    if (selection == 0 && ui->sessions->count > 0U) {
        return fs_library_find_path(ui->library, ui->sessions->items[0].path);
    }
    if (selection == 1) {
        for (i = 0U; i < ui->library->game_count; i++) {
            if (ui->library->games[i].favorite) {
                return (ssize_t)i;
            }
        }
    }
    if (selection == 2 && ui->sessions->count > 0U) {
        return fs_library_find_path(ui->library, ui->sessions->items[0].path);
    }
    return -1;
}

static void ui_draw_home(FsUi *ui) {
    char meta[64];
    size_t favorite_count = 0U;
    size_t i;
    const char *scan_status;
    ssize_t continue_index;
    const char *continue_title;
    ui_draw_header(ui, "Fast access to what matters");
    for (i = 0U; i < ui->library->game_count; i++) favorite_count += ui->library->games[i].favorite ? 1U : 0U;
    continue_index = ui_home_game_at(ui, 0);
    continue_title = continue_index >= 0 &&
                     fs_file_exists(ui->library->games[continue_index].path) ?
                     ui->sessions->items[0].title : "No recent game";
    ui_list_row(ui, 0, ui->selected[PAGE_HOME] == 0, "Continue",
                continue_title, 0);
    (void)snprintf(meta, sizeof(meta), "%u saved", (unsigned)favorite_count);
    ui_list_row(ui, 1, ui->selected[PAGE_HOME] == 1, "Favorites", meta, 1);
    (void)snprintf(meta, sizeof(meta), "%u sessions", (unsigned)ui->sessions->count);
    ui_list_row(ui, 2, ui->selected[PAGE_HOME] == 2, "Recent activity", meta, 0);
    (void)snprintf(meta, sizeof(meta), "%u games", (unsigned)ui->library->game_count);
    ui_list_row(ui, 3, ui->selected[PAGE_HOME] == 3, "Browse library", meta, 0);
    scan_status = ui->library->scan_active ? "Scanning in the background" :
                  (ui->library->scan_start_failed ? "Library scan could not start" :
                   (ui->library->scan_truncated ? "Library scan hit safety limits" :
                   (ui->library->cache_save_failed ? "Library cache could not be saved" :
                    (ui->library->cache_load_failed ? "Library cache needs a rescan" :
                     "Library cache is ready"))));
    ui_text_fit(ui, ui->font_small, scan_status, 20, 174, 280, ui->theme.muted);
    ui_draw_footer(ui, "Open", "", NULL);
}

static void ui_draw_library(FsUi *ui) {
    int selection = ui->selected[PAGE_LIBRARY];
    size_t total;
    int first;
    int row;
    char meta[64];
    if (ui->library_view == LIBRARY_SYSTEMS) {
        size_t system_counts[FS_MAX_SYSTEMS] = {0U};
        size_t i;
        ui_draw_header(ui, "Choose a system");
        total = ui->library->system_count + 1U;
        for (i = 0U; i < ui->library->game_count; i++) {
            int system_index = ui->library->games[i].system_index;
            if (system_index >= 0 && (size_t)system_index < ui->library->system_count) {
                system_counts[system_index]++;
            }
        }
        if (total == 1U) {
            ui_empty(ui, "No emulator systems found", "Check the configured launcher provider or systems manifest.");
        } else {
            first = selection > 3 ? selection - 3 : 0;
            for (row = 0; row < 5 && (size_t)(first + row) < total; row++) {
                if (first + row == 0) {
                    (void)snprintf(meta, sizeof(meta), "%u games", (unsigned)ui->library->game_count);
                    ui_list_row(ui, row, selection == 0, "All games", meta, 0);
                } else {
                    size_t index = (size_t)(first + row - 1);
                    (void)snprintf(meta, sizeof(meta), "%u", (unsigned)system_counts[index]);
                    ui_list_row(ui, row, selection == first + row,
                                ui->library->systems[index].title, meta, 0);
                }
            }
        }
        ui_draw_footer(ui, "Browse", "Home", NULL);
    } else {
        const char *subtitle = ui->library_system == LIBRARY_FILTER_FAVORITES ? "Favorites" :
                               (ui->library_system == LIBRARY_FILTER_ALL ? "All games" :
                                ui->library->systems[ui->library_system].title);
        ui_draw_header(ui, subtitle);
        total = ui_filtered_game_count(ui);
        if (total == 0U) {
            ui_empty(ui, ui->library_system == LIBRARY_FILTER_FAVORITES ?
                     "No favorites yet" : "No games found",
                     ui->library->scan_active ? "Scanning is still running." :
                     (ui->library_system == LIBRARY_FILTER_FAVORITES ?
                      "Use the Favorite action on a game to add it here." :
                      "Check the ROM folder configured by the launcher provider."));
        } else {
            ssize_t visible[5];
            if ((size_t)selection >= total) selection = (int)total - 1;
            first = selection > 3 ? selection - 3 : 0;
            ui_filtered_game_window(ui, (size_t)first, visible, 5U);
            for (row = 0; row < 5 && (size_t)(first + row) < total; row++) {
                ssize_t game_index = visible[row];
                if (game_index >= 0) {
                    FsGame *game = &ui->library->games[game_index];
                    FsSystem *system = &ui->library->systems[game->system_index];
                    ui_list_row(ui, row, selection == first + row, game->title,
                                system->title, game->favorite);
                }
            }
        }
        ui_draw_footer(ui, "Play", "Systems", "Favorite");
    }
}

static void ui_refresh_search(FsUi *ui) {
    if (ui->search_query[0] == '\0') {
        ui->search_count = 0U;
    } else {
        ui->search_count = fs_library_search(ui->library, ui->search_query,
                                             ui->search_indices, FS_MAX_SEARCH_RESULTS);
    }
    if (ui->selected[PAGE_SEARCH] >= (int)ui->search_count) {
        ui->selected[PAGE_SEARCH] = ui->search_count == 0U ? 0 : (int)ui->search_count - 1;
    }
}

static void ui_draw_search(FsUi *ui) {
    char query[64];
    char picker[64];
    int selection = ui->selected[PAGE_SEARCH];
    int first;
    int row;
    (void)snprintf(query, sizeof(query), "Query: %s",
                   ui->search_query[0] == '\0' ? "empty" : ui->search_query);
    ui_draw_header(ui, query);
    if (fs_platform_actions_share_key(ui->platform, FS_ACTION_START, FS_ACTION_OPTIONS)) {
        (void)snprintf(picker, sizeof(picker), "< %c >  %s add  %s erase  %s play",
                       search_chars[ui->search_char],
                       ui_action_label(ui, FS_ACTION_ACCEPT),
                       ui_action_label(ui, FS_ACTION_FAVORITE),
                       ui_action_label(ui, FS_ACTION_OPTIONS));
    } else {
        (void)snprintf(picker, sizeof(picker), "< %c >  %s add  %s erase  %s clear",
                       search_chars[ui->search_char],
                       ui_action_label(ui, FS_ACTION_ACCEPT),
                       ui_action_label(ui, FS_ACTION_FAVORITE),
                       ui_action_label(ui, FS_ACTION_START));
    }
    ui_panel(ui, 14, 48, 292, 24, ui->theme.panel_alt, ui->theme.accent_soft);
    ui_draw_text(ui, ui->font_small, picker, 25, 54, ui->theme.text);
    if (ui->search_count == 0U) {
        ui_empty(ui, ui->search_query[0] == '\0' ? "Search your library" : "No matches",
                 "Choose a character with Left/Right, then use Add.");
    } else {
        first = selection > 2 ? selection - 2 : 0;
        for (row = 0; row < 4 && (size_t)(first + row) < ui->search_count; row++) {
            FsGame *game = &ui->library->games[ui->search_indices[first + row]];
            FsSystem *system = &ui->library->systems[game->system_index];
            int yrow = row + 1;
            ui_list_row(ui, yrow, selection == first + row, game->title,
                        system->title, game->favorite);
        }
    }
    ui_draw_footer(ui, "Add", "Back", "Erase");
    if (!fs_platform_actions_share_key(ui->platform, FS_ACTION_START, FS_ACTION_OPTIONS)) {
        char hint[48];
        (void)snprintf(hint, sizeof(hint), "%s plays",
                       ui_action_label(ui, FS_ACTION_OPTIONS));
        ui_text_fit(ui, ui->font_small, hint, 236, 195, 76, ui->theme.muted);
    }
}

static void ui_draw_activity(FsUi *ui) {
    int selection = ui->selected[PAGE_ACTIVITY];
    int first;
    int row;
    char meta[64];
    ui_draw_header(ui, "Recent play sessions");
    if (ui->sessions->count == 0U) {
        ui_empty(ui, "Nothing played yet", "Sessions appear here after a game exits.");
    } else {
        if ((size_t)selection >= ui->sessions->count) selection = (int)ui->sessions->count - 1;
        first = selection > 3 ? selection - 3 : 0;
        for (row = 0; row < 5 && (size_t)(first + row) < ui->sessions->count; row++) {
            const FsSession *session = &ui->sessions->items[first + row];
            if (session->duration_seconds >= 3600U) {
                (void)snprintf(meta, sizeof(meta), "%uh %02um",
                               session->duration_seconds / 3600U,
                               (session->duration_seconds / 60U) % 60U);
            } else {
                (void)snprintf(meta, sizeof(meta), "%um %02us",
                               session->duration_seconds / 60U,
                               session->duration_seconds % 60U);
            }
            ui_list_row(ui, row, selection == first + row, session->title,
                        meta, 0);
        }
    }
    ui_draw_footer(ui, "Play again", "Home", NULL);
}

static void ui_draw_tools(FsUi *ui) {
    int selection = ui->selected[PAGE_TOOLS];
    int first = selection > 3 ? selection - 3 : 0;
    int row;
    const int count = ui->tools == NULL ? 0 : (int)ui->tools->count;
    ui_draw_header(ui, "Diagnostics, backups and device care");
    for (row = 0; row < 5 && first + row < count; row++) {
        const FsToolEntry *tool = &ui->tools->items[first + row];
        ui_list_row(ui, row, selection == first + row, tool->title, tool->meta, 0);
    }
    if (count == 0) {
        const char *message = ui->tools != NULL && ui->tools->load_failed ?
            "Maintenance tools could not be loaded. Open recovery and check the tool manifest." :
            "No maintenance tools are enabled for this device.";
        ui_text_fit(ui, ui->font_body, message, 20, 76, 280,
                    ui->tools != NULL && ui->tools->load_failed ? ui->theme.danger : ui->theme.muted);
    }
    ui_draw_footer(ui, "Run", "Home", NULL);
}

static const char *ui_on_off(int value) {
    return value ? "On" : "Off";
}

static void ui_draw_settings(FsUi *ui) {
    static const char *const labels[] = {
        "Default launcher", "Scan each startup", "Large text",
        "High contrast", "Metadata and artwork", "Safe mode next boot",
        "Recovery hint", "Rescan library", "Run setup again"
    };
    int selection = ui->selected[PAGE_SETTINGS];
    int first = selection > 3 ? selection - 3 : 0;
    int row;
    ui_draw_header(ui, "Simple defaults and accessibility");
    for (row = 0; row < 5 && first + row < 9; row++) {
        const char *value = "Run";
        switch (first + row) {
            case 0: value = fs_casecmp(ui->config->launcher_mode, "forgeshell") == 0 ? "ForgeShell" : ui_fallback_name(ui); break;
            case 1: value = ui_on_off(ui->config->scan_on_start); break;
            case 2: value = ui_on_off(ui->config->large_text); break;
            case 3: value = ui_on_off(ui->config->high_contrast); break;
            case 4: value = ui_on_off(ui->config->metadata_enabled); break;
            case 5: value = ui_on_off(ui->config->safe_mode_next_boot); break;
            case 6: value = ui_on_off(ui->config->show_recovery_hint); break;
            case 8: value = "Start"; break;
            default: break;
        }
        ui_list_row(ui, row, selection == first + row, labels[first + row], value, 0);
    }
    ui_draw_footer(ui, "Change", "Home", NULL);
}

static void ui_draw_power(FsUi *ui) {
    int selection = ui->selected[PAGE_POWER];
    ui_draw_header(ui, "Safe exits and recovery");
    ui_list_row(ui, 0, selection == 0, ui_fallback_name(ui), "Recovery", 0);
    if (ui->platform->cap_safe_shutdown) {
        ui_list_row(ui, 1, selection == 1, "Restart", "Flush first", 0);
        ui_list_row(ui, 2, selection == 2, "Shut down", "Flush first", 0);
    } else {
        ui_text_fit(ui, ui->font_small,
                    "Restart and shutdown are managed by this device's system launcher.",
                    20, 106, 280, ui->theme.muted);
    }
    {
        char hint[128];
        (void)snprintf(hint, sizeof(hint), "%s opens recovery. %s exits ForgeShell.",
                       ui_action_label(ui, FS_ACTION_SELECT),
                       ui_action_label(ui, FS_ACTION_POWER));
        ui_text_fit(ui, ui->font_small, hint, 20, 146, 280, ui->theme.muted);
    }
    ui_draw_footer(ui, "Choose", "Home", NULL);
}

static void ui_draw_setup_dots(FsUi *ui, int step, int count) {
    int i;
    int start = 160 - ((count * 10) / 2);
    for (i = 0; i < count; i++) {
        ui_rounded_fill(ui, start + i * 10, 188, 6, 6, 3,
                        i == step ? ui->theme.accent : ui->theme.border);
    }
}

static void ui_draw_onboarding(FsUi *ui) {
    const int count = 5;
    ui_fill(ui, 0, 0, FS_LOGICAL_W, FS_LOGICAL_H, ui->theme.background);
    ui_draw_text(ui, ui->font_title, "Welcome to ForgeShell", 14, 10, ui->theme.text);
    ui_panel(ui, 10, 42, 300, 142, ui->theme.panel, ui->theme.border);
    switch (ui->onboarding_step) {
        case 0:
            {
                char device_line[FS_MAX_TITLE + 24];
                (void)snprintf(device_line, sizeof(device_line), "A fresh %s home screen",
                               ui->platform->device_name);
                ui_text_fit(ui, ui->font_body, device_line, 24, 58, 270, ui->theme.accent);
            }
            ui_text_wrap(ui, ui->font_small,
                         "ForgeShell keeps the proven hardware layer while replacing the visible launcher and settings experience.",
                         24, 86, 270, 14, 3, ui->theme.text);
            ui_text_wrap(ui, ui->font_small,
                         "The original system launcher stays available as a recovery path.",
                         24, 132, 270, 14, 2, ui->theme.muted);
            break;
        case 1:
            ui_draw_text(ui, ui->font_body, "Build your library", 24, 58, ui->theme.accent);
            ui_text_wrap(ui, ui->font_small,
                         "ForgeShell reads this device's emulator provider and scans its configured ROM folders.",
                         24, 86, 270, 14, 3, ui->theme.text);
            ui_text_fit(ui, ui->font_small,
                        ui->library->scan_active ? "Scanning is running in the background." :
                        "Use Next to start a bounded scan now.",
                        24, 134, 270, ui->theme.muted);
            break;
        case 2:
            ui_draw_text(ui, ui->font_body, "Know the recovery controls", 24, 58, ui->theme.accent);
            {
                char recovery[192];
                (void)snprintf(recovery, sizeof(recovery),
                               "%s opens recovery. The boot adapter can also recover after repeated startup failures.",
                               ui_action_label(ui, FS_ACTION_SELECT));
                ui_text_wrap(ui, ui->font_small, recovery, 24, 86, 270, 14, 3, ui->theme.text);
            }
            ui_text_wrap(ui, ui->font_small,
                         "Safe mode disables scans, metadata and per-game overrides.",
                         24, 136, 270, 14, 2, ui->theme.muted);
            break;
        case 3:
            ui_draw_text(ui, ui->font_body, "Choose the boot launcher", 24, 58, ui->theme.accent);
            ui_panel(ui, 28, 92, 264, 40, ui->theme.panel_alt, ui->theme.accent_soft);
            ui_draw_text(ui, ui->font_body,
                         ui->onboarding_choice ? "ForgeShell" : ui_fallback_name(ui),
                         116, 102, ui->theme.text);
            ui_draw_text(ui, ui->font_small, "Left / Right changes the choice", 70, 145,
                         ui->theme.muted);
            break;
        default:
            ui_draw_text(ui, ui->font_body, "Protect your progress", 24, 58, ui->theme.accent);
            ui_text_wrap(ui, ui->font_small,
                         "Run Save Backup now, or finish setup and create one later from Maintenance.",
                         24, 86, 270, 14, 3, ui->theme.text);
            ui_text_fit(ui, ui->font_small,
                        "Choose Backup, or use Finish to continue.", 24, 138, 270, ui->theme.muted);
            break;
    }
    ui_draw_setup_dots(ui, ui->onboarding_step, count);
    ui_fill(ui, 0, 205, FS_LOGICAL_W, 35, ui->theme.background);
    (void)ui_button(ui, 9, 213, ui_action_label(ui, FS_ACTION_ACCEPT),
                    ui->onboarding_step == 4 ? "Backup" : "Next");
    (void)ui_button(ui, 108, 213, ui_action_label(ui, FS_ACTION_BACK),
                    ui->onboarding_step == 0 ? "Recovery" : "Back");
    if (ui->onboarding_step == 4) {
        (void)ui_button(ui, 213, 213, ui_action_label(ui, FS_ACTION_FAVORITE), "Finish");
    }
}

static const FsGameOverride *ui_game_override(const FsUi *ui, const FsGame *game) {
    if (ui->safe_mode || ui->overrides == NULL || game == NULL) return NULL;
    return fs_overrides_find(ui->overrides, game->path);
}

static const FsSystem *ui_override_system(const FsUi *ui, const FsGame *game,
                                          const FsGameOverride *override) {
    size_t i;
    if (game == NULL || game->system_index < 0 ||
        (size_t)game->system_index >= ui->library->system_count) return NULL;
    if (override != NULL && override->emulator_id[0] != '\0') {
        const FsSystem *source = &ui->library->systems[game->system_index];
        for (i = 0U; i < ui->library->system_count; i++) {
            if (strcmp(ui->library->systems[i].id, override->emulator_id) == 0 &&
                fs_library_systems_compatible(source, &ui->library->systems[i],
                                              game->path)) {
                return &ui->library->systems[i];
            }
        }
    }
    return &ui->library->systems[game->system_index];
}

static const char *ui_frameskip_name(int value) {
    switch (value) {
        case 0: return "Off";
        case 1: return "1";
        case 2: return "2";
        case 5: return "Auto";
        default: return "Default";
    }
}

static const char *ui_option_name(const char *value) {
    if (value == NULL || value[0] == '\0' || fs_casecmp(value, "default") == 0) return "Default";
    if (fs_casecmp(value, "eco") == 0) return "Eco";
    if (fs_casecmp(value, "balanced") == 0) return "Balanced";
    if (fs_casecmp(value, "performance") == 0) return "Performance";
    if (fs_casecmp(value, "original") == 0) return "Original";
    if (fs_casecmp(value, "fullscreen") == 0) return "Fullscreen";
    if (fs_casecmp(value, "nearest") == 0) return "Nearest";
    if (fs_casecmp(value, "smooth") == 0) return "Smooth";
    return value;
}

static void ui_clear_game_options_art(FsUi *ui) {
    if (ui->game_options_art != NULL) {
        SDL_FreeSurface(ui->game_options_art);
        ui->game_options_art = NULL;
    }
}

static void ui_load_game_options_art(FsUi *ui, const FsGame *game) {
    ui_clear_game_options_art(ui);
    if (game == NULL || game->art_path[0] == '\0' || !fs_file_exists(game->art_path)) return;
    ui->game_options_art = SDL_LoadBMP(game->art_path);
}

static void ui_draw_game_options(FsUi *ui) {
    FsGame *game;
    const FsGameOverride *override;
    const FsSystem *system;
    const char *values[6];
    int first;
    int row;
    if (ui->game_options_index < 0 ||
        (size_t)ui->game_options_index >= ui->library->game_count) {
        ui->game_options_active = 0;
        return;
    }
    game = &ui->library->games[ui->game_options_index];
    override = ui_game_override(ui, game);
    system = ui_override_system(ui, game, override);
    values[0] = system == NULL ? "Unavailable" : system->title;
    values[1] = override == NULL ? "Default" : ui_option_name(override->cpu_profile);
    values[2] = override == NULL ? "Default" : ui_option_name(override->aspect);
    values[3] = override == NULL ? "Default" : ui_option_name(override->scaling);
    values[4] = override == NULL ? "Default" : ui_frameskip_name(override->frameskip);
    values[5] = override != NULL && override->bios_path[0] != '\0' ? "Imported" : "Default";
    ui_fill(ui, 0, 0, FS_LOGICAL_W, FS_LOGICAL_H, ui->theme.background);
    ui_draw_text(ui, ui->font_title, "Game Options", 10, 5, ui->theme.text);
    ui_text_fit(ui, ui->font_small, game->title, 10, 29, 220, ui->theme.muted);
    ui_panel(ui, 7, 40, 306, 160, ui->theme.panel, ui->theme.border);
    if (ui->game_options_art != NULL) {
        SDL_Rect src = {0, 0, 64, 80};
        SDL_Rect dst = {237, 48, 64, 80};
        (void)SDL_BlitSurface(ui->game_options_art, &src, ui->screen, &dst);
    } else {
        ui_panel(ui, 237, 48, 64, 80, ui->theme.panel_alt, ui->theme.border);
        ui_draw_text(ui, ui->font_small, "NO ART", 248, 80, ui->theme.muted);
    }
    first = ui->game_options_selection > 3 ? ui->game_options_selection - 3 : 0;
    for (row = 0; row < 5 && first + row < 6; row++) {
        static const char *const labels[] = {
            "Emulator", "CPU profile", "Aspect", "Scaling", "Frameskip", "BIOS"
        };
        int y = 48 + row * 29;
        int selected = ui->game_options_selection == first + row;
        ui_panel(ui, 13, y, 215, 25,
                 selected ? ui->theme.panel_alt : ui->theme.panel,
                 selected ? ui->theme.accent : ui->theme.panel);
        ui_text_fit(ui, ui->font_body, labels[first + row], 20, y + 3, 118, ui->theme.text);
        ui_text_fit(ui, ui->font_small, values[first + row], 137, y + 6, 83, ui->theme.muted);
    }
    ui_draw_footer(ui, "Change", "Back", "Reset");
}

static void ui_commit_last_good(FsUi *ui) {
    char path[FS_MAX_PATH];
    if (ui == NULL || ui->safe_mode || !ui->config_needs_lkg) return;
    if (fs_path_join(path, sizeof(path), ui->home, "config.ini") == 0 &&
        fs_config_save_last_good(path) == 0) {
        ui->config_needs_lkg = 0;
    }
}

static void ui_draw(FsUi *ui) {
    if (ui->onboarding_active) {
        ui_draw_onboarding(ui);
        ui_draw_toast(ui);
        ui_present(ui);
        ui_commit_last_good(ui);
        ui->needs_redraw = 0;
        return;
    }
    if (ui->game_options_active) {
        ui_draw_game_options(ui);
        ui_draw_toast(ui);
        ui_present(ui);
        ui_commit_last_good(ui);
        ui->needs_redraw = 0;
        return;
    }
    switch (ui->page) {
        case PAGE_HOME: ui_draw_home(ui); break;
        case PAGE_LIBRARY: ui_draw_library(ui); break;
        case PAGE_SEARCH: ui_draw_search(ui); break;
        case PAGE_ACTIVITY: ui_draw_activity(ui); break;
        case PAGE_TOOLS: ui_draw_tools(ui); break;
        case PAGE_SETTINGS: ui_draw_settings(ui); break;
        case PAGE_POWER: ui_draw_power(ui); break;
        default: break;
    }
    ui_draw_toast(ui);
    ui_draw_modal(ui);
    ui_present(ui);
    ui_commit_last_good(ui);
    ui->needs_redraw = 0;
}

static int ui_page_item_count(FsUi *ui) {
    switch (ui->page) {
        case PAGE_HOME: return 4;
        case PAGE_LIBRARY:
            return ui->library_view == LIBRARY_SYSTEMS ? (int)ui->library->system_count + 1 : (int)ui_filtered_game_count(ui);
        case PAGE_SEARCH: return (int)ui->search_count;
        case PAGE_ACTIVITY: return (int)ui->sessions->count;
        case PAGE_TOOLS: return ui->tools == NULL ? 0 : (int)ui->tools->count;
        case PAGE_SETTINGS: return 9;
        case PAGE_POWER: return ui->platform->cap_safe_shutdown ? 3 : 1;
        default: return 0;
    }
}

static void ui_move_selection(FsUi *ui, int delta) {
    int count = ui_page_item_count(ui);
    int *selection = &ui->selected[ui->page];
    if (count <= 0) {
        *selection = 0;
        return;
    }
    *selection += delta;
    if (*selection < 0) *selection = count - 1;
    if (*selection >= count) *selection = 0;
    ui->needs_redraw = 1;
}

static void ui_change_page(FsUi *ui, int delta) {
    int next = (int)ui->page + delta;
    if (next < 0) next = PAGE_COUNT - 1;
    if (next >= PAGE_COUNT) next = 0;
    ui->page = (FsPage)next;
    ui->needs_redraw = 1;
}

static void ui_clamp_current_selection(FsUi *ui) {
    int count = ui_page_item_count(ui);
    int *selection = &ui->selected[ui->page];
    if (count <= 0) {
        *selection = 0;
    } else if (*selection >= count) {
        *selection = count - 1;
    } else if (*selection < 0) {
        *selection = 0;
    }
}

static void ui_apply_favorites(FsUi *ui) {
    fs_library_apply_favorites(ui->library, ui->favorites);
    ui_refresh_search(ui);
    ui_clamp_current_selection(ui);
}

static int ui_launch_index(FsUi *ui, ssize_t game_index) {
    FsGame *game;
    const FsSystem *system;
    const FsGameOverride *override;
    FsSession session;
    int result;
    int session_saved;
    if (game_index < 0 || (size_t)game_index >= ui->library->game_count) {
        ui_toast(ui, "That game is no longer in the library");
        return -1;
    }
    game = &ui->library->games[game_index];
    if (!fs_file_exists(game->path)) {
        ui_toast(ui, "Game file is missing; rescan the library");
        return -1;
    }
    if (game->system_index < 0 ||
        (size_t)game->system_index >= ui->library->system_count) {
        ui_toast(ui, "That emulator entry is no longer available");
        return -1;
    }
    override = ui_game_override(ui, game);
    system = ui_override_system(ui, game, override);
    if (system == NULL) {
        ui_toast(ui, "No compatible emulator is available");
        return -1;
    }
    ui_graphics_quit(ui);
    result = fs_runner_launch_override(system, game, override, &session);
    session_saved = fs_sessions_append(ui->sessions, &session) == 0;
    if (ui_graphics_init(ui) != 0) {
        ui->running = 0;
        ui->exit_action = FS_EXIT_RECOVERY;
        return -1;
    }
    ui->needs_redraw = 1;
    if (!session_saved) {
        ui_toast(ui, "Session log could not be saved");
    } else if (result != 0) {
        char message[96];
        (void)snprintf(message, sizeof(message), "Game exited with status %d", result);
        ui_toast(ui, message);
    } else {
        ui_toast(ui, "Session saved");
    }
    return result;
}

static int ui_run_tool(FsUi *ui, const FsToolEntry *tool) {
    pid_t child;
    int status = 0;
    int result = -1;
    if (tool == NULL || tool->command[0] == '\0') {
        ui_toast(ui, "This maintenance tool is not available");
        return -1;
    }
    ui_graphics_quit(ui);
    child = fork();
    if (child == 0) {
        execl("/bin/sh", "sh", "-c", tool->command, (char *)NULL);
        _exit(127);
    }
    if (child > 0) {
        pid_t waited;
        do {
            waited = waitpid(child, &status, 0);
        } while (waited < 0 && errno == EINTR);
        if (waited == child) {
            if (WIFEXITED(status)) {
                result = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                result = 128 + WTERMSIG(status);
            }
        }
    }
    if (ui_graphics_init(ui) != 0) {
        ui->running = 0;
        ui->exit_action = FS_EXIT_RECOVERY;
        return -1;
    }
    ui->needs_redraw = 1;
    if (child < 0) {
        ui_toast(ui, "Could not start the maintenance tool");
    } else if (result == 0) {
        ui_toast(ui, "Maintenance tool closed");
    } else {
        char message[96];
        (void)snprintf(message, sizeof(message), "Tool exited with status %d", result);
        ui_toast(ui, message);
    }
    return result;
}

static const FsToolEntry *ui_find_tool(const FsUi *ui, const char *id) {
    size_t i;
    if (ui == NULL || ui->tools == NULL || id == NULL) return NULL;
    for (i = 0U; i < ui->tools->count; i++) {
        if (strcmp(ui->tools->items[i].id, id) == 0) return &ui->tools->items[i];
    }
    return NULL;
}

static int ui_finish_onboarding(FsUi *ui) {
    const char *launcher = ui->onboarding_choice ? "forgeshell" : "gmenu2x";
    const char *keys[] = {"launcher_mode", "onboarding_complete"};
    const char *values[] = {launcher, "1"};
    if (ui_write_config_many(ui, keys, values, 2U) != 0) return -1;
    (void)fs_copy(ui->config->launcher_mode, sizeof(ui->config->launcher_mode), launcher);
    ui->config->onboarding_complete = 1;
    ui->onboarding_active = 0;
    ui_toast(ui, ui->onboarding_choice ?
             "Setup complete; ForgeShell starts next boot" :
             "Setup complete; the system launcher remains the default");
    return 0;
}

static void ui_handle_onboarding(FsUi *ui, FsAction action) {
    if (action == FS_ACTION_SELECT) {
        ui->exit_action = FS_EXIT_RECOVERY;
        ui->running = 0;
    } else if (action == FS_ACTION_POWER) {
        ui->exit_action = FS_EXIT_GMENU;
        ui->running = 0;
    } else if (action == FS_ACTION_BACK) {
        if (ui->onboarding_step > 0) ui->onboarding_step--;
        else { ui->running = 0; ui->exit_action = FS_EXIT_GMENU; }
        ui->needs_redraw = 1;
    } else if ((action == FS_ACTION_LEFT || action == FS_ACTION_RIGHT) && ui->onboarding_step == 3) {
        ui->onboarding_choice = !ui->onboarding_choice;
        ui->needs_redraw = 1;
    } else if (action == FS_ACTION_FAVORITE && ui->onboarding_step == 4) {
        (void)ui_finish_onboarding(ui);
        ui->needs_redraw = 1;
    } else if (action == FS_ACTION_ACCEPT || action == FS_ACTION_OPTIONS) {
        if (ui->onboarding_step == 1 && !ui->library->scan_active &&
            fs_library_start_scan(ui->library) != 0) {
            ui_toast(ui, "Library scan could not start; try Safe Mode or recovery");
            ui->needs_redraw = 1;
            return;
        }
        if (ui->onboarding_step == 4) {
            {
                const FsToolEntry *backup = ui_find_tool(ui, "backup-saves");
                if (backup != NULL) (void)ui_run_tool(ui, backup);
                else ui_toast(ui, "Save backup is not available on this device");
            }
        } else if (ui->onboarding_step < 4) {
            ui->onboarding_step++;
        }
        ui->needs_redraw = 1;
    }
}

static FsGameOverride *ui_mutable_override(FsUi *ui, FsGame *game) {
    if (ui->safe_mode || ui->overrides == NULL || game == NULL) return NULL;
    return fs_overrides_get(ui->overrides, game->path, 1);
}

static int ui_save_override(FsUi *ui) {
    if (fs_overrides_save(ui->overrides) != 0) {
        ui_toast(ui, "Could not save game options");
        return -1;
    }
    ui_toast(ui, "Game options saved");
    return 0;
}

static void ui_cycle_override_string(char *value, size_t value_size,
                                     const char *const *choices, size_t count,
                                     int direction) {
    size_t i;
    size_t current = 0U;
    size_t next;
    for (i = 0U; i < count; i++) {
        if (fs_casecmp(value, choices[i]) == 0) {
            current = i;
            break;
        }
    }
    if (direction < 0) {
        next = current == 0U ? count - 1U : current - 1U;
    } else {
        next = (current + 1U) % count;
    }
    (void)fs_copy(value, value_size, choices[next]);
}

static int ui_cycle_emulator(FsUi *ui, FsGame *game, FsGameOverride *override,
                             int direction) {
    size_t i;
    size_t candidates[FS_MAX_SYSTEMS];
    size_t count = 0U;
    size_t position = 0U;
    size_t next;
    for (i = 0U; i < ui->library->system_count; i++) {
        const FsSystem *source = &ui->library->systems[game->system_index];
        if ((int)i != game->system_index &&
            fs_library_systems_compatible(source, &ui->library->systems[i],
                                          game->path)) {
            candidates[count++] = i;
        }
    }
    if (count == 0U) {
        ui_toast(ui, "No alternate emulator for this ROM folder");
        return 0;
    }
    if (override->emulator_id[0] != '\0') {
        for (i = 0U; i < count; i++) {
            if (strcmp(ui->library->systems[candidates[i]].id, override->emulator_id) == 0) {
                position = i + 1U;
                break;
            }
        }
    }
    if (direction < 0) {
        next = position == 0U ? count : position - 1U;
    } else {
        next = (position + 1U) % (count + 1U);
    }
    if (next == 0U) {
        override->emulator_id[0] = '\0';
    } else {
        (void)fs_copy(override->emulator_id, sizeof(override->emulator_id),
                      ui->library->systems[candidates[next - 1U]].id);
    }
    return 1;
}

static void ui_change_game_option(FsUi *ui, int direction) {
    static const char *const cpu[] = {"default", "eco", "balanced", "performance"};
    static const char *const aspect[] = {"default", "original", "4:3", "fullscreen"};
    static const char *const scaling[] = {"default", "nearest", "smooth"};
    static const int frameskip[] = {-1, 0, 1, 2, 5};
    FsGame *game;
    FsGameOverride *override;
    FsGameOverride previous;
    size_t previous_count;
    size_t i;
    int changed = 1;
    if (ui->game_options_index < 0 ||
        (size_t)ui->game_options_index >= ui->library->game_count) return;
    game = &ui->library->games[ui->game_options_index];
    previous_count = ui->overrides == NULL ? 0U : ui->overrides->count;
    override = ui_mutable_override(ui, game);
    if (override == NULL) {
        ui_toast(ui, ui->safe_mode ? "Game options are disabled in safe mode" :
                 "Could not create a game override");
        return;
    }
    previous = *override;
    switch (ui->game_options_selection) {
        case 0: changed = ui_cycle_emulator(ui, game, override, direction); break;
        case 1: ui_cycle_override_string(override->cpu_profile,
                                         sizeof(override->cpu_profile), cpu, 4U, direction); break;
        case 2: ui_cycle_override_string(override->aspect,
                                         sizeof(override->aspect), aspect, 4U, direction); break;
        case 3: ui_cycle_override_string(override->scaling,
                                         sizeof(override->scaling), scaling, 3U, direction); break;
        case 4:
            for (i = 0U; i < sizeof(frameskip) / sizeof(frameskip[0]); i++) {
                if (override->frameskip == frameskip[i]) {
                    size_t count = sizeof(frameskip) / sizeof(frameskip[0]);
                    size_t next = direction < 0 ? (i == 0U ? count - 1U : i - 1U) :
                                                  (i + 1U) % count;
                    override->frameskip = frameskip[next];
                    break;
                }
            }
            break;
        case 5:
            if (override->bios_path[0] != '\0') {
                override->bios_path[0] = '\0';
            } else {
                if (ui->overrides->count > previous_count) ui->overrides->count = previous_count;
                else *override = previous;
                ui_toast(ui, "Import a BIOS path with the desktop index tool");
                return;
            }
            break;
        default: return;
    }
    if (!changed) {
        if (ui->overrides->count > previous_count) ui->overrides->count = previous_count;
        return;
    }
    if (ui_save_override(ui) != 0) {
        if (ui->overrides->count > previous_count) {
            ui->overrides->count = previous_count;
        } else {
            *override = previous;
        }
    }
    ui->needs_redraw = 1;
}

static void ui_reset_game_options(FsUi *ui) {
    FsGame *game;
    if (ui->game_options_index < 0 ||
        (size_t)ui->game_options_index >= ui->library->game_count) return;
    game = &ui->library->games[ui->game_options_index];
    if (fs_overrides_reset(ui->overrides, game->path) == 0) {
        ui_toast(ui, "Game options reset");
    } else {
        ui_toast(ui, "Could not reset game options");
    }
    ui->needs_redraw = 1;
}

static void ui_handle_game_options(FsUi *ui, FsAction action) {
    if (action == FS_ACTION_SELECT) {
        ui->running = 0;
        ui->exit_action = FS_EXIT_RECOVERY;
    } else if (action == FS_ACTION_POWER || action == FS_ACTION_BACK) {
        ui->game_options_active = 0;
        ui_clear_game_options_art(ui);
        ui->needs_redraw = 1;
    } else if (action == FS_ACTION_UP) {
        ui->game_options_selection--;
        if (ui->game_options_selection < 0) ui->game_options_selection = 5;
        ui->needs_redraw = 1;
    } else if (action == FS_ACTION_DOWN) {
        ui->game_options_selection++;
        if (ui->game_options_selection > 5) ui->game_options_selection = 0;
        ui->needs_redraw = 1;
    } else if (action == FS_ACTION_ACCEPT || action == FS_ACTION_RIGHT) {
        ui_change_game_option(ui, 1);
    } else if (action == FS_ACTION_LEFT) {
        ui_change_game_option(ui, -1);
    } else if (action == FS_ACTION_FAVORITE) {
        ui_reset_game_options(ui);
    }
}

static void ui_open_game_options(FsUi *ui, ssize_t index) {
    if (index < 0 || (size_t)index >= ui->library->game_count) return;
    if (ui->safe_mode) {
        ui_toast(ui, "Game options are disabled in safe mode");
        return;
    }
    ui->game_options_index = index;
    ui->game_options_selection = 0;
    ui_load_game_options_art(ui, &ui->library->games[index]);
    ui->game_options_active = 1;
    ui->needs_redraw = 1;
}

static void ui_toggle_current_favorite(FsUi *ui) {
    ssize_t game_index = -1;
    int result;
    if (ui->page == PAGE_LIBRARY && ui->library_view == LIBRARY_GAMES) {
        game_index = ui_filtered_game_at(ui, (size_t)ui->selected[PAGE_LIBRARY]);
    } else if (ui->page == PAGE_SEARCH && ui->search_count > 0U) {
        game_index = (ssize_t)ui->search_indices[ui->selected[PAGE_SEARCH]];
    }
    if (game_index < 0) {
        return;
    }
    result = fs_favorites_toggle(ui->favorites, ui->library->games[game_index].path);
    if (result >= 0) {
        ui_apply_favorites(ui);
        ui_toast(ui, result == 1 ? "Added to favorites" : "Removed from favorites");
    } else {
        ui_toast(ui, "Could not update favorites");
    }
}

static void ui_activate_home(FsUi *ui) {
    int selection = ui->selected[PAGE_HOME];
    ssize_t game = ui_home_game_at(ui, selection);
    if (selection == 0) {
        if (game >= 0) {
            (void)ui_launch_index(ui, game);
        } else {
            ui_toast(ui, "No recent game is available");
        }
    } else if (selection == 1) {
        ui->page = PAGE_LIBRARY;
        ui->library_view = LIBRARY_GAMES;
        ui->library_system = LIBRARY_FILTER_FAVORITES;
        ui->selected[PAGE_LIBRARY] = 0;
    } else if (selection == 2) {
        ui->page = PAGE_ACTIVITY;
    } else {
        ui->page = PAGE_LIBRARY;
        ui->library_view = LIBRARY_SYSTEMS;
    }
    ui->needs_redraw = 1;
}

static void ui_activate_library(FsUi *ui) {
    if (ui->library_view == LIBRARY_SYSTEMS) {
        int selection = ui->selected[PAGE_LIBRARY];
        ui->library_system = selection == 0 ? LIBRARY_FILTER_ALL : selection - 1;
        ui->library_view = LIBRARY_GAMES;
        ui->selected[PAGE_LIBRARY] = 0;
        ui->needs_redraw = 1;
    } else {
        ssize_t game = ui_filtered_game_at(ui, (size_t)ui->selected[PAGE_LIBRARY]);
        (void)ui_launch_index(ui, game);
    }
}

static int ui_write_config_many(FsUi *ui, const char *const *keys,
                                const char *const *values, size_t count) {
    char path[FS_MAX_PATH];
    if (ui == NULL || fs_path_join(path, sizeof(path), ui->home, "config.ini") != 0 ||
        fs_config_set_many(path, keys, values, count) != 0) {
        if (ui != NULL) ui_toast(ui, "Could not save the setting");
        return -1;
    }
    ui->config_needs_lkg = 1;
    return 0;
}

static int ui_write_config(FsUi *ui, const char *key, const char *value) {
    const char *keys[] = {key};
    const char *values[] = {value};
    return ui_write_config_many(ui, keys, values, 1U);
}

static void ui_activate_settings(FsUi *ui) {
    int selection = ui->selected[PAGE_SETTINGS];
    switch (selection) {
        case 0: {
            const char *next = fs_casecmp(ui->config->launcher_mode, "forgeshell") == 0 ?
                               "gmenu2x" : "forgeshell";
            if (ui_write_config(ui, "launcher_mode", next) == 0) {
                (void)fs_copy(ui->config->launcher_mode,
                              sizeof(ui->config->launcher_mode), next);
                ui_toast(ui, "Default changes on next boot");
            }
            break;
        }
        case 1: {
            int next = !ui->config->scan_on_start;
            if (ui_write_config(ui, "scan_on_start", next ? "1" : "0") == 0) {
                ui->config->scan_on_start = next;
            }
            break;
        }
        case 2: {
            int previous = ui->config->large_text;
            int next = !previous;
            ui->config->large_text = next;
            if (ui_open_fonts(ui) != 0) {
                ui->config->large_text = previous;
                (void)ui_open_fonts(ui);
                ui_toast(ui, "Large text could not be loaded");
            } else if (ui_write_config(ui, "large_text", next ? "1" : "0") != 0) {
                ui->config->large_text = previous;
                (void)ui_open_fonts(ui);
            }
            break;
        }
        case 3: {
            int next = !ui->config->high_contrast;
            if (ui_write_config(ui, "high_contrast", next ? "1" : "0") == 0) {
                ui->config->high_contrast = next;
                ui_theme_accessibility(ui);
            }
            break;
        }
        case 4: {
            int next = !ui->config->metadata_enabled;
            if (ui_write_config(ui, "metadata_enabled", next ? "1" : "0") == 0) {
                ui->config->metadata_enabled = next;
                if (next && !ui->safe_mode) {
                    if (fs_metadata_load(ui->metadata) < 0) {
                        ui_toast(ui, "Metadata enabled, but the index could not be loaded");
                    } else {
                        fs_metadata_apply(ui->metadata, ui->library);
                        ui_refresh_search(ui);
                        ui_toast(ui, "Metadata enabled");
                    }
                } else {
                    ui_toast(ui, next ? "Metadata will load after Safe Mode" :
                             "Metadata disabled until restart");
                }
            }
            break;
        }
        case 5: {
            int next = !ui->config->safe_mode_next_boot;
            if (ui_write_config(ui, "safe_mode_next_boot", next ? "1" : "0") == 0) {
                ui->config->safe_mode_next_boot = next;
                ui_toast(ui, next ? "Safe mode will start next boot" : "Normal boot restored");
            }
            break;
        }
        case 6: {
            int next = !ui->config->show_recovery_hint;
            if (ui_write_config(ui, "show_recovery_hint", next ? "1" : "0") == 0) {
                ui->config->show_recovery_hint = next;
            }
            break;
        }
        case 7:
            if (ui->safe_mode) {
                ui_toast(ui, "Scanning is disabled in safe mode");
            } else if (fs_library_start_scan(ui->library) != 0) {
                ui_toast(ui, "Library rescan could not start");
            } else {
                ui_toast(ui, "Library rescan started");
            }
            break;
        case 8:
            ui->onboarding_step = 0;
            ui->onboarding_choice = fs_casecmp(ui->config->launcher_mode, "forgeshell") == 0;
            ui->onboarding_active = 1;
            break;
        default:
            break;
    }
    ui->needs_redraw = 1;
}

static void ui_activate(FsUi *ui) {
    switch (ui->page) {
        case PAGE_HOME: ui_activate_home(ui); break;
        case PAGE_LIBRARY: ui_activate_library(ui); break;
        case PAGE_SEARCH:
            if (strlen(ui->search_query) + 1U < sizeof(ui->search_query)) {
                size_t len = strlen(ui->search_query);
                ui->search_query[len] = search_chars[ui->search_char];
                ui->search_query[len + 1U] = '\0';
                ui_refresh_search(ui);
                ui->needs_redraw = 1;
            }
            break;
        case PAGE_ACTIVITY:
            if (ui->sessions->count > 0U) {
                ssize_t index = fs_library_find_path(ui->library,
                    ui->sessions->items[ui->selected[PAGE_ACTIVITY]].path);
                (void)ui_launch_index(ui, index);
            }
            break;
        case PAGE_TOOLS:
            if (ui->tools != NULL && (size_t)ui->selected[PAGE_TOOLS] < ui->tools->count) {
                (void)ui_run_tool(ui, &ui->tools->items[ui->selected[PAGE_TOOLS]]);
            }
            break;
        case PAGE_SETTINGS: ui_activate_settings(ui); break;
        case PAGE_POWER:
            if (ui->selected[PAGE_POWER] == 0) {
                ui->running = 0;
                ui->exit_action = FS_EXIT_GMENU;
            } else if (ui->platform->cap_safe_shutdown) {
                ui->modal_active = 1;
                ui->modal_action = ui->selected[PAGE_POWER] == 1 ? FS_EXIT_REBOOT : FS_EXIT_POWEROFF;
                ui->needs_redraw = 1;
            }
            break;
        default: break;
    }
}

static void ui_back(FsUi *ui) {
    if (ui->page == PAGE_LIBRARY && ui->library_view == LIBRARY_GAMES) {
        ui->library_view = LIBRARY_SYSTEMS;
        ui->selected[PAGE_LIBRARY] = ui->library_system >= 0 ? ui->library_system + 1 : 0;
    } else if (ui->page != PAGE_HOME) {
        ui->page = PAGE_HOME;
    }
    ui->needs_redraw = 1;
}

static void ui_handle_action(FsUi *ui, FsAction action) {
    if (action == FS_ACTION_NONE) return;
    if (ui->onboarding_active) {
        ui_handle_onboarding(ui, action);
        return;
    }
    if (ui->game_options_active) {
        ui_handle_game_options(ui, action);
        return;
    }
    if (ui->modal_active) {
        if (action == FS_ACTION_SELECT) {
            ui->exit_action = FS_EXIT_RECOVERY;
            ui->running = 0;
        } else if (action == FS_ACTION_POWER) {
            ui->exit_action = FS_EXIT_GMENU;
            ui->running = 0;
        } else if (action == FS_ACTION_ACCEPT || action == FS_ACTION_OPTIONS) {
            ui->exit_action = ui->modal_action;
            ui->running = 0;
        } else if (action == FS_ACTION_BACK) {
            ui->modal_active = 0;
            ui->needs_redraw = 1;
        }
        return;
    }
    if (action == FS_ACTION_SELECT) {
        ui->exit_action = FS_EXIT_RECOVERY;
        ui->running = 0;
        return;
    }
    if (action == FS_ACTION_PAGE_LEFT) {
        ui_change_page(ui, -1);
        return;
    }
    if (action == FS_ACTION_PAGE_RIGHT) {
        ui_change_page(ui, 1);
        return;
    }
    if (action == FS_ACTION_POWER) {
        ui->exit_action = FS_EXIT_GMENU;
        ui->running = 0;
        return;
    }
    if (action == FS_ACTION_UP) {
        ui_move_selection(ui, -1);
    } else if (action == FS_ACTION_DOWN) {
        ui_move_selection(ui, 1);
    } else if (action == FS_ACTION_LEFT && ui->page == PAGE_SEARCH) {
        ui->search_char--;
        if (ui->search_char < 0) ui->search_char = (int)strlen(search_chars) - 1;
        ui->needs_redraw = 1;
    } else if (action == FS_ACTION_RIGHT && ui->page == PAGE_SEARCH) {
        ui->search_char++;
        if ((size_t)ui->search_char >= strlen(search_chars)) ui->search_char = 0;
        ui->needs_redraw = 1;
    } else if (action == FS_ACTION_ACCEPT) {
        ui_activate(ui);
    } else if (action == FS_ACTION_BACK) {
        ui_back(ui);
    } else if (action == FS_ACTION_FAVORITE) {
        if (ui->page == PAGE_SEARCH) {
            size_t len = strlen(ui->search_query);
            if (len > 0U) {
                ui->search_query[len - 1U] = '\0';
                ui_refresh_search(ui);
                ui->needs_redraw = 1;
            }
        } else {
            ui_toggle_current_favorite(ui);
        }
    } else if (action == FS_ACTION_START && ui->page == PAGE_SEARCH) {
        ui->search_query[0] = '\0';
        ui_refresh_search(ui);
        ui->needs_redraw = 1;
    } else if (action == FS_ACTION_OPTIONS && ui->page == PAGE_LIBRARY &&
               ui->library_view == LIBRARY_GAMES) {
        ui_open_game_options(ui,
            ui_filtered_game_at(ui, (size_t)ui->selected[PAGE_LIBRARY]));
    } else if (action == FS_ACTION_OPTIONS && ui->page == PAGE_SEARCH && ui->search_count > 0U) {
        (void)ui_launch_index(ui, (ssize_t)ui->search_indices[ui->selected[PAGE_SEARCH]]);
    }
}

int fs_ui_run(const FsPlatform *platform, FsToolCatalog *tools,
              FsConfig *config, FsTheme *theme, FsLibrary *library,
              FsFavorites *favorites, FsSessions *sessions,
              FsOverrides *overrides, FsMetadata *metadata, int boot_mode, int safe_mode) {
    FsUi ui;
    SDL_Event event;
    Uint32 last_scan_redraw = 0U;
    const char *boot_ok;
    memset(&ui, 0, sizeof(ui));
    ui.home = platform->home;
    ui.platform = platform;
    ui.tools = tools;
    ui.config = config;
    ui.base_theme = *theme;
    ui.theme = *theme;
    ui.library = library;
    ui.favorites = favorites;
    ui.sessions = sessions;
    ui.overrides = overrides;
    ui.metadata = metadata;
    ui.safe_mode = safe_mode;
    ui.onboarding_active = !safe_mode && !config->onboarding_complete;
    ui.onboarding_choice = fs_casecmp(config->launcher_mode, "forgeshell") == 0;
    ui.game_options_index = -1;
    ui.page = PAGE_HOME;
    ui.library_view = LIBRARY_SYSTEMS;
    ui.library_system = LIBRARY_FILTER_ALL;
    ui.running = 1;
    ui.exit_action = FS_EXIT_GMENU;
    ui.needs_redraw = 1;
    if (ui_graphics_init(&ui) != 0) {
        return FS_EXIT_RECOVERY;
    }
    ui_refresh_search(&ui);
    if (safe_mode) {
        ui_toast(&ui, "Safe mode: scanning, metadata and overrides are disabled");
    } else if (library->scan_start_failed) {
        ui_toast(&ui, "Library scan could not allocate memory");
    } else if (favorites->load_failed) {
        ui_toast(&ui, "Favorites could not be loaded");
    } else if (sessions->load_failed) {
        ui_toast(&ui, "Activity history could not be loaded");
    } else if (overrides->load_failed) {
        ui_toast(&ui, "Game options could not be loaded");
    } else if (metadata->load_failed) {
        ui_toast(&ui, "Metadata index could not be loaded");
    } else if (boot_mode && config->show_recovery_hint) {
        char hint[128];
        (void)snprintf(hint, sizeof(hint), "%s opens the recovery launcher at any time",
                       ui_action_label(&ui, FS_ACTION_SELECT));
        ui_toast(&ui, hint);
    }
    ui.config_needs_lkg = !safe_mode;
    ui_draw(&ui);
    boot_ok = getenv("FORGESHELL_BOOT_OK");
    if (boot_mode && boot_ok != NULL && boot_ok[0] != '\0') {
        static const char ok[] = "ok\n";
        if (fs_write_atomic(boot_ok, ok, sizeof(ok) - 1U, 0644) != 0) {
            fprintf(stderr, "ForgeShell: could not complete the boot recovery handshake\n");
            ui_graphics_quit(&ui);
            return FS_EXIT_RECOVERY;
        }
    }
    while (ui.running) {
        int had_event = 0;
        while (SDL_PollEvent(&event)) {
            had_event = 1;
            if (event.type == SDL_QUIT) {
                ui.running = 0;
                ui.exit_action = FS_EXIT_GMENU;
            } else if (event.type == SDL_KEYDOWN) {
                ui_handle_action(&ui, fs_platform_translate_key(ui.platform, event.key.keysym.sym));
            } else if (event.type == SDL_USEREVENT) {
                ui.needs_redraw = 1;
            }
        }
        if (library->scan_active) {
            int was_active = library->scan_active;
            (void)fs_library_scan_step(library, (size_t)config->scan_budget);
            if (was_active && !library->scan_active) {
                ui_apply_favorites(&ui);
                if (!ui.safe_mode && config->metadata_enabled) {
                    fs_metadata_apply(metadata, library);
                    ui_refresh_search(&ui);
                }
                ui_write_scan_metrics(&ui);
                if (library->scan_truncated) {
                    ui_toast(&ui, "Scan complete; some items were skipped");
                } else if (library->cache_save_failed) {
                    ui_toast(&ui, "Scan complete; cache could not be saved");
                } else {
                    ui_toast(&ui, "Library scan complete");
                }
            } else if (SDL_GetTicks() - last_scan_redraw > 500U) {
                ui.needs_redraw = 1;
                last_scan_redraw = SDL_GetTicks();
            }
        }
        if (ui.toast[0] != '\0' &&
            (int32_t)(SDL_GetTicks() - ui.toast_until) >= 0) {
            ui.toast[0] = '\0';
            if (ui.toast_timer != NULL) {
                (void)SDL_RemoveTimer(ui.toast_timer);
                ui.toast_timer = NULL;
            }
            ui.needs_redraw = 1;
        }
        if (ui.needs_redraw) {
            ui_draw(&ui);
        }
        if (library->scan_active || (ui.toast[0] != '\0' && ui.toast_timer == NULL) || had_event) {
            SDL_Delay(33U);
        } else if (SDL_WaitEvent(&event)) {
            if (event.type == SDL_QUIT) {
                ui.running = 0;
            } else if (event.type == SDL_KEYDOWN) {
                ui_handle_action(&ui, fs_platform_translate_key(ui.platform, event.key.keysym.sym));
            } else if (event.type == SDL_USEREVENT) {
                ui.needs_redraw = 1;
            }
        } else {
            SDL_Delay(33U);
        }
    }
    ui_graphics_quit(&ui);
    return ui.exit_action;
}
