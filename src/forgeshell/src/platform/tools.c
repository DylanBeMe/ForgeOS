#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int tool_id_valid(const char *id) {
    size_t i;
    size_t length;
    if (id == NULL) return 0;
    length = strlen(id);
    if (length == 0U || length >= 48U || !isalnum((unsigned char)id[0])) return 0;
    for (i = 1U; i < length; i++) {
        unsigned char value = (unsigned char)id[i];
        if (!isalnum(value) && value != (unsigned char)'_' &&
            value != (unsigned char)'-' && value != (unsigned char)'.') return 0;
    }
    return 1;
}

void fs_tools_init(FsToolCatalog *catalog) {
    if (catalog != NULL) memset(catalog, 0, sizeof(*catalog));
}

static int tools_line_complete(FILE *file, const char *line) {
    int ch;
    if (strchr(line, '\n') != NULL || feof(file)) return 1;
    do { ch = fgetc(file); } while (ch != '\n' && ch != EOF);
    errno = EOVERFLOW;
    return -1;
}

static int split_line(char *line, char *fields[5]) {
    size_t i;
    char *cursor = line;
    for (i = 0U; i < 5U; i++) {
        char *tab;
        fields[i] = cursor;
        if (i == 4U) break;
        tab = strchr(cursor, '\t');
        if (tab == NULL) return -1;
        *tab = '\0';
        cursor = tab + 1;
    }
    return strchr(fields[4], '\t') == NULL ? 0 : -1;
}

static int expand_tool_command(const FsPlatform *platform, const char *source,
                               char *out, size_t out_size) {
    char temp[FS_MAX_VALUE];
    if (fs_replace_all(source, "${tool_root}", platform->tool_root,
                       temp, sizeof(temp)) != 0) return -1;
    if (fs_replace_all(temp, "${home}", platform->home,
                       out, out_size) != 0) return -1;
    if (strstr(out, "${") != NULL) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int fs_tools_load(FsToolCatalog *catalog, const FsPlatform *platform) {
    FILE *file;
    char line[1024];
    char seen_ids[FS_MAX_TOOLS * 2][48];
    size_t seen_count = 0U;
    if (catalog == NULL || platform == NULL) {
        errno = EINVAL;
        return -1;
    }
    fs_tools_init(catalog);
    file = fopen(platform->tools_manifest, "r");
    if (file == NULL) {
        catalog->load_failed = 1;
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[5];
        char *text;
        if (tools_line_complete(file, line) < 0) {
            (void)fclose(file);
            catalog->load_failed = 1;
            return -1;
        }
        text = fs_trim(line);
        FsToolEntry *item;
        size_t len = strlen(text);
        size_t existing;
        if (text[0] == '\0' || text[0] == '#') continue;
        if (len > 0U && text[len - 1U] == '\n') text[len - 1U] = '\0';
        if (split_line(text, fields) != 0 || !tool_id_valid(fs_trim(fields[0])) ||
            fs_trim(fields[1])[0] == '\0' || fs_trim(fields[3])[0] == '\0') {
            (void)fclose(file);
            catalog->load_failed = 1;
            errno = EINVAL;
            return -1;
        }
        for (existing = 0U; existing < seen_count; existing++) {
            if (strcmp(seen_ids[existing], fs_trim(fields[0])) == 0) {
                (void)fclose(file);
                catalog->load_failed = 1;
                errno = EEXIST;
                return -1;
            }
        }
        if (seen_count >= sizeof(seen_ids) / sizeof(seen_ids[0]) ||
            fs_copy(seen_ids[seen_count], sizeof(seen_ids[seen_count]),
                    fs_trim(fields[0])) != 0) {
            (void)fclose(file);
            catalog->load_failed = 1;
            errno = ENOSPC;
            return -1;
        }
        seen_count++;
        if (fs_trim(fields[4])[0] == '\0' ||
            (fs_casecmp(fs_trim(fields[4]), "always") != 0 &&
             fs_casecmp(fs_trim(fields[4]), "battery") != 0 &&
             fs_casecmp(fs_trim(fields[4]), "cpu_profiles") != 0 &&
             fs_casecmp(fs_trim(fields[4]), "brightness") != 0 &&
             fs_casecmp(fs_trim(fields[4]), "volume") != 0 &&
             fs_casecmp(fs_trim(fields[4]), "safe_shutdown") != 0 &&
             fs_casecmp(fs_trim(fields[4]), "storage_health") != 0 &&
             fs_casecmp(fs_trim(fields[4]), "system_info") != 0)) {
            (void)fclose(file);
            catalog->load_failed = 1;
            errno = EINVAL;
            return -1;
        }
        if (!fs_platform_has_capability(platform, fs_trim(fields[4]))) continue;
        if (catalog->count >= FS_MAX_TOOLS) {
            (void)fclose(file);
            catalog->load_failed = 1;
            errno = ENOSPC;
            return -1;
        }
        item = &catalog->items[catalog->count];
        errno = 0;
        if (fs_copy(item->id, sizeof(item->id), fs_trim(fields[0])) != 0 ||
            fs_copy(item->title, sizeof(item->title), fs_trim(fields[1])) != 0 ||
            fs_copy(item->meta, sizeof(item->meta), fs_trim(fields[2])) != 0 ||
            expand_tool_command(platform, fs_trim(fields[3]), item->command,
                                sizeof(item->command)) != 0 ||
            fs_copy(item->requires, sizeof(item->requires), fs_trim(fields[4])) != 0) {
            int saved_errno = errno;
            (void)fclose(file);
            catalog->load_failed = 1;
            errno = saved_errno == 0 ? ENAMETOOLONG : saved_errno;
            return -1;
        }
        catalog->count++;
    }
    if (ferror(file)) {
        (void)fclose(file);
        catalog->load_failed = 1;
        return -1;
    }
    if (fclose(file) != 0) {
        catalog->load_failed = 1;
        return -1;
    }
    return 0;
}
