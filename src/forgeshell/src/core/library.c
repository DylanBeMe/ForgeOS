#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static void fs_strip_quotes(char *value) {
    size_t len;
    char quote;
    if (value == NULL) {
        return;
    }
    len = strlen(value);
    if (len < 2U) {
        return;
    }
    quote = value[0];
    if ((quote == '\'' || quote == '"') && value[len - 1U] == quote) {
        memmove(value, value + 1, len - 2U);
        value[len - 2U] = '\0';
    }
}

static uint32_t fs_path_hash(const char *text) {
    uint32_t hash = 2166136261U;
    const unsigned char *cursor = (const unsigned char *)text;
    while (cursor != NULL && *cursor != 0U) {
        hash ^= (uint32_t)*cursor++;
        hash *= 16777619U;
    }
    return hash;
}

static uint64_t fs_hash64_update(uint64_t hash, const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    while (cursor != NULL && *cursor != 0U) {
        hash ^= (uint64_t)*cursor++;
        hash *= UINT64_C(1099511628211);
    }
    hash ^= UINT64_C(0xff);
    hash *= UINT64_C(1099511628211);
    return hash;
}

static uint64_t fs_library_signature(const FsLibrary *library) {
    uint64_t signature;
    size_t i;
    if (library == NULL) {
        return 0U;
    }
    signature = UINT64_C(0x9e3779b97f4a7c15) ^ (uint64_t)library->system_count;
    for (i = 0U; i < library->system_count; i++) {
        uint64_t item = UINT64_C(1469598103934665603);
        item = fs_hash64_update(item, library->systems[i].id);
        item = fs_hash64_update(item, library->systems[i].romdir);
        item = fs_hash64_update(item, library->systems[i].romexts);
        signature ^= item;
    }
    return signature;
}

static int fs_system_compare(const void *left, const void *right) {
    const FsSystem *a = (const FsSystem *)left;
    const FsSystem *b = (const FsSystem *)right;
    int result = fs_casecmp(a->title, b->title);
    return result != 0 ? result : strcmp(a->source_path, b->source_path);
}

static int fs_game_compare(const void *left, const void *right) {
    const FsGame *a = (const FsGame *)left;
    const FsGame *b = (const FsGame *)right;
    int result;
    if (a->system_index != b->system_index) {
        return a->system_index - b->system_index;
    }
    result = fs_casecmp(a->title, b->title);
    return result != 0 ? result : strcmp(a->path, b->path);
}

static void fs_make_system_id(const char *base, const char *path,
                              char *id, size_t id_size) {
    char slug[35];
    size_t used = 0U;
    const unsigned char *cursor = (const unsigned char *)base;
    while (cursor != NULL && *cursor != 0U && used + 1U < sizeof(slug)) {
        unsigned char value = *cursor++;
        if ((value >= (unsigned char)'a' && value <= (unsigned char)'z') ||
            (value >= (unsigned char)'A' && value <= (unsigned char)'Z') ||
            (value >= (unsigned char)'0' && value <= (unsigned char)'9') ||
            value == (unsigned char)'-' || value == (unsigned char)'_') {
            slug[used++] = (char)value;
        } else {
            slug[used++] = '_';
        }
    }
    if (used == 0U) {
        slug[used++] = 's';
    }
    slug[used] = '\0';
    (void)snprintf(id, id_size, "%s-%08x", slug, (unsigned)fs_path_hash(path));
}

