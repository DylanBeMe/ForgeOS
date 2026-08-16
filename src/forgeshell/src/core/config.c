#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fs_line_complete(FILE *file, const char *line) {
    int ch;
    if (strchr(line, '\n') != NULL || feof(file)) {
        return 1;
    }
    do {
        ch = fgetc(file);
    } while (ch != '\n' && ch != EOF);
    (void)ch;
    errno = EOVERFLOW;
    return -1;
}

static int fs_config_parse_bool(const char *value, int *out) {
    if (value == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (fs_casecmp(value, "1") == 0 || fs_casecmp(value, "true") == 0 ||
        fs_casecmp(value, "yes") == 0 || fs_casecmp(value, "on") == 0) {
        *out = 1;
        return 0;
    }
    if (fs_casecmp(value, "0") == 0 || fs_casecmp(value, "false") == 0 ||
        fs_casecmp(value, "no") == 0 || fs_casecmp(value, "off") == 0) {
        *out = 0;
        return 0;
    }
    errno = EINVAL;
    return -1;
}

static int fs_config_parse_int(const char *value, int minimum, int maximum, int *out) {
    char *end = NULL;
    long parsed;
    if (value == NULL || out == NULL || value[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value) {
        errno = EINVAL;
        return -1;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) end++;
    if (*end != '\0' || parsed < minimum || parsed > maximum) {
        errno = EINVAL;
        return -1;
    }
    *out = (int)parsed;
    return 0;
}


static unsigned fs_config_key_bit(const char *key) {
    static const char *const keys[] = {
        "launcher_mode", "scan_on_start", "large_text", "high_contrast",
        "scan_budget", "onboarding_complete", "safe_mode_next_boot",
        "metadata_enabled", "show_recovery_hint"
    };
    size_t i;
    if (key == NULL) return 0U;
    for (i = 0U; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (fs_casecmp(key, keys[i]) == 0) return 1U << i;
    }
    return 0U;
}

static int fs_config_apply(FsConfig *config, const char *key, const char *value) {
    if (config == NULL || key == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (fs_casecmp(key, "launcher_mode") == 0) {
        if (fs_casecmp(value, "forgeshell") != 0 && fs_casecmp(value, "gmenu2x") != 0) {
            errno = EINVAL;
            return -1;
        }
        return fs_copy(config->launcher_mode, sizeof(config->launcher_mode), value) == 0 ? 1 : -1;
    }
    if (fs_casecmp(key, "scan_on_start") == 0)
        return fs_config_parse_bool(value, &config->scan_on_start) == 0 ? 1 : -1;
    if (fs_casecmp(key, "large_text") == 0)
        return fs_config_parse_bool(value, &config->large_text) == 0 ? 1 : -1;
    if (fs_casecmp(key, "high_contrast") == 0)
        return fs_config_parse_bool(value, &config->high_contrast) == 0 ? 1 : -1;
    if (fs_casecmp(key, "scan_budget") == 0)
        return fs_config_parse_int(value, 2, 64, &config->scan_budget) == 0 ? 1 : -1;
    if (fs_casecmp(key, "onboarding_complete") == 0)
        return fs_config_parse_bool(value, &config->onboarding_complete) == 0 ? 1 : -1;
    if (fs_casecmp(key, "safe_mode_next_boot") == 0)
        return fs_config_parse_bool(value, &config->safe_mode_next_boot) == 0 ? 1 : -1;
    if (fs_casecmp(key, "metadata_enabled") == 0)
        return fs_config_parse_bool(value, &config->metadata_enabled) == 0 ? 1 : -1;
    if (fs_casecmp(key, "show_recovery_hint") == 0)
        return fs_config_parse_bool(value, &config->show_recovery_hint) == 0 ? 1 : -1;
    return 0;
}

void fs_config_defaults(FsConfig *config) {
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    (void)fs_copy(config->launcher_mode, sizeof(config->launcher_mode), "gmenu2x");
    config->scan_on_start = 0;
    config->large_text = 0;
    config->high_contrast = 0;
    config->scan_budget = FS_SCAN_BUDGET_DEFAULT;
    config->onboarding_complete = 0;
    config->safe_mode_next_boot = 0;
    config->metadata_enabled = 1;
    config->show_recovery_hint = 1;
}

int fs_config_load(const char *path, FsConfig *config) {
    FILE *file;
    char line[1024];
    unsigned seen = 0U;
    int recognized = 0;
    if (path == NULL || config == NULL) {
        errno = EINVAL;
        return -1;
    }
    fs_config_defaults(config);
    file = fopen(path, "r");
    if (file == NULL) {
        return errno == ENOENT ? 0 : -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmed;
        char *equals;
        char *key;
        char *value;
        unsigned bit;
        int applied;
        int line_status = fs_line_complete(file, line);
        if (line_status < 0) {
            (void)fclose(file);
            errno = EOVERFLOW;
            return -1;
        }
        if (line_status == 0) continue;
        trimmed = fs_trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') continue;
        equals = strchr(trimmed, '=');
        if (equals == NULL) {
            (void)fclose(file);
            errno = EINVAL;
            return -1;
        }
        *equals = '\0';
        key = fs_trim(trimmed);
        value = fs_trim(equals + 1);
        if (key[0] == '\0') {
            (void)fclose(file);
            errno = EINVAL;
            return -1;
        }
        bit = fs_config_key_bit(key);
        if (bit == 0U) {
            (void)fclose(file);
            errno = EINVAL;
            return -1;
        }
        if ((seen & bit) != 0U) {
            (void)fclose(file);
            errno = EEXIST;
            return -1;
        }
        applied = fs_config_apply(config, key, value);
        if (applied <= 0) {
            (void)fclose(file);
            if (applied == 0) errno = EINVAL;
            return -1;
        }
        seen |= bit;
        recognized++;
    }
    if (ferror(file)) {
        (void)fclose(file);
        return -1;
    }
    if (fclose(file) != 0) return -1;
    if (recognized == 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int fs_config_update_valid(const char *key, const char *value) {
    return key != NULL && value != NULL && key[0] != '\0' &&
           strchr(key, '=') == NULL && strchr(key, '\n') == NULL &&
           strchr(key, '\r') == NULL && strchr(value, '\n') == NULL &&
           strchr(value, '\r') == NULL;
}

int fs_config_set_many(const char *path, const char *const *keys,
                       const char *const *values, size_t count) {
    char *source = NULL;
    size_t source_len = 0U;
    size_t capacity;
    size_t extra = 64U;
    char *output = NULL;
    unsigned char *replaced = NULL;
    size_t used = 0U;
    char *cursor;
    size_t i;

    if (path == NULL || keys == NULL || values == NULL || count == 0U) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0U; i < count; i++) {
        FsConfig candidate;
        size_t j;
        if (!fs_config_update_valid(keys[i], values[i]) || fs_config_key_bit(keys[i]) == 0U) {
            errno = EINVAL;
            return -1;
        }
        fs_config_defaults(&candidate);
        if (fs_config_apply(&candidate, keys[i], values[i]) <= 0) {
            errno = EINVAL;
            return -1;
        }
        for (j = 0U; j < i; j++) {
            if (fs_casecmp(keys[i], keys[j]) == 0) {
                errno = EEXIST;
                return -1;
            }
        }
        if (strlen(keys[i]) > SIZE_MAX - strlen(values[i]) - extra - 2U) {
            errno = EOVERFLOW;
            return -1;
        }
        extra += strlen(keys[i]) + strlen(values[i]) + 2U;
    }
    if (fs_read_text(path, &source, 1024U * 1024U) == 0) {
        source_len = strlen(source);
    } else if (errno != ENOENT) {
        return -1;
    }
    if (source_len > SIZE_MAX - extra) {
        free(source);
        errno = EOVERFLOW;
        return -1;
    }
    capacity = source_len + extra;
    output = (char *)calloc(capacity, 1U);
    replaced = (unsigned char *)calloc(count, sizeof(*replaced));
    if (output == NULL || replaced == NULL) {
        free(source);
        free(output);
        free(replaced);
        return -1;
    }
    cursor = source;
    {
        unsigned seen_source = 0U;
        while (cursor != NULL && *cursor != '\0') {
            char *newline = strchr(cursor, '\n');
            size_t line_len = newline == NULL ? strlen(cursor) : (size_t)(newline - cursor);
            ssize_t match = -1;
            char line[1024];
            char *trimmed;
            char *equals;
            char *existing_key = NULL;
            char *existing_value = NULL;
            unsigned existing_bit = 0U;
            if (line_len >= sizeof(line)) {
                errno = EOVERFLOW;
                goto invalid_source;
            }
            memcpy(line, cursor, line_len);
            line[line_len] = '\0';
            trimmed = fs_trim(line);
            if (trimmed[0] != '\0' && trimmed[0] != '#' && trimmed[0] != ';') {
                FsConfig candidate;
                equals = strchr(trimmed, '=');
                if (equals == NULL) {
                    errno = EINVAL;
                    goto invalid_source;
                }
                *equals = '\0';
                existing_key = fs_trim(trimmed);
                existing_value = fs_trim(equals + 1);
                existing_bit = fs_config_key_bit(existing_key);
                if (existing_key[0] == '\0' || existing_bit == 0U) {
                    errno = EINVAL;
                    goto invalid_source;
                }
                for (i = 0U; i < count; i++) {
                    if (fs_casecmp(existing_key, keys[i]) == 0) {
                        match = (ssize_t)i;
                        break;
                    }
                }
                if ((seen_source & existing_bit) != 0U && match < 0) {
                    errno = EEXIST;
                    goto invalid_source;
                }
                seen_source |= existing_bit;
                if (match < 0) {
                    fs_config_defaults(&candidate);
                    if (fs_config_apply(&candidate, existing_key, existing_value) <= 0) {
                        errno = EINVAL;
                        goto invalid_source;
                    }
                }
            }
            if (match >= 0) {
            size_t index = (size_t)match;
            if (!replaced[index]) {
                int written = snprintf(output + used, capacity - used, "%s=%s\n",
                                       keys[index], values[index]);
                if (written < 0 || (size_t)written >= capacity - used) goto no_space;
                used += (size_t)written;
                replaced[index] = 1U;
            }
        } else {
            if (used + line_len + 1U >= capacity) goto no_space;
            memcpy(output + used, cursor, line_len);
            used += line_len;
            output[used++] = '\n';
        }
            cursor = newline == NULL ? NULL : newline + 1;
        }
    }
    for (i = 0U; i < count; i++) {
        if (!replaced[i]) {
            int written = snprintf(output + used, capacity - used, "%s=%s\n",
                                   keys[i], values[i]);
            if (written < 0 || (size_t)written >= capacity - used) goto no_space;
            used += (size_t)written;
        }
    }
    free(source);
    free(replaced);
    if (fs_write_atomic(path, output, used, 0644) != 0) {
        free(output);
        return -1;
    }
    free(output);
    return 0;

invalid_source:
    free(source);
    free(output);
    free(replaced);
    return -1;

no_space:
    free(source);
    free(output);
    free(replaced);
    errno = ENOSPC;
    return -1;
}

int fs_config_set(const char *path, const char *key, const char *value) {
    const char *keys[] = {key};
    const char *values[] = {value};
    return fs_config_set_many(path, keys, values, 1U);
}

static int fs_config_companion_path(const char *path, char *out, size_t out_size) {
    int written;
    if (path == NULL || out == NULL || out_size == 0U) {
        errno = EINVAL;
        return -1;
    }
    written = snprintf(out, out_size, "%s.last-good", path);
    if (written < 0 || (size_t)written >= out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int fs_config_save_last_good(const char *path) {
    char companion[FS_MAX_PATH];
    char *data = NULL;
    int result;
    if (fs_config_companion_path(path, companion, sizeof(companion)) != 0) return -1;
    if (fs_read_text(path, &data, 1024U * 1024U) != 0) return -1;
    result = fs_write_atomic(companion, data, strlen(data), 0644);
    free(data);
    return result;
}

int fs_config_restore_last_good(const char *path) {
    char companion[FS_MAX_PATH];
    char *data = NULL;
    int result;
    if (fs_config_companion_path(path, companion, sizeof(companion)) != 0) return -1;
    if (fs_read_text(companion, &data, 1024U * 1024U) != 0) return -1;
    result = fs_write_atomic(path, data, strlen(data), 0644);
    free(data);
    return result;
}

void fs_theme_defaults(FsTheme *theme) {
    if (theme == NULL) {
        return;
    }
    memset(theme, 0, sizeof(*theme));
    theme->background = 0x071521U;
    theme->panel = 0x10283AU;
    theme->panel_alt = 0x17384DU;
    theme->accent = 0x71E6C1U;
    theme->accent_soft = 0x2E7F71U;
    theme->text = 0xF4FBF9U;
    theme->muted = 0x9BB7B5U;
    theme->danger = 0xFF7A85U;
    theme->border = 0x28536AU;
    theme->radius = 6;
    theme->font_small = 10;
    theme->font_body = 13;
    theme->font_title = 18;
    (void)fs_copy(theme->font_regular, sizeof(theme->font_regular),
                  "/usr/share/fonts/dejavu/DejaVuSans.ttf");
    (void)fs_copy(theme->font_bold, sizeof(theme->font_bold),
                  "/usr/share/fonts/dejavu/DejaVuSans-Bold.ttf");
}

static int fs_theme_parse_color(const char *value, uint32_t *out) {
    size_t i;
    if (value == NULL || out == NULL || value[0] != '#' || strlen(value) != 7U) {
        errno = EINVAL;
        return -1;
    }
    for (i = 1U; i < 7U; i++) {
        if (!((value[i] >= '0' && value[i] <= '9') ||
              (value[i] >= 'a' && value[i] <= 'f') ||
              (value[i] >= 'A' && value[i] <= 'F'))) {
            errno = EINVAL;
            return -1;
        }
    }
    *out = fs_parse_color(value, 0U);
    return 0;
}

static int fs_theme_parse_int(const char *value, int minimum, int maximum, int *out) {
    char *end = NULL;
    long parsed;
    if (value == NULL || out == NULL || value[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value) {
        errno = EINVAL;
        return -1;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) end++;
    if (*end != '\0' || parsed < minimum || parsed > maximum) {
        errno = EINVAL;
        return -1;
    }
    *out = (int)parsed;
    return 0;
}

static int fs_theme_apply(FsTheme *theme, const char *key, const char *value,
                          unsigned long *seen) {
    enum {
        THEME_BACKGROUND, THEME_PANEL, THEME_PANEL_ALT, THEME_ACCENT,
        THEME_ACCENT_SOFT, THEME_TEXT, THEME_MUTED, THEME_DANGER,
        THEME_BORDER, THEME_RADIUS, THEME_FONT_SMALL, THEME_FONT_BODY,
        THEME_FONT_TITLE, THEME_FONT_REGULAR, THEME_FONT_BOLD, THEME_KEY_COUNT
    } index;
    int result = 0;
    if (theme == NULL || key == NULL || value == NULL || seen == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (fs_casecmp(key, "background") == 0) index = THEME_BACKGROUND;
    else if (fs_casecmp(key, "panel") == 0) index = THEME_PANEL;
    else if (fs_casecmp(key, "panel_alt") == 0) index = THEME_PANEL_ALT;
    else if (fs_casecmp(key, "accent") == 0) index = THEME_ACCENT;
    else if (fs_casecmp(key, "accent_soft") == 0) index = THEME_ACCENT_SOFT;
    else if (fs_casecmp(key, "text") == 0) index = THEME_TEXT;
    else if (fs_casecmp(key, "muted") == 0) index = THEME_MUTED;
    else if (fs_casecmp(key, "danger") == 0) index = THEME_DANGER;
    else if (fs_casecmp(key, "border") == 0) index = THEME_BORDER;
    else if (fs_casecmp(key, "radius") == 0) index = THEME_RADIUS;
    else if (fs_casecmp(key, "font_small") == 0) index = THEME_FONT_SMALL;
    else if (fs_casecmp(key, "font_body") == 0) index = THEME_FONT_BODY;
    else if (fs_casecmp(key, "font_title") == 0) index = THEME_FONT_TITLE;
    else if (fs_casecmp(key, "font_regular") == 0) index = THEME_FONT_REGULAR;
    else if (fs_casecmp(key, "font_bold") == 0) index = THEME_FONT_BOLD;
    else {
        errno = EINVAL;
        return -1;
    }
    if ((*seen & (1UL << (unsigned)index)) != 0UL) {
        errno = EEXIST;
        return -1;
    }
    *seen |= 1UL << (unsigned)index;
    switch (index) {
        case THEME_BACKGROUND: result = fs_theme_parse_color(value, &theme->background); break;
        case THEME_PANEL: result = fs_theme_parse_color(value, &theme->panel); break;
        case THEME_PANEL_ALT: result = fs_theme_parse_color(value, &theme->panel_alt); break;
        case THEME_ACCENT: result = fs_theme_parse_color(value, &theme->accent); break;
        case THEME_ACCENT_SOFT: result = fs_theme_parse_color(value, &theme->accent_soft); break;
        case THEME_TEXT: result = fs_theme_parse_color(value, &theme->text); break;
        case THEME_MUTED: result = fs_theme_parse_color(value, &theme->muted); break;
        case THEME_DANGER: result = fs_theme_parse_color(value, &theme->danger); break;
        case THEME_BORDER: result = fs_theme_parse_color(value, &theme->border); break;
        case THEME_RADIUS: result = fs_theme_parse_int(value, 0, 12, &theme->radius); break;
        case THEME_FONT_SMALL: result = fs_theme_parse_int(value, 8, 16, &theme->font_small); break;
        case THEME_FONT_BODY: result = fs_theme_parse_int(value, 10, 20, &theme->font_body); break;
        case THEME_FONT_TITLE: result = fs_theme_parse_int(value, 14, 26, &theme->font_title); break;
        case THEME_FONT_REGULAR:
            if (value[0] != '/') result = -1;
            else result = fs_copy(theme->font_regular, sizeof(theme->font_regular), value);
            break;
        case THEME_FONT_BOLD:
            if (value[0] != '/') result = -1;
            else result = fs_copy(theme->font_bold, sizeof(theme->font_bold), value);
            break;
        default: result = -1; break;
    }
    if (result != 0 && errno == 0) errno = EINVAL;
    return result;
}

int fs_theme_load(const char *path, FsTheme *theme) {
    FILE *file;
    char line[1024];
    FsTheme candidate;
    unsigned long seen = 0UL;
    int saved_errno;
    if (path == NULL || theme == NULL) {
        errno = EINVAL;
        return -1;
    }
    fs_theme_defaults(theme);
    candidate = *theme;
    file = fopen(path, "r");
    if (file == NULL) return errno == ENOENT ? 0 : -1;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmed;
        char *equals;
        int line_status = fs_line_complete(file, line);
        if (line_status < 0) {
            errno = EOVERFLOW;
            goto fail;
        }
        if (line_status == 0) continue;
        trimmed = fs_trim(line);
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') continue;
        if (trimmed[0] == '[') {
            if (fs_casecmp(trimmed, "[theme]") != 0) {
                errno = EINVAL;
                goto fail;
            }
            continue;
        }
        equals = strchr(trimmed, '=');
        if (equals == NULL) {
            errno = EINVAL;
            goto fail;
        }
        *equals = '\0';
        if (fs_theme_apply(&candidate, fs_trim(trimmed), fs_trim(equals + 1), &seen) != 0) {
            goto fail;
        }
    }
    if (ferror(file)) goto fail;
    if (fclose(file) != 0) return -1;
    *theme = candidate;
    return 0;

fail:
    saved_errno = errno == 0 ? EINVAL : errno;
    (void)fclose(file);
    fs_theme_defaults(theme);
    errno = saved_errno;
    return -1;
}

