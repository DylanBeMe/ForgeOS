#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <SDL/SDL.h>
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static int platform_tail_empty(const char *text) {
    if (text == NULL) return 0;
    while (*text != '\0' && isspace((unsigned char)*text)) text++;
    return *text == '\0';
}

static int platform_key_from_name(const char *value, int *key_out) {
    struct KeyName { const char *name; int key; };
    static const struct KeyName keys[] = {
        {"UP", SDLK_UP}, {"DOWN", SDLK_DOWN}, {"LEFT", SDLK_LEFT},
        {"RIGHT", SDLK_RIGHT}, {"RETURN", SDLK_RETURN}, {"ENTER", SDLK_RETURN},
        {"ESCAPE", SDLK_ESCAPE}, {"ESC", SDLK_ESCAPE}, {"SPACE", SDLK_SPACE},
        {"TAB", SDLK_TAB}, {"BACKSPACE", SDLK_BACKSPACE},
        {"LSHIFT", SDLK_LSHIFT}, {"LCTRL", SDLK_LCTRL},
        {"LALT", SDLK_LALT}, {"RCTRL", SDLK_RCTRL}
    };
    size_t i;
    char *end = NULL;
    long parsed;
    if (value == NULL || key_out == NULL || value[0] == '\0') return -1;
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno == 0 && end != value && platform_tail_empty(end) && parsed >= 0 && parsed <= 65535) {
        *key_out = (int)parsed;
        return 0;
    }
    for (i = 0U; i < sizeof(keys) / sizeof(keys[0]); i++) {
        if (fs_casecmp(value, keys[i].name) == 0) {
            *key_out = keys[i].key;
            return 0;
        }
    }
    errno = EINVAL;
    return -1;
}

