#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fs_line_complete(FILE *file, const char *line) {
    int ch;
    if (strchr(line, '\n') != NULL || feof(file)) return 1;
    do { ch = fgetc(file); } while (ch != '\n' && ch != EOF);
    (void)ch;
    errno = EOVERFLOW;
    return -1;
}

static int fs_field_valid(const char *value) {
    return value != NULL && strchr(value, '\t') == NULL &&
           strchr(value, '\n') == NULL && strchr(value, '\r') == NULL;
}

static int fs_choice_valid(const char *value, const char *const *choices, size_t count) {
    size_t i;
    if (value == NULL) return 0;
    for (i = 0U; i < count; i++) {
        if (fs_casecmp(value, choices[i]) == 0) return 1;
    }
    return 0;
}

static void fs_override_sanitize(FsGameOverride *item) {
    static const char *const cpu[] = {"default", "eco", "balanced", "performance"};
    static const char *const aspect[] = {"default", "original", "4:3", "fullscreen"};
    static const char *const scaling[] = {"default", "nearest", "smooth"};
    if (!fs_choice_valid(item->cpu_profile, cpu, sizeof(cpu) / sizeof(cpu[0]))) {
        (void)fs_copy(item->cpu_profile, sizeof(item->cpu_profile), "default");
    }
    if (!fs_choice_valid(item->aspect, aspect, sizeof(aspect) / sizeof(aspect[0]))) {
        (void)fs_copy(item->aspect, sizeof(item->aspect), "default");
    }
    if (!fs_choice_valid(item->scaling, scaling, sizeof(scaling) / sizeof(scaling[0]))) {
        (void)fs_copy(item->scaling, sizeof(item->scaling), "default");
    }
    if (item->frameskip != -1 && item->frameskip != 0 && item->frameskip != 1 &&
        item->frameskip != 2 && item->frameskip != 5) item->frameskip = -1;
}


static int fs_split_override_line(char *line, char *fields[7]) {
    size_t i;
    char *cursor = line;
    for (i = 0U; i < 7U; i++) {
        char *tab;
        fields[i] = cursor;
        if (i == 6U) {
            char *end = strpbrk(cursor, "\r\n");
            if (end != NULL) *end = '\0';
            return 0;
        }
        tab = strchr(cursor, '\t');
        if (tab == NULL) return -1;
        *tab = '\0';
        cursor = tab + 1;
    }
    return -1;
}

static void fs_override_defaults(FsGameOverride *item) {
    memset(item, 0, sizeof(*item));
    item->frameskip = -1;
    (void)fs_copy(item->cpu_profile, sizeof(item->cpu_profile), "default");
    (void)fs_copy(item->aspect, sizeof(item->aspect), "default");
    (void)fs_copy(item->scaling, sizeof(item->scaling), "default");
}

void fs_overrides_init(FsOverrides *overrides, const char *path) {
    if (overrides == NULL) return;
    memset(overrides, 0, sizeof(*overrides));
    if (path != NULL) (void)fs_copy(overrides->path, sizeof(overrides->path), path);
}

const FsGameOverride *fs_overrides_find(const FsOverrides *overrides, const char *path) {
    size_t i;
    if (overrides == NULL || path == NULL) return NULL;
    for (i = 0U; i < overrides->count; i++) {
        if (strcmp(overrides->items[i].path, path) == 0) return &overrides->items[i];
    }
    return NULL;
}

FsGameOverride *fs_overrides_get(FsOverrides *overrides, const char *path, int create) {
    size_t i;
    if (overrides == NULL || path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }
    for (i = 0U; i < overrides->count; i++) {
        if (strcmp(overrides->items[i].path, path) == 0) return &overrides->items[i];
    }
    if (!create || overrides->count >= FS_MAX_OVERRIDES) {
        errno = create ? ENOSPC : ENOENT;
        return NULL;
    }
    fs_override_defaults(&overrides->items[overrides->count]);
    if (fs_copy(overrides->items[overrides->count].path,
                sizeof(overrides->items[overrides->count].path), path) != 0) return NULL;
    overrides->count++;
    return &overrides->items[overrides->count - 1U];
}