static int fs_parse_link(const char *path, FsSystem *system) {
    FILE *file;
    char line[1024];
    char filename[FS_MAX_PATH];
    char *base;
    char *dot;
    if (path == NULL || system == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(system, 0, sizeof(*system));
    if (fs_copy(filename, sizeof(filename), path) != 0) {
        return -1;
    }
    base = strrchr(filename, '/');
    base = base == NULL ? filename : base + 1;
    dot = strrchr(base, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
    fs_make_system_id(base, path, system->id, sizeof(system->id));
    if (system->id[0] == '\0' ||
        fs_copy(system->source_path, sizeof(system->source_path), path) != 0) {
        errno = ENAMETOOLONG;
        return -1;
    }
    if (fs_copy(system->title, sizeof(system->title), base) != 0) {
        (void)snprintf(system->title, sizeof(system->title), "%.*s",
                       (int)sizeof(system->title) - 1, base);
    }

    file = fopen(path, "r");
    if (file == NULL) {
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *trimmed;
        int line_status = fs_line_complete(file, line);
        if (line_status <= 0) {
            goto invalid_link;
        }
        trimmed = fs_trim(line);
        char *equals;
        char *key;
        char *value;
        if (trimmed[0] == '\0' || trimmed[0] == '#' || trimmed[0] == ';') {
            continue;
        }
        equals = strchr(trimmed, '=');
        if (equals == NULL) {
            continue;
        }
        *equals = '\0';
        key = fs_trim(trimmed);
        value = fs_trim(equals + 1);
        fs_strip_quotes(value);
        if (fs_casecmp(key, "title") == 0) {
            if (fs_copy(system->title, sizeof(system->title), value) != 0) {
                (void)snprintf(system->title, sizeof(system->title), "%.*s",
                               (int)sizeof(system->title) - 1, value);
            }
        } else if (fs_casecmp(key, "exec") == 0) {
            if (fs_copy(system->exec_path, sizeof(system->exec_path), value) != 0) goto invalid_link;
        } else if (fs_casecmp(key, "params") == 0) {
            if (fs_copy(system->params, sizeof(system->params), value) != 0) goto invalid_link;
        } else if (fs_casecmp(key, "workdir") == 0) {
            if (fs_copy(system->workdir, sizeof(system->workdir), value) != 0) goto invalid_link;
        } else if (fs_casecmp(key, "selectordir") == 0 ||
                   fs_casecmp(key, "romdir") == 0) {
            if (fs_copy(system->romdir, sizeof(system->romdir), value) != 0) goto invalid_link;
        } else if (fs_casecmp(key, "selectorfilter") == 0 ||
                   fs_casecmp(key, "romexts") == 0) {
            if (fs_copy(system->romexts, sizeof(system->romexts), value) != 0) goto invalid_link;
        }
    }
    if (ferror(file)) {
        (void)fclose(file);
        return -1;
    }
    if (fclose(file) != 0) {
        return -1;
    }
    if (system->exec_path[0] == '\0' || system->romdir[0] == '\0') {
        return 1;
    }
    if (system->workdir[0] == '\0') {
        char *slash;
        if (fs_copy(system->workdir, sizeof(system->workdir), system->exec_path) == 0) {
            slash = strrchr(system->workdir, '/');
            if (slash != NULL) {
                *slash = '\0';
            } else {
                (void)fs_copy(system->workdir, sizeof(system->workdir), ".");
            }
        }
    }
    if (system->params[0] == '\0') {
        (void)fs_copy(system->params, sizeof(system->params), "[rom]");
    }
    return 0;

invalid_link:
    (void)fclose(file);
    errno = ENAMETOOLONG;
    return -1;
}

static int fs_extension_token_matches(const char *dot, const char *token, size_t length) {
    size_t dot_length;
    size_t i;
    if (dot == NULL || token == NULL || length == 0U) return 0;
    if ((length == 1U && token[0] == '*') ||
        (length == 3U && token[0] == '*' && token[1] == '.' && token[2] == '*')) {
        return 1;
    }
    while (length > 0U && *token == '*') {
        token++;
        length--;
    }
    if (length == 0U) return 1;
    dot_length = strlen(dot);
    if (token[0] == '.') {
        if (length != dot_length) return 0;
        for (i = 0U; i < length; i++) {
            if (tolower((unsigned char)token[i]) != tolower((unsigned char)dot[i])) return 0;
        }
        return 1;
    }
    if (dot[0] != '.' || length + 1U != dot_length) return 0;
    for (i = 0U; i < length; i++) {
        if (tolower((unsigned char)token[i]) != tolower((unsigned char)dot[i + 1U])) return 0;
    }
    return 1;
}

static int fs_extension_matches(const char *filename, const char *extensions) {
    const char *dot;
    const char *cursor;
    if (filename == NULL) return 0;
    if (extensions == NULL || extensions[0] == '\0') return 1;
    dot = strrchr(filename, '.');
    if (dot == NULL) return 0;
    cursor = extensions;
    while (*cursor != '\0') {
        const char *start;
        size_t length;
        while (*cursor != '\0' &&
               (*cursor == ',' || *cursor == ';' || *cursor == '|' ||
                isspace((unsigned char)*cursor))) cursor++;
        start = cursor;
        while (*cursor != '\0' &&
               *cursor != ',' && *cursor != ';' && *cursor != '|' &&
               !isspace((unsigned char)*cursor)) cursor++;
        length = (size_t)(cursor - start);
        if (fs_extension_token_matches(dot, start, length)) return 1;
    }
    return 0;
}

void fs_library_init(FsLibrary *library, const char *cache_path) {
    if (library == NULL) {
        return;
    }
    memset(library, 0, sizeof(*library));
    if (cache_path != NULL) {
        (void)fs_copy(library->cache_path, sizeof(library->cache_path), cache_path);
    }
}

static void fs_scan_close_dir(FsLibrary *library) {
    if (library != NULL && library->scan_dir != NULL) {
        (void)closedir(library->scan_dir);
        library->scan_dir = NULL;
    }
}

void fs_library_close(FsLibrary *library) {
    if (library == NULL) return;
    fs_scan_close_dir(library);
    free(library->staged_games);
    library->staged_games = NULL;
    free(library->scan_seen);
    library->scan_seen = NULL;
    library->staged_count = 0U;
}

static int fs_system_source_exists(const FsLibrary *library, const char *path) {
    size_t i;
    for (i = 0U; i < library->system_count; i++) {
        if (strcmp(library->systems[i].source_path, path) == 0) {
            return 1;
        }
    }
    return 0;
}

static void fs_discover_dir(FsLibrary *library, const char *directory) {
    DIR *dir;
    struct dirent *entry;
    dir = opendir(directory);
    if (dir == NULL) {
        return;
    }
    while ((entry = readdir(dir)) != NULL && library->system_count < FS_MAX_SYSTEMS) {
        char path[FS_MAX_PATH];
        struct stat st;
        FsSystem parsed;
        int result;
        if (entry->d_name[0] == '.') {
            continue;
        }
        if (fs_path_join(path, sizeof(path), directory, entry->d_name) != 0 ||
            lstat(path, &st) != 0 || !S_ISREG(st.st_mode) ||
            fs_system_source_exists(library, path)) {
            continue;
        }
        result = fs_parse_link(path, &parsed);
        if (result == 0) {
            library->systems[library->system_count++] = parsed;
        }
    }
    (void)closedir(dir);
}

int fs_library_discover(FsLibrary *library, const char *section_dirs) {
    char dirs[FS_MAX_VALUE];
    char *save = NULL;
    char *token;
    if (library == NULL || section_dirs == NULL) {
        errno = EINVAL;
        return -1;
    }
    library->system_count = 0U;
    if (fs_copy(dirs, sizeof(dirs), section_dirs) != 0) {
        return -1;
    }
    for (token = strtok_r(dirs, ":", &save); token != NULL;
         token = strtok_r(NULL, ":", &save)) {
        char *directory = fs_trim(token);
        if (directory[0] != '\0') {
            fs_discover_dir(library, directory);
        }
    }
    qsort(library->systems, library->system_count, sizeof(library->systems[0]),
          fs_system_compare);
    return (int)library->system_count;
}


static int fs_manifest_expand(const FsPlatform *platform, const char *source,
                              char *out, size_t out_size) {
    char temp1[FS_MAX_VALUE];
    char temp2[FS_MAX_VALUE];
    char temp3[FS_MAX_VALUE];
    if (fs_replace_all(source, "${rom_root}", platform->rom_root,
                       temp1, sizeof(temp1)) != 0 ||
        fs_replace_all(temp1, "${data_root}", platform->data_root,
                       temp2, sizeof(temp2)) != 0 ||
        fs_replace_all(temp2, "${home}", platform->home,
                       temp3, sizeof(temp3)) != 0 ||
        fs_replace_all(temp3, "${tool_root}", platform->tool_root,
                       out, out_size) != 0) return -1;
    if (strstr(out, "${") != NULL) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

static int fs_manifest_id_valid(const char *id) {
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

static int fs_manifest_add_system(FsLibrary *library, FsSystem *system,
                                  const char *manifest_path) {
    char source[FS_MAX_PATH];
    size_t i;
    if (!fs_manifest_id_valid(system->id)) {
        errno = EINVAL;
        return -1;
    }
    for (i = 0U; i < library->system_count; i++) {
        if (strcmp(library->systems[i].id, system->id) == 0) {
            errno = EEXIST;
            return -1;
        }
    }
    if (system->exec_path[0] == '\0' || system->romdir[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (system->title[0] == '\0') {
        (void)fs_copy(system->title, sizeof(system->title), system->id);
    }
    if (system->params[0] == '\0') {
        (void)fs_copy(system->params, sizeof(system->params), "[rom]");
    }
    if (system->workdir[0] == '\0') {
        (void)fs_copy(system->workdir, sizeof(system->workdir), ".");
    }
    if (snprintf(source, sizeof(source), "%s#%s", manifest_path, system->id) >=
        (int)sizeof(source) ||
        fs_copy(system->source_path, sizeof(system->source_path), source) != 0) return -1;
    if (library->system_count >= FS_MAX_SYSTEMS) {
        errno = ENOSPC;
        return -1;
    }
    library->systems[library->system_count++] = *system;
    return 0;
}

static int fs_library_discover_manifest(FsLibrary *library, const FsPlatform *platform) {
    enum {
        MANIFEST_TITLE = 1U << 0,
        MANIFEST_EXEC = 1U << 1,
        MANIFEST_PARAMS = 1U << 2,
        MANIFEST_WORKDIR = 1U << 3,
        MANIFEST_ROMDIR = 1U << 4,
        MANIFEST_ROMEXTS = 1U << 5
    };
    FILE *file;
    char line[1024];
    FsSystem current;
    unsigned seen = 0U;
    int in_system = 0;
    int result = 0;
    int saved_errno = 0;
    if (library == NULL || platform == NULL) {
        errno = EINVAL;
        return -1;
    }
    library->system_count = 0U;
    memset(&current, 0, sizeof(current));
    file = fopen(platform->provider_source, "r");
    if (file == NULL) return -1;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *text;
        int line_status = fs_line_complete(file, line);
        if (line_status <= 0) {
            errno = line_status < 0 ? EOVERFLOW : EINVAL;
            result = -1;
            break;
        }
        text = fs_trim(line);
        if (text[0] == '\0' || text[0] == '#' || text[0] == ';') continue;
        if (text[0] == '[') {
            char *end = strchr(text + 1, ']');
            if (end == NULL || fs_trim(end + 1)[0] != '\0') {
                errno = EINVAL;
                result = -1;
                break;
            }
            *end = '\0';
            if (in_system) {
                result = fs_manifest_add_system(library, &current, platform->provider_source);
                if (result != 0) break;
            }
            memset(&current, 0, sizeof(current));
            seen = 0U;
            in_system = strncmp(text + 1, "system.", 7U) == 0;
            if (!in_system || !fs_manifest_id_valid(text + 8) ||
                fs_copy(current.id, sizeof(current.id), text + 8) != 0) {
                errno = EINVAL;
                result = -1;
                break;
            }
            continue;
        }
        if (!in_system) {
            errno = EINVAL;
            result = -1;
            break;
        }
        {
            char *equals = strchr(text, '=');
            char *key;
            char *value;
            char expanded[FS_MAX_VALUE];
            unsigned bit;
            if (equals == NULL) {
                errno = EINVAL;
                result = -1;
                break;
            }
            *equals = '\0';
            key = fs_trim(text);
            value = fs_trim(equals + 1);
            fs_strip_quotes(value);
            if (fs_casecmp(key, "title") == 0) bit = MANIFEST_TITLE;
            else if (fs_casecmp(key, "exec") == 0) bit = MANIFEST_EXEC;
            else if (fs_casecmp(key, "params") == 0) bit = MANIFEST_PARAMS;
            else if (fs_casecmp(key, "workdir") == 0) bit = MANIFEST_WORKDIR;
            else if (fs_casecmp(key, "romdir") == 0 ||
                     fs_casecmp(key, "selectordir") == 0) bit = MANIFEST_ROMDIR;
            else if (fs_casecmp(key, "romexts") == 0 ||
                     fs_casecmp(key, "selectorfilter") == 0) bit = MANIFEST_ROMEXTS;
            else {
                errno = EINVAL;
                result = -1;
                break;
            }
            if ((seen & bit) != 0U) {
                errno = EEXIST;
                result = -1;
                break;
            }
            seen |= bit;
            if (fs_manifest_expand(platform, value, expanded, sizeof(expanded)) != 0) {
                result = -1;
                break;
            }
            if (bit == MANIFEST_TITLE) result = fs_copy(current.title, sizeof(current.title), expanded);
            else if (bit == MANIFEST_EXEC) result = fs_copy(current.exec_path, sizeof(current.exec_path), expanded);
            else if (bit == MANIFEST_PARAMS) result = fs_copy(current.params, sizeof(current.params), expanded);
            else if (bit == MANIFEST_WORKDIR) result = fs_copy(current.workdir, sizeof(current.workdir), expanded);
            else if (bit == MANIFEST_ROMDIR) result = fs_copy(current.romdir, sizeof(current.romdir), expanded);
            else result = fs_copy(current.romexts, sizeof(current.romexts), expanded);
            if (result != 0) break;
        }
    }
    if (result == 0 && ferror(file)) result = -1;
    if (result == 0 && in_system) result = fs_manifest_add_system(library, &current,
                                                                  platform->provider_source);
    if (result == 0 && library->system_count == 0U) {
        errno = EINVAL;
        result = -1;
    }
    saved_errno = errno;
    if (fclose(file) != 0 && result == 0) {
        result = -1;
        saved_errno = errno;
    }
    if (result < 0) {
        library->system_count = 0U;
        errno = saved_errno == 0 ? EINVAL : saved_errno;
        return -1;
    }
    qsort(library->systems, library->system_count, sizeof(library->systems[0]),
          fs_system_compare);
    return (int)library->system_count;
}

int fs_library_discover_platform(FsLibrary *library, const FsPlatform *platform) {
    if (library == NULL || platform == NULL) {
        errno = EINVAL;
        return -1;
    }
    if (fs_casecmp(platform->launcher_provider, "gmenu2x") == 0) {
        return fs_library_discover(library, platform->provider_source);
    }
    if (fs_casecmp(platform->launcher_provider, "forge-manifest") == 0) {
        return fs_library_discover_manifest(library, platform);
    }
    errno = ENOTSUP;
    return -1;
}

static int fs_parse_cache_integer(const char *text, long long *value) {
    char *end = NULL;
    long long parsed;
    if (text == NULL || value == NULL || text[0] == '\0') {
        return -1;
    }
    errno = 0;
    parsed = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' || parsed < 0LL) {
        return -1;
    }
    *value = parsed;
    return 0;
}

static int fs_system_index_by_id(const FsLibrary *library, const char *id) {
    size_t i;
    if (library == NULL || id == NULL) {
        return -1;
    }
    for (i = 0U; i < library->system_count; i++) {
        if (strcmp(library->systems[i].id, id) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int fs_library_load_cache(FsLibrary *library) {
    FILE *file;
    char line[FS_MAX_PATH + FS_MAX_TITLE + 192];
    int header_seen = 0;
    char expected_header[64];
    if (library == NULL || library->cache_path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    library->cache_load_failed = 0;
    if (snprintf(expected_header, sizeof(expected_header),
                 "FORGESHELL-CACHE-4\t%016llx",
                 (unsigned long long)fs_library_signature(library)) >=
        (int)sizeof(expected_header)) {
        errno = ENOSPC;
        return -1;
    }
    file = fopen(library->cache_path, "r");
    if (file == NULL) {
        if (errno == ENOENT) {
            return 0;
        }
        library->cache_load_failed = 1;
        return -1;
    }
    library->game_count = 0U;
    while (fgets(line, sizeof(line), file) != NULL) {
        char *save = NULL;
        char *field;
        FsGame game;
        int index;
        long long size;
        long long mtime;
        int line_status = fs_line_complete(file, line);
        if (line_status <= 0) {
            goto invalid_cache;
        }
        if (!header_seen) {
            header_seen = 1;
            if (strcmp(fs_trim(line), expected_header) != 0) {
                goto invalid_cache;
            }
            continue;
        }
        memset(&game, 0, sizeof(game));
        field = strtok_r(line, "\t", &save);
        if (field == NULL) continue;
        index = fs_system_index_by_id(library, field);
        field = strtok_r(NULL, "\t", &save);
        if (field == NULL) continue;
        if (fs_parse_cache_integer(field, &size) != 0) continue;
        field = strtok_r(NULL, "\t", &save);
        if (field == NULL || fs_parse_cache_integer(field, &mtime) != 0) continue;
        field = strtok_r(NULL, "\t", &save);
        if (field == NULL || fs_copy(game.title, sizeof(game.title), field) != 0) continue;
        field = strtok_r(NULL, "\n", &save);
        if (field == NULL || fs_copy(game.path, sizeof(game.path), field) != 0) continue;
        if (index < 0) {
            goto invalid_cache;
        }
        if (library->game_count >= FS_MAX_GAMES) {
            continue;
        }
        game.system_index = index;
        game.size = (off_t)size;
        game.mtime = (time_t)mtime;
        if ((long long)game.size != size || (long long)game.mtime != mtime) {
            continue;
        }
        library->games[library->game_count++] = game;
    }
    if (ferror(file)) {
        (void)fclose(file);
        library->game_count = 0U;
        library->cache_load_failed = 1;
        return -1;
    }
    if (fclose(file) != 0) {
        library->game_count = 0U;
        library->cache_load_failed = 1;
        return -1;
    }
    if (!header_seen) {
        library->game_count = 0U;
        library->cache_load_failed = 1;
        errno = EINVAL;
        return -1;
    }
    qsort(library->games, library->game_count, sizeof(library->games[0]), fs_game_compare);
    return (int)library->game_count;

invalid_cache:
    (void)fclose(file);
    library->game_count = 0U;
    library->cache_load_failed = 1;
    errno = EINVAL;
    return -1;
}

int fs_library_save_cache(const FsLibrary *library) {
    size_t capacity;
    char *buffer;
    size_t used = 0U;
    size_t i;
    if (library == NULL || library->cache_path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    capacity = 32U + library->game_count *
        (FS_MAX_PATH + FS_MAX_TITLE + sizeof(library->systems[0].id) + 112U);
    buffer = (char *)malloc(capacity);
    if (buffer == NULL) {
        return -1;
    }
    {
        int header_written = snprintf(buffer, capacity,
                                      "FORGESHELL-CACHE-4\t%016llx\n",
                                      (unsigned long long)fs_library_signature(library));
        if (header_written < 0 || (size_t)header_written >= capacity) {
            free(buffer);
            errno = ENOSPC;
            return -1;
        }
        used = (size_t)header_written;
    }
    for (i = 0U; i < library->game_count; i++) {
        char safe_title[FS_MAX_TITLE];
        size_t j;
        int written;
        int system_index = library->games[i].system_index;
        if (system_index < 0 || (size_t)system_index >= library->system_count) {
            continue;
        }
        if (strchr(library->games[i].path, '\t') != NULL ||
            strchr(library->games[i].path, '\n') != NULL) {
            continue;
        }
        (void)fs_copy(safe_title, sizeof(safe_title), library->games[i].title);
        for (j = 0U; safe_title[j] != '\0'; j++) {
            if (safe_title[j] == '\t' || safe_title[j] == '\n' || safe_title[j] == '\r') {
                safe_title[j] = ' ';
            }
        }
        written = snprintf(buffer + used, capacity - used, "%s\t%lld\t%lld\t%s\t%s\n",
                           library->systems[system_index].id,
                           (long long)library->games[i].size,
                           (long long)library->games[i].mtime,
                           safe_title, library->games[i].path);
        if (written < 0 || (size_t)written >= capacity - used) {
            free(buffer);
            errno = ENOSPC;
            return -1;
        }
        used += (size_t)written;
    }
    if (fs_write_atomic(library->cache_path, buffer, used, 0644) != 0) {
        free(buffer);
        return -1;
    }
    free(buffer);
    return 0;
}

static int fs_scan_enqueue(FsLibrary *library, int system_index, int depth,
                           const char *path) {
    size_t slot;
    if (library == NULL || path == NULL || system_index < 0 ||
        (size_t)system_index >= library->system_count || depth < 0) {
        errno = EINVAL;
        return -1;
    }
    if (library->scan_tail >= FS_MAX_SCAN_DIRS) {
        library->scan_truncated = 1;
        errno = ENOSPC;
        return -1;
    }
    slot = library->scan_tail;
    if (fs_copy(library->scan_dirs[slot], sizeof(library->scan_dirs[slot]), path) != 0) {
        library->scan_truncated = 1;
        return -1;
    }
    library->scan_dir_system[slot] = (unsigned char)system_index;
    library->scan_dir_depth[slot] = (unsigned char)depth;
    library->scan_tail++;
    return 0;
}

int fs_library_start_scan(FsLibrary *library) {
    size_t i;
    if (library == NULL) {
        errno = EINVAL;
        return -1;
    }
    fs_library_close(library);
    library->scan_start_failed = 0;
    library->staged_games = (FsGame *)calloc(FS_MAX_GAMES, sizeof(FsGame));
    library->scan_seen = (uint16_t *)calloc(FS_SCAN_SEEN_SLOTS, sizeof(uint16_t));
    if (library->staged_games == NULL || library->scan_seen == NULL) {
        free(library->staged_games);
        library->staged_games = NULL;
        free(library->scan_seen);
        library->scan_seen = NULL;
        library->scan_start_failed = 1;
        library->scan_active = 0;
        library->scan_complete = 1;
        return -1;
    }
    library->staged_count = 0U;
    library->scan_head = 0U;
    library->scan_tail = 0U;
    library->scan_current_system = -1;
    library->scan_current_depth = 0;
    library->scan_current_path[0] = '\0';
    library->scan_complete = 0;
    library->scan_truncated = 0;
    library->scan_started_ms = fs_monotonic_milliseconds();
    library->last_scan_ms = 0LL;
    library->cache_save_failed = 0;
    for (i = 0U; i < library->system_count; i++) {
        if (fs_scan_enqueue(library, (int)i, 0, library->systems[i].romdir) != 0 &&
            library->scan_tail >= FS_MAX_SCAN_DIRS) {
            break;
        }
    }
    library->scan_active = library->scan_tail > 0U;
    library->scan_complete = !library->scan_active;
    if (!library->scan_active) {
        free(library->staged_games);
        library->staged_games = NULL;
        free(library->scan_seen);
        library->scan_seen = NULL;
    }
    return 0;
}

static int fs_scan_open_next(FsLibrary *library) {
    while (library->scan_head < library->scan_tail) {
        size_t slot = library->scan_head++;
        library->scan_current_system = (int)library->scan_dir_system[slot];
        library->scan_current_depth = (int)library->scan_dir_depth[slot];
        if (fs_copy(library->scan_current_path, sizeof(library->scan_current_path),
                    library->scan_dirs[slot]) != 0) {
            continue;
        }
        library->scan_dir = opendir(library->scan_current_path);
        if (library->scan_dir != NULL) {
            return 1;
        }
    }
    return 0;
}

static void fs_scan_finish(FsLibrary *library) {
    size_t count;
    fs_scan_close_dir(library);
    qsort(library->staged_games, library->staged_count,
          sizeof(library->staged_games[0]), fs_game_compare);
    count = library->staged_count;
    memcpy(library->games, library->staged_games, count * sizeof(library->games[0]));
    free(library->staged_games);
    library->staged_games = NULL;
    free(library->scan_seen);
    library->scan_seen = NULL;
    library->staged_count = 0U;
    library->game_count = count;
    library->scan_active = 0;
    library->scan_complete = 1;
    {
        long long finished_ms = fs_monotonic_milliseconds();
        library->last_scan_ms = finished_ms >= library->scan_started_ms ?
                                finished_ms - library->scan_started_ms : 0LL;
    }
    library->scan_head = 0U;
    library->scan_tail = 0U;
    library->cache_save_failed = fs_library_save_cache(library) != 0;
    if (!library->cache_save_failed) {
        library->cache_load_failed = 0;
    }
}

static int fs_scan_seen_slot(const FsLibrary *library, const char *path,
                             size_t *slot_out) {
    size_t slot;
    size_t probes;
    if (library == NULL || path == NULL || slot_out == NULL ||
        library->scan_seen == NULL || library->staged_games == NULL) {
        errno = EINVAL;
        return -1;
    }
    slot = (size_t)(fs_path_hash(path) % FS_SCAN_SEEN_SLOTS);
    for (probes = 0U; probes < FS_SCAN_SEEN_SLOTS; probes++) {
        uint16_t stored = library->scan_seen[slot];
        if (stored == 0U) {
            *slot_out = slot;
            return 0;
        }
        if ((size_t)stored <= library->staged_count &&
            strcmp(library->staged_games[(size_t)stored - 1U].path, path) == 0) {
            return 1;
        }
        slot = (slot + 1U) % FS_SCAN_SEEN_SLOTS;
    }
    errno = ENOSPC;
    return -1;
}

int fs_library_scan_step(FsLibrary *library, size_t budget) {
    size_t processed = 0U;
    if (library == NULL || budget == 0U || !library->scan_active) {
        return 0;
    }
    while (processed < budget && library->scan_active) {
        struct dirent *entry;
        FsSystem *system;
        if (library->scan_dir == NULL && !fs_scan_open_next(library)) {
            fs_scan_finish(library);
            break;
        }
        if (library->scan_current_system < 0 ||
            (size_t)library->scan_current_system >= library->system_count) {
            fs_scan_close_dir(library);
            continue;
        }
        system = &library->systems[library->scan_current_system];
        entry = readdir(library->scan_dir);
        if (entry == NULL) {
            (void)closedir(library->scan_dir);
            library->scan_dir = NULL;
            continue;
        }
        processed++;
        if (entry->d_name[0] == '.') {
            continue;
        }
        {
            char path[FS_MAX_PATH];
            struct stat st;
            if (fs_path_join(path, sizeof(path), library->scan_current_path,
                             entry->d_name) != 0) {
                library->scan_truncated = 1;
                continue;
            }
            if (lstat(path, &st) != 0) {
                continue;
            }
            if (S_ISLNK(st.st_mode)) {
                continue;
            }
            if (S_ISDIR(st.st_mode)) {
                if (library->scan_current_depth < FS_MAX_SCAN_DEPTH) {
                    (void)fs_scan_enqueue(library, library->scan_current_system,
                                          library->scan_current_depth + 1, path);
                }
                continue;
            }
            if (!S_ISREG(st.st_mode) ||
                !fs_extension_matches(entry->d_name, system->romexts)) {
                continue;
            }
            if (library->staged_count >= FS_MAX_GAMES) {
                library->scan_truncated = 1;
                continue;
            }
            {
                size_t seen_slot = 0U;
                int seen = fs_scan_seen_slot(library, path, &seen_slot);
                FsGame *game;
                if (seen > 0) {
                    continue;
                }
                if (seen < 0) {
                    library->scan_truncated = 1;
                    continue;
                }
                game = &library->staged_games[library->staged_count];
                memset(game, 0, sizeof(*game));
                if (fs_copy(game->path, sizeof(game->path), path) != 0) {
                    library->scan_truncated = 1;
                    continue;
                }
                fs_title_from_path(path, game->title, sizeof(game->title));
                game->system_index = library->scan_current_system;
                game->size = st.st_size;
                game->mtime = st.st_mtime;
                library->scan_seen[seen_slot] = (uint16_t)(library->staged_count + 1U);
                library->staged_count++;
            }
        }
    }
    return library->scan_active ? 1 : 0;
}

void fs_library_apply_favorites(FsLibrary *library, const FsFavorites *favorites) {
    size_t i;
    if (library == NULL || favorites == NULL) {
        return;
    }
    for (i = 0U; i < library->game_count; i++) {
        library->games[i].favorite = fs_favorites_contains(favorites, library->games[i].path);
    }
}

size_t fs_library_search(const FsLibrary *library, const char *query,
                         size_t *indices, size_t max_indices) {
    size_t i;
    size_t found = 0U;
    if (library == NULL || query == NULL || indices == NULL || max_indices == 0U) {
        return 0U;
    }
    for (i = 0U; i < library->game_count && found < max_indices; i++) {
        const FsSystem *system = &library->systems[library->games[i].system_index];
        if (fs_case_contains(library->games[i].title, query) ||
            fs_case_contains(system->title, query)) {
            indices[found++] = i;
        }
    }
    return found;
}

ssize_t fs_library_find_path(const FsLibrary *library, const char *path) {
    size_t i;
    if (library == NULL || path == NULL) {
        return -1;
    }
    for (i = 0U; i < library->game_count; i++) {
        if (strcmp(library->games[i].path, path) == 0) {
            return (ssize_t)i;
        }
    }
    return -1;
}

static size_t fs_root_length(const char *root) {
    size_t length;
    if (root == NULL) return 0U;
    length = strlen(root);
    while (length > 1U && root[length - 1U] == '/') length--;
    return length;
}

static int fs_path_is_under_root(const char *path, const char *root) {
    size_t root_length = fs_root_length(root);
    if (path == NULL || root_length == 0U) return 0;
    if (root_length == 1U && root[0] == '/') return path[0] == '/';
    return strncmp(path, root, root_length) == 0 && path[root_length] == '/';
}

static int fs_roots_equal(const char *left, const char *right) {
    size_t left_length = fs_root_length(left);
    size_t right_length = fs_root_length(right);
    return left_length > 0U && left_length == right_length &&
           strncmp(left, right, left_length) == 0;
}

int fs_library_system_supports_path(const FsSystem *system, const char *path) {
    const char *base;
    if (system == NULL || path == NULL ||
        !fs_path_is_under_root(path, system->romdir)) return 0;
    base = strrchr(path, '/');
    base = base == NULL ? path : base + 1;
    return fs_extension_matches(base, system->romexts);
}

int fs_library_systems_compatible(const FsSystem *source, const FsSystem *candidate,
                                  const char *path) {
    if (source == NULL || candidate == NULL || path == NULL) return 0;
    return fs_roots_equal(source->romdir, candidate->romdir) &&
           fs_library_system_supports_path(candidate, path);
}