static int platform_parse_bool(const char *value, int *out) {
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

static int platform_parse_int(const char *value, int minimum, int maximum, int *out) {
    char *end = NULL;
    long parsed;
    if (value == NULL || out == NULL || value[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value || !platform_tail_empty(end) ||
        parsed < minimum || parsed > maximum) {
        errno = EINVAL;
        return -1;
    }
    *out = (int)parsed;
    return 0;
}

static int platform_expand_once(char *value, size_t value_size,
                                const FsPlatform *platform) {
    struct Variable { const char *token; const char *value; };
    const struct Variable vars[] = {
        {"${data_root}", platform->data_root},
        {"${rom_root}", platform->rom_root},
        {"${home}", platform->home},
        {"${tool_root}", platform->tool_root},
        {"${device_id}", platform->device_id}
    };
    char buffer[FS_MAX_VALUE];
    size_t i;
    for (i = 0U; i < sizeof(vars) / sizeof(vars[0]); i++) {
        if (strstr(value, vars[i].token) != NULL) {
            if (fs_replace_all(value, vars[i].token, vars[i].value,
                               buffer, sizeof(buffer)) != 0 ||
                fs_copy(value, value_size, buffer) != 0) return -1;
        }
    }
    return 0;
}

static int platform_expand(FsPlatform *platform) {
    char *values[] = {
        platform->data_root, platform->rom_root, platform->home,
        platform->tool_root, platform->provider_source,
        platform->fallback_command, platform->reboot_command,
        platform->poweroff_command, platform->cpu_helper,
        platform->tools_manifest
    };
    const size_t sizes[] = {
        sizeof(platform->data_root), sizeof(platform->rom_root), sizeof(platform->home),
        sizeof(platform->tool_root), sizeof(platform->provider_source),
        sizeof(platform->fallback_command), sizeof(platform->reboot_command),
        sizeof(platform->poweroff_command), sizeof(platform->cpu_helper),
        sizeof(platform->tools_manifest)
    };
    size_t pass;
    size_t i;
    for (pass = 0U; pass < 4U; pass++) {
        for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++) {
            if (platform_expand_once(values[i], sizes[i], platform) != 0) return -1;
        }
    }
    for (i = 0U; i < sizeof(values) / sizeof(values[0]); i++) {
        if (strstr(values[i], "${") != NULL) {
            errno = EINVAL;
            return -1;
        }
    }
    return 0;
}

void fs_platform_defaults(FsPlatform *platform) {
    if (platform == NULL) return;
    memset(platform, 0, sizeof(*platform));
    (void)fs_copy(platform->device_id, sizeof(platform->device_id), "generic-linux");
    (void)fs_copy(platform->device_name, sizeof(platform->device_name), "Generic Linux handheld");
    (void)fs_copy(platform->family, sizeof(platform->family), "linux-sdl");
    platform->screen_width = FS_LOGICAL_W;
    platform->screen_height = FS_LOGICAL_H;
    platform->screen_bpp = 16;
    platform->fullscreen = 0;
    platform->fake_battery = -1;
    (void)fs_copy(platform->data_root, sizeof(platform->data_root), ".");
    (void)fs_copy(platform->rom_root, sizeof(platform->rom_root), "${data_root}/roms");
    (void)fs_copy(platform->home, sizeof(platform->home), "${data_root}/forgeshell");
    (void)fs_copy(platform->tool_root, sizeof(platform->tool_root), "${data_root}/apps/forge-tools");
    (void)fs_copy(platform->launcher_provider, sizeof(platform->launcher_provider), "forge-manifest");
    (void)fs_copy(platform->provider_source, sizeof(platform->provider_source), "${home}/systems.ini");
    (void)fs_copy(platform->fallback_command, sizeof(platform->fallback_command), "/bin/true");
    (void)fs_copy(platform->reboot_command, sizeof(platform->reboot_command), "/sbin/reboot");
    (void)fs_copy(platform->poweroff_command, sizeof(platform->poweroff_command), "/sbin/poweroff");
    (void)fs_copy(platform->cpu_helper, sizeof(platform->cpu_helper), "${tool_root}/cpu-profile-control.sh");
    (void)fs_copy(platform->frontend_value, sizeof(platform->frontend_value), "forgeshell");
    (void)fs_copy(platform->tools_manifest, sizeof(platform->tools_manifest), "${home}/tools.tsv");
    platform->key_up = SDLK_UP;
    platform->key_down = SDLK_DOWN;
    platform->key_left = SDLK_LEFT;
    platform->key_right = SDLK_RIGHT;
    platform->key_accept = SDLK_RETURN;
    platform->key_back = SDLK_ESCAPE;
    platform->key_favorite = SDLK_SPACE;
    platform->key_options = SDLK_RCTRL;
    platform->key_page_left = SDLK_TAB;
    platform->key_page_right = SDLK_BACKSPACE;
    platform->key_start = SDLK_LALT;
    platform->key_select = SDLK_LCTRL;
    platform->key_power = SDLK_LSHIFT;
    (void)fs_copy(platform->label_accept, sizeof(platform->label_accept), "ENTER");
    (void)fs_copy(platform->label_back, sizeof(platform->label_back), "ESC");
    (void)fs_copy(platform->label_favorite, sizeof(platform->label_favorite), "SPACE");
    (void)fs_copy(platform->label_options, sizeof(platform->label_options), "RCTRL");
    (void)fs_copy(platform->label_page_left, sizeof(platform->label_page_left), "TAB");
    (void)fs_copy(platform->label_page_right, sizeof(platform->label_page_right), "BKSP");
    (void)fs_copy(platform->label_start, sizeof(platform->label_start), "ALT");
    (void)fs_copy(platform->label_select, sizeof(platform->label_select), "CTRL");
    (void)fs_copy(platform->label_power, sizeof(platform->label_power), "SHIFT");
    platform->cap_safe_shutdown = 1;
    platform->cap_storage_health = 1;
    platform->cap_system_info = 1;
}

static int platform_set_string(char *dst, size_t dst_size, const char *value) {
    if (fs_copy(dst, dst_size, value) != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

int fs_platform_apply_override(FsPlatform *p, const char *key, const char *value) {
    if (p == NULL || key == NULL || value == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (fs_casecmp(key, "device.id") == 0) return platform_set_string(p->device_id, sizeof(p->device_id), value);
    if (fs_casecmp(key, "device.name") == 0) return platform_set_string(p->device_name, sizeof(p->device_name), value);
    if (fs_casecmp(key, "device.family") == 0) return platform_set_string(p->family, sizeof(p->family), value);
    if (fs_casecmp(key, "ui.screen_width") == 0) return platform_parse_int(value, 160, 3840, &p->screen_width);
    else if (fs_casecmp(key, "ui.screen_height") == 0) return platform_parse_int(value, 120, 2160, &p->screen_height);
    else if (fs_casecmp(key, "ui.screen_bpp") == 0) {
        int parsed;
        if (platform_parse_int(value, 16, 32, &parsed) != 0 ||
            (parsed != 16 && parsed != 24 && parsed != 32)) {
            errno = EINVAL;
            return -1;
        }
        p->screen_bpp = parsed;
        return 0;
    }
    else if (fs_casecmp(key, "ui.fullscreen") == 0) return platform_parse_bool(value, &p->fullscreen);
    else if (fs_casecmp(key, "storage.data_root") == 0) return platform_set_string(p->data_root, sizeof(p->data_root), value);
    else if (fs_casecmp(key, "storage.rom_root") == 0) return platform_set_string(p->rom_root, sizeof(p->rom_root), value);
    else if (fs_casecmp(key, "storage.home") == 0) return platform_set_string(p->home, sizeof(p->home), value);
    else if (fs_casecmp(key, "storage.tool_root") == 0) return platform_set_string(p->tool_root, sizeof(p->tool_root), value);
    else if (fs_casecmp(key, "launcher.provider") == 0) return platform_set_string(p->launcher_provider, sizeof(p->launcher_provider), value);
    else if (fs_casecmp(key, "launcher.source") == 0) return platform_set_string(p->provider_source, sizeof(p->provider_source), value);
    else if (fs_casecmp(key, "launcher.fallback_command") == 0) return platform_set_string(p->fallback_command, sizeof(p->fallback_command), value);
    else if (fs_casecmp(key, "launcher.frontend_value") == 0) return platform_set_string(p->frontend_value, sizeof(p->frontend_value), value);
    else if (fs_casecmp(key, "power.reboot") == 0) return platform_set_string(p->reboot_command, sizeof(p->reboot_command), value);
    else if (fs_casecmp(key, "power.poweroff") == 0) return platform_set_string(p->poweroff_command, sizeof(p->poweroff_command), value);
    else if (fs_casecmp(key, "performance.cpu_helper") == 0) return platform_set_string(p->cpu_helper, sizeof(p->cpu_helper), value);
    else if (fs_casecmp(key, "maintenance.manifest") == 0) return platform_set_string(p->tools_manifest, sizeof(p->tools_manifest), value);
    else if (fs_casecmp(key, "input.up") == 0) return platform_key_from_name(value, &p->key_up);
    else if (fs_casecmp(key, "input.down") == 0) return platform_key_from_name(value, &p->key_down);
    else if (fs_casecmp(key, "input.left") == 0) return platform_key_from_name(value, &p->key_left);
    else if (fs_casecmp(key, "input.right") == 0) return platform_key_from_name(value, &p->key_right);
    else if (fs_casecmp(key, "input.accept") == 0) return platform_key_from_name(value, &p->key_accept);
    else if (fs_casecmp(key, "input.back") == 0) return platform_key_from_name(value, &p->key_back);
    else if (fs_casecmp(key, "input.favorite") == 0) return platform_key_from_name(value, &p->key_favorite);
    else if (fs_casecmp(key, "input.options") == 0) return platform_key_from_name(value, &p->key_options);
    else if (fs_casecmp(key, "input.page_left") == 0) return platform_key_from_name(value, &p->key_page_left);
    else if (fs_casecmp(key, "input.page_right") == 0) return platform_key_from_name(value, &p->key_page_right);
    else if (fs_casecmp(key, "input.start") == 0) return platform_key_from_name(value, &p->key_start);
    else if (fs_casecmp(key, "input.select") == 0) return platform_key_from_name(value, &p->key_select);
    else if (fs_casecmp(key, "input.power") == 0) return platform_key_from_name(value, &p->key_power);
    else if (fs_casecmp(key, "labels.accept") == 0) return platform_set_string(p->label_accept, sizeof(p->label_accept), value);
    else if (fs_casecmp(key, "labels.back") == 0) return platform_set_string(p->label_back, sizeof(p->label_back), value);
    else if (fs_casecmp(key, "labels.favorite") == 0) return platform_set_string(p->label_favorite, sizeof(p->label_favorite), value);
    else if (fs_casecmp(key, "labels.options") == 0) return platform_set_string(p->label_options, sizeof(p->label_options), value);
    else if (fs_casecmp(key, "labels.page_left") == 0) return platform_set_string(p->label_page_left, sizeof(p->label_page_left), value);
    else if (fs_casecmp(key, "labels.page_right") == 0) return platform_set_string(p->label_page_right, sizeof(p->label_page_right), value);
    else if (fs_casecmp(key, "labels.start") == 0) return platform_set_string(p->label_start, sizeof(p->label_start), value);
    else if (fs_casecmp(key, "labels.select") == 0) return platform_set_string(p->label_select, sizeof(p->label_select), value);
    else if (fs_casecmp(key, "labels.power") == 0) return platform_set_string(p->label_power, sizeof(p->label_power), value);
    else if (fs_casecmp(key, "capabilities.battery") == 0) return platform_parse_bool(value, &p->cap_battery);
    else if (fs_casecmp(key, "capabilities.cpu_profiles") == 0) return platform_parse_bool(value, &p->cap_cpu_profiles);
    else if (fs_casecmp(key, "capabilities.brightness") == 0) return platform_parse_bool(value, &p->cap_brightness);
    else if (fs_casecmp(key, "capabilities.volume") == 0) return platform_parse_bool(value, &p->cap_volume);
    else if (fs_casecmp(key, "capabilities.safe_shutdown") == 0) return platform_parse_bool(value, &p->cap_safe_shutdown);
    else if (fs_casecmp(key, "capabilities.storage_health") == 0) return platform_parse_bool(value, &p->cap_storage_health);
    else if (fs_casecmp(key, "capabilities.system_info") == 0) return platform_parse_bool(value, &p->cap_system_info);
    else { errno = EINVAL; return -1; }
    return 0;
}

static int platform_section_known(const char *section) {
    static const char *const sections[] = {
        "device", "ui", "storage", "launcher", "maintenance", "performance",
        "power", "input", "labels", "capabilities"
    };
    size_t i;
    for (i = 0U; i < sizeof(sections) / sizeof(sections[0]); i++) {
        if (fs_casecmp(section, sections[i]) == 0) return 1;
    }
    return 0;
}

static int platform_line_complete(FILE *file, const char *line) {
    int ch;
    if (strchr(line, '\n') != NULL || feof(file)) return 1;
    do { ch = fgetc(file); } while (ch != '\n' && ch != EOF);
    errno = EOVERFLOW;
    return -1;
}

static int platform_required_keys_present(char keys[][112], size_t count) {
    static const char *const required[] = {
        "device.id", "device.name", "device.family",
        "ui.screen_width", "ui.screen_height", "ui.screen_bpp", "ui.fullscreen",
        "storage.data_root", "storage.rom_root", "storage.home", "storage.tool_root",
        "launcher.provider", "launcher.source", "launcher.fallback_command", "launcher.frontend_value",
        "maintenance.manifest", "performance.cpu_helper",
        "power.reboot", "power.poweroff",
        "input.up", "input.down", "input.left", "input.right", "input.accept",
        "input.back", "input.favorite", "input.options", "input.page_left",
        "input.page_right", "input.start", "input.select", "input.power",
        "labels.accept", "labels.back", "labels.favorite", "labels.options",
        "labels.page_left", "labels.page_right", "labels.start", "labels.select",
        "labels.power", "capabilities.battery", "capabilities.cpu_profiles",
        "capabilities.brightness", "capabilities.volume", "capabilities.safe_shutdown",
        "capabilities.storage_health", "capabilities.system_info"
    };
    size_t required_index;
    for (required_index = 0U;
         required_index < sizeof(required) / sizeof(required[0]); required_index++) {
        size_t index;
        int found = 0;
        for (index = 0U; index < count; index++) {
            if (fs_casecmp(keys[index], required[required_index]) == 0) {
                found = 1;
                break;
            }
        }
        if (!found) {
            errno = EINVAL;
            return 0;
        }
    }
    return 1;
}

int fs_platform_load(const char *path, FsPlatform *platform) {
    FILE *file;
    char line[1024];
    char section[48] = "device";
    char seen_keys[64][112];
    size_t seen_count = 0U;
    if (platform == NULL) {
        errno = EINVAL;
        return -1;
    }
    fs_platform_defaults(platform);
    if (path == NULL || path[0] == '\0') {
        return platform_expand(platform);
    }
    if (fs_copy(platform->profile_path, sizeof(platform->profile_path), path) != 0) return -1;
    file = fopen(path, "r");
    if (file == NULL) return -1;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text;
        if (platform_line_complete(file, line) < 0) {
            (void)fclose(file);
            return -1;
        }
        text = fs_trim(line);
        char *equals;
        char full_key[112];
        int written;
        if (text[0] == '\0' || text[0] == '#' || text[0] == ';') continue;
        if (text[0] == '[') {
            char *end = strchr(text + 1, ']');
            if (end == NULL) {
                (void)fclose(file);
                errno = EINVAL;
                return -1;
            }
            *end = '\0';
            if (!platform_tail_empty(end + 1)) {
                (void)fclose(file);
                errno = EINVAL;
                return -1;
            }
            if (fs_copy(section, sizeof(section), fs_trim(text + 1)) != 0) {
                (void)fclose(file);
                errno = ENAMETOOLONG;
                return -1;
            }
            if (!platform_section_known(section)) {
                (void)fclose(file);
                errno = EINVAL;
                return -1;
            }
            continue;
        }
        equals = strchr(text, '=');
        if (equals == NULL) {
            (void)fclose(file);
            errno = EINVAL;
            return -1;
        }
        *equals = '\0';
        written = snprintf(full_key, sizeof(full_key), "%s.%s", section, fs_trim(text));
        if (written < 0 || (size_t)written >= sizeof(full_key)) {
            (void)fclose(file);
            errno = ENAMETOOLONG;
            return -1;
        }
        {
            size_t index;
            for (index = 0U; index < seen_count; index++) {
                if (fs_casecmp(seen_keys[index], full_key) == 0) {
                    (void)fclose(file);
                    errno = EEXIST;
                    return -1;
                }
            }
            if (seen_count >= sizeof(seen_keys) / sizeof(seen_keys[0]) ||
                fs_copy(seen_keys[seen_count], sizeof(seen_keys[seen_count]), full_key) != 0) {
                (void)fclose(file);
                errno = ENOSPC;
                return -1;
            }
            seen_count++;
        }
        if (fs_platform_apply_override(platform, full_key, fs_trim(equals + 1)) < 0) {
            (void)fclose(file);
            return -1;
        }
    }
    if (ferror(file)) {
        (void)fclose(file);
        return -1;
    }
    if (!platform_required_keys_present(seen_keys, seen_count)) {
        (void)fclose(file);
        return -1;
    }
    if (fclose(file) != 0) return -1;
    return platform_expand(platform);
}

static int platform_action_key(const FsPlatform *p, FsAction action) {
    if (p == NULL) return 0;
    switch (action) {
        case FS_ACTION_UP: return p->key_up;
        case FS_ACTION_DOWN: return p->key_down;
        case FS_ACTION_LEFT: return p->key_left;
        case FS_ACTION_RIGHT: return p->key_right;
        case FS_ACTION_ACCEPT: return p->key_accept;
        case FS_ACTION_BACK: return p->key_back;
        case FS_ACTION_FAVORITE: return p->key_favorite;
        case FS_ACTION_OPTIONS: return p->key_options;
        case FS_ACTION_PAGE_LEFT: return p->key_page_left;
        case FS_ACTION_PAGE_RIGHT: return p->key_page_right;
        case FS_ACTION_START: return p->key_start;
        case FS_ACTION_SELECT: return p->key_select;
        case FS_ACTION_POWER: return p->key_power;
        default: return 0;
    }
}

const char *fs_platform_action_label(const FsPlatform *p, FsAction action) {
    if (p == NULL) return "?";
    switch (action) {
        case FS_ACTION_ACCEPT: return p->label_accept;
        case FS_ACTION_BACK: return p->label_back;
        case FS_ACTION_FAVORITE: return p->label_favorite;
        case FS_ACTION_OPTIONS: return p->label_options;
        case FS_ACTION_PAGE_LEFT: return p->label_page_left;
        case FS_ACTION_PAGE_RIGHT: return p->label_page_right;
        case FS_ACTION_START: return p->label_start;
        case FS_ACTION_SELECT: return p->label_select;
        case FS_ACTION_POWER: return p->label_power;
        default: return "?";
    }
}

int fs_platform_actions_share_key(const FsPlatform *p, FsAction left, FsAction right) {
    int left_key = platform_action_key(p, left);
    int right_key = platform_action_key(p, right);
    return left_key > 0 && left_key == right_key;
}

FsAction fs_platform_translate_key(const FsPlatform *p, int key) {
    if (p == NULL) return FS_ACTION_NONE;
    if (key == p->key_up) return FS_ACTION_UP;
    if (key == p->key_down) return FS_ACTION_DOWN;
    if (key == p->key_left) return FS_ACTION_LEFT;
    if (key == p->key_right) return FS_ACTION_RIGHT;
    if (key == p->key_accept) return FS_ACTION_ACCEPT;
    if (key == p->key_back) return FS_ACTION_BACK;
    if (key == p->key_favorite) return FS_ACTION_FAVORITE;
    if (key == p->key_options) return FS_ACTION_OPTIONS;
    if (key == p->key_page_left) return FS_ACTION_PAGE_LEFT;
    if (key == p->key_page_right) return FS_ACTION_PAGE_RIGHT;
    if (key == p->key_start) return FS_ACTION_START;
    if (key == p->key_select) return FS_ACTION_SELECT;
    if (key == p->key_power) return FS_ACTION_POWER;
    return FS_ACTION_NONE;
}

int fs_platform_has_capability(const FsPlatform *p, const char *capability) {
    if (capability == NULL || capability[0] == '\0') return 0;
    if (fs_casecmp(capability, "always") == 0) return 1;
    if (p == NULL) return 0;
    if (fs_casecmp(capability, "battery") == 0) return p->cap_battery;
    if (fs_casecmp(capability, "cpu_profiles") == 0) return p->cap_cpu_profiles;
    if (fs_casecmp(capability, "brightness") == 0) return p->cap_brightness;
    if (fs_casecmp(capability, "volume") == 0) return p->cap_volume;
    if (fs_casecmp(capability, "safe_shutdown") == 0) return p->cap_safe_shutdown;
    if (fs_casecmp(capability, "storage_health") == 0) return p->cap_storage_health;
    if (fs_casecmp(capability, "system_info") == 0) return p->cap_system_info;
    return 0;
}

int fs_platform_run_command(const char *command) {
    pid_t child;
    int status = 0;
    if (command == NULL || command[0] == '\0') {
        errno = ENOTSUP;
        return -1;
    }
    child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return 255;
}

int fs_platform_compute_viewport(int screen_width, int screen_height,
                                 int logical_width, int logical_height,
                                 int *x, int *y, int *width, int *height) {
    long long scaled_width;
    long long scaled_height;
    if (screen_width <= 0 || screen_height <= 0 || logical_width <= 0 ||
        logical_height <= 0 || x == NULL || y == NULL || width == NULL || height == NULL) {
        errno = EINVAL;
        return -1;
    }
    scaled_width = screen_width;
    scaled_height = ((long long)logical_height * scaled_width) / logical_width;
    if (scaled_height > screen_height) {
        scaled_height = screen_height;
        scaled_width = ((long long)logical_width * scaled_height) / logical_height;
    }
    if (scaled_width <= 0 || scaled_height <= 0 || scaled_width > 65535 || scaled_height > 65535) {
        errno = ERANGE;
        return -1;
    }
    *width = (int)scaled_width;
    *height = (int)scaled_height;
    *x = (screen_width - *width) / 2;
    *y = (screen_height - *height) / 2;
    return 0;
}

int fs_platform_validate(const FsPlatform *p, char *error, size_t error_size) {
    struct ActionKey { FsAction action; int key; };
    const struct ActionKey actions[] = {
        {FS_ACTION_UP, p != NULL ? p->key_up : 0},
        {FS_ACTION_DOWN, p != NULL ? p->key_down : 0},
        {FS_ACTION_LEFT, p != NULL ? p->key_left : 0},
        {FS_ACTION_RIGHT, p != NULL ? p->key_right : 0},
        {FS_ACTION_ACCEPT, p != NULL ? p->key_accept : 0},
        {FS_ACTION_BACK, p != NULL ? p->key_back : 0},
        {FS_ACTION_FAVORITE, p != NULL ? p->key_favorite : 0},
        {FS_ACTION_OPTIONS, p != NULL ? p->key_options : 0},
        {FS_ACTION_PAGE_LEFT, p != NULL ? p->key_page_left : 0},
        {FS_ACTION_PAGE_RIGHT, p != NULL ? p->key_page_right : 0},
        {FS_ACTION_START, p != NULL ? p->key_start : 0},
        {FS_ACTION_SELECT, p != NULL ? p->key_select : 0},
        {FS_ACTION_POWER, p != NULL ? p->key_power : 0}
    };
    const char *labels[] = {
        p != NULL ? p->label_accept : NULL, p != NULL ? p->label_back : NULL,
        p != NULL ? p->label_favorite : NULL, p != NULL ? p->label_options : NULL,
        p != NULL ? p->label_page_left : NULL, p != NULL ? p->label_page_right : NULL,
        p != NULL ? p->label_start : NULL, p != NULL ? p->label_select : NULL,
        p != NULL ? p->label_power : NULL
    };
    size_t i;
    size_t j;
#define PLATFORM_ERROR(msg) do { if (error != NULL && error_size > 0U) (void)fs_copy(error, error_size, msg); return -1; } while (0)
    if (p == NULL) PLATFORM_ERROR("platform is null");
    if (p->device_id[0] == '\0' || p->device_name[0] == '\0' || p->family[0] == '\0') PLATFORM_ERROR("device id, name, and family are required");
    if (p->screen_width < 160 || p->screen_height < 120) PLATFORM_ERROR("screen is smaller than 160x120");
    if (p->screen_bpp != 16 && p->screen_bpp != 24 && p->screen_bpp != 32) PLATFORM_ERROR("screen_bpp must be 16, 24, or 32");
    if (p->home[0] == '\0' || p->data_root[0] == '\0' ||
        p->rom_root[0] == '\0' || p->tool_root[0] == '\0') PLATFORM_ERROR("storage roots are required");
    if (fs_casecmp(p->launcher_provider, "gmenu2x") != 0 &&
        fs_casecmp(p->launcher_provider, "forge-manifest") != 0) PLATFORM_ERROR("unsupported launcher provider");
    if (p->provider_source[0] == '\0' || p->fallback_command[0] == '\0' ||
        p->frontend_value[0] == '\0') PLATFORM_ERROR("launcher source, fallback, and frontend are required");
    if (p->tools_manifest[0] == '\0') PLATFORM_ERROR("maintenance manifest is required");
    for (i = 0U; i < sizeof(actions) / sizeof(actions[0]); i++) {
        if (actions[i].key <= 0) PLATFORM_ERROR("an input action is unmapped");
        for (j = i + 1U; j < sizeof(actions) / sizeof(actions[0]); j++) {
            int allowed_alias =
                (actions[i].action == FS_ACTION_OPTIONS && actions[j].action == FS_ACTION_START) ||
                (actions[i].action == FS_ACTION_START && actions[j].action == FS_ACTION_OPTIONS);
            if (!allowed_alias && actions[i].key == actions[j].key) {
                PLATFORM_ERROR("input actions share a key");
            }
        }
    }
    for (i = 0U; i < sizeof(labels) / sizeof(labels[0]); i++) {
        if (labels[i] == NULL || labels[i][0] == '\0') PLATFORM_ERROR("an input label is empty");
    }
    if (p->cap_safe_shutdown && (p->reboot_command[0] == '\0' || p->poweroff_command[0] == '\0')) {
        PLATFORM_ERROR("safe_shutdown requires reboot and poweroff commands");
    }
    if (error != NULL && error_size > 0U) error[0] = '\0';
    return 0;
#undef PLATFORM_ERROR
}