int fs_overrides_load(FsOverrides *overrides) {
    FILE *file;
    char line[(2 * FS_MAX_PATH) + FS_MAX_VALUE + 192];
    if (overrides == NULL || overrides->path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    overrides->count = 0U;
    overrides->load_failed = 0;
    file = fopen(overrides->path, "r");
    if (file == NULL) {
        if (errno == ENOENT) return 0;
        overrides->load_failed = 1;
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[7];
        FsGameOverride item;
        int complete = fs_line_complete(file, line);
        if (complete < 0) { overrides->load_failed = 1; break; }
        if (complete == 0 || line[0] == '#' || line[0] == '\n') continue;
        if (fs_split_override_line(line, fields) != 0 ||
            overrides->count >= FS_MAX_OVERRIDES) continue;
        fs_override_defaults(&item);
        if (fs_copy(item.path, sizeof(item.path), fields[0]) != 0 ||
            fs_copy(item.emulator_id, sizeof(item.emulator_id), fields[1]) != 0 ||
            fs_copy(item.cpu_profile, sizeof(item.cpu_profile), fields[2]) != 0 ||
            fs_copy(item.aspect, sizeof(item.aspect), fields[3]) != 0 ||
            fs_copy(item.scaling, sizeof(item.scaling), fields[4]) != 0 ||
            fs_copy(item.bios_path, sizeof(item.bios_path), fields[6]) != 0) continue;
        if (item.path[0] == '\0') continue;
        item.frameskip = fs_parse_int(fields[5], -1, -1, 5);
        fs_override_sanitize(&item);
        {
            FsGameOverride *existing = fs_overrides_get(overrides, item.path, 0);
            if (existing != NULL) {
                *existing = item;
            } else if (overrides->count < FS_MAX_OVERRIDES) {
                overrides->items[overrides->count++] = item;
            }
        }
    }
    if (ferror(file)) overrides->load_failed = 1;
    if (fclose(file) != 0) overrides->load_failed = 1;
    if (overrides->load_failed) {
        overrides->count = 0U;
        return -1;
    }
    return (int)overrides->count;
}

int fs_overrides_save(const FsOverrides *overrides) {
    size_t capacity;
    char *buffer;
    size_t used;
    size_t i;
    if (overrides == NULL || overrides->path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    capacity = 64U + overrides->count * ((2U * FS_MAX_PATH) + FS_MAX_VALUE + 192U);
    buffer = (char *)malloc(capacity);
    if (buffer == NULL) return -1;
    used = (size_t)snprintf(buffer, capacity, "# ForgeShell per-game overrides v1\n");
    for (i = 0U; i < overrides->count; i++) {
        const FsGameOverride *item = &overrides->items[i];
        int written;
        if (!fs_field_valid(item->path) || !fs_field_valid(item->emulator_id) ||
            !fs_field_valid(item->cpu_profile) || !fs_field_valid(item->aspect) ||
            !fs_field_valid(item->scaling) || !fs_field_valid(item->bios_path)) continue;
        written = snprintf(buffer + used, capacity - used,
                           "%s\t%s\t%s\t%s\t%s\t%d\t%s\n",
                           item->path, item->emulator_id, item->cpu_profile,
                           item->aspect, item->scaling, item->frameskip,
                           item->bios_path);
        if (written < 0 || (size_t)written >= capacity - used) {
            free(buffer);
            errno = ENOSPC;
            return -1;
        }
        used += (size_t)written;
    }
    if (fs_write_atomic(overrides->path, buffer, used, 0644) != 0) {
        free(buffer);
        return -1;
    }
    free(buffer);
    return 0;
}

int fs_overrides_reset(FsOverrides *overrides, const char *path) {
    size_t i;
    if (overrides == NULL || path == NULL) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0U; i < overrides->count; i++) {
        if (strcmp(overrides->items[i].path, path) == 0) {
            FsGameOverride removed = overrides->items[i];
            size_t remaining = overrides->count - i - 1U;
            if (remaining > 0U) {
                memmove(&overrides->items[i], &overrides->items[i + 1U],
                        remaining * sizeof(overrides->items[0]));
            }
            overrides->count--;
            if (fs_overrides_save(overrides) == 0) return 0;
            if (remaining > 0U) {
                memmove(&overrides->items[i + 1U], &overrides->items[i],
                        remaining * sizeof(overrides->items[0]));
            }
            overrides->items[i] = removed;
            overrides->count++;
            return -1;
        }
    }
    return 0;
}
