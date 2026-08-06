#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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

void fs_favorites_init(FsFavorites *favorites, const char *path) {
    if (favorites == NULL) {
        return;
    }
    memset(favorites, 0, sizeof(*favorites));
    if (path != NULL) {
        (void)fs_copy(favorites->path, sizeof(favorites->path), path);
    }
}

int fs_favorites_load(FsFavorites *favorites) {
    FILE *file;
    char line[FS_MAX_PATH + 8];
    if (favorites == NULL || favorites->path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    favorites->count = 0U;
    favorites->load_failed = 0;
    file = fopen(favorites->path, "r");
    if (file == NULL) {
        if (errno == ENOENT) return 0;
        favorites->load_failed = 1;
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL && favorites->count < FS_MAX_FAVORITES) {
        char *path;
        int line_status = fs_line_complete(file, line);
        if (line_status < 0) {
            favorites->load_failed = 1;
            break;
        }
        if (line_status == 0) {
            continue;
        }
        path = fs_trim(line);
        if (path[0] == '\0' || path[0] == '#' ||
            fs_favorites_contains(favorites, path)) {
            continue;
        }
        if (fs_copy(favorites->paths[favorites->count], FS_MAX_PATH, path) == 0) {
            favorites->count++;
        }
    }
    if (ferror(file) || favorites->load_failed) {
        (void)fclose(file);
        favorites->count = 0U;
        favorites->load_failed = 1;
        return -1;
    }
    if (fclose(file) != 0) {
        favorites->count = 0U;
        favorites->load_failed = 1;
        return -1;
    }
    return 0;
}

int fs_favorites_contains(const FsFavorites *favorites, const char *path) {
    size_t i;
    if (favorites == NULL || path == NULL) {
        return 0;
    }
    for (i = 0U; i < favorites->count; i++) {
        if (strcmp(favorites->paths[i], path) == 0) {
            return 1;
        }
    }
    return 0;
}

static int fs_favorites_save(const FsFavorites *favorites) {
    size_t capacity;
    char *buffer;
    size_t used = 0U;
    size_t i;
    if (favorites == NULL || favorites->path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    capacity = 32U + favorites->count * (FS_MAX_PATH + 2U);
    buffer = (char *)malloc(capacity);
    if (buffer == NULL) {
        return -1;
    }
    used = (size_t)snprintf(buffer, capacity, "# ForgeShell favorites v1\n");
    for (i = 0U; i < favorites->count; i++) {
        int written;
        if (strchr(favorites->paths[i], '\n') != NULL ||
            strchr(favorites->paths[i], '\r') != NULL) {
            continue;
        }
        written = snprintf(buffer + used, capacity - used, "%s\n", favorites->paths[i]);
        if (written < 0 || (size_t)written >= capacity - used) {
            free(buffer);
            errno = ENOSPC;
            return -1;
        }
        used += (size_t)written;
    }
    if (fs_write_atomic(favorites->path, buffer, used, 0644) != 0) {
        free(buffer);
        return -1;
    }
    free(buffer);
    return 0;
}

int fs_favorites_toggle(FsFavorites *favorites, const char *path) {
    size_t i;
    if (favorites == NULL || path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    for (i = 0U; i < favorites->count; i++) {
        if (strcmp(favorites->paths[i], path) == 0) {
            char removed[FS_MAX_PATH];
            size_t remaining = favorites->count - i - 1U;
            (void)fs_copy(removed, sizeof(removed), favorites->paths[i]);
            if (remaining > 0U) {
                memmove(favorites->paths[i], favorites->paths[i + 1U],
                        remaining * sizeof(favorites->paths[0]));
            }
            favorites->count--;
            if (fs_favorites_save(favorites) == 0) {
                return 0;
            }
            if (remaining > 0U) {
                memmove(favorites->paths[i + 1U], favorites->paths[i],
                        remaining * sizeof(favorites->paths[0]));
            }
            (void)fs_copy(favorites->paths[i], FS_MAX_PATH, removed);
            favorites->count++;
            return -1;
        }
    }
    if (favorites->count >= FS_MAX_FAVORITES ||
        fs_copy(favorites->paths[favorites->count], FS_MAX_PATH, path) != 0) {
        errno = ENOSPC;
        return -1;
    }
    favorites->count++;
    if (fs_favorites_save(favorites) == 0) {
        return 1;
    }
    favorites->count--;
    favorites->paths[favorites->count][0] = '\0';
    return -1;
}

void fs_sessions_init(FsSessions *sessions, const char *path) {
    if (sessions == NULL) {
        return;
    }
    memset(sessions, 0, sizeof(*sessions));
    if (path != NULL) {
        (void)fs_copy(sessions->path, sizeof(sessions->path), path);
    }
}

static void fs_clean_field(char *text) {
    char *cursor;
    if (text == NULL) {
        return;
    }
    for (cursor = text; *cursor != '\0'; cursor++) {
        if (*cursor == '\t' || *cursor == '\n' || *cursor == '\r') {
            *cursor = ' ';
        }
    }
}

static int fs_parse_signed(const char *text, long long min_value,
                           long long max_value, long long *out) {
    char *end = NULL;
    long long value;
    if (text == NULL || out == NULL || text[0] == '\0') {
        return -1;
    }
    errno = 0;
    value = strtoll(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value < min_value || value > max_value) {
        return -1;
    }
    *out = value;
    return 0;
}

static int fs_parse_session_line(char *line, FsSession *session) {
    char *save = NULL;
    char *field;
    long long started;
    long long duration;
    long long status;
    time_t converted_time;
    memset(session, 0, sizeof(*session));
    field = strtok_r(line, "\t", &save);
    if (field == NULL || fs_parse_signed(field, 0LL, 4102444800LL, &started) != 0) return -1;
    field = strtok_r(NULL, "\t", &save);
    if (field == NULL || fs_parse_signed(field, 0LL, 31536000LL, &duration) != 0) return -1;
    field = strtok_r(NULL, "\t", &save);
    if (field == NULL || fs_parse_signed(field, 0LL, 255LL, &status) != 0) return -1;
    field = strtok_r(NULL, "\t", &save);
    if (field == NULL || fs_copy(session->title, sizeof(session->title), field) != 0) return -1;
    field = strtok_r(NULL, "\t", &save);
    if (field == NULL || fs_copy(session->system_title, sizeof(session->system_title), field) != 0) return -1;
    field = strtok_r(NULL, "\n", &save);
    if (field == NULL || fs_copy(session->path, sizeof(session->path), field) != 0) return -1;
    converted_time = (time_t)started;
    if ((long long)converted_time != started) return -1;
    session->started_at = converted_time;
    session->duration_seconds = (unsigned)duration;
    session->exit_status = (int)status;
    return 0;
}

int fs_sessions_load(FsSessions *sessions) {
    FILE *file;
    char line[FS_MAX_PATH + (2 * FS_MAX_TITLE) + 128];
    FsSession ring[FS_MAX_SESSIONS];
    size_t total = 0U;
    size_t stored = 0U;
    size_t i;
    if (sessions == NULL || sessions->path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    sessions->count = 0U;
    sessions->load_failed = 0;
    file = fopen(sessions->path, "r");
    if (file == NULL) {
        if (errno == ENOENT) return 0;
        sessions->load_failed = 1;
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        FsSession parsed;
        int line_status = fs_line_complete(file, line);
        if (line_status < 0) {
            sessions->load_failed = 1;
            break;
        }
        if (line_status == 0 || line[0] == '#') {
            continue;
        }
        if (fs_parse_session_line(line, &parsed) == 0) {
            ring[total % FS_MAX_SESSIONS] = parsed;
            total++;
            if (stored < FS_MAX_SESSIONS) {
                stored++;
            }
        }
    }
    if (ferror(file) || sessions->load_failed) {
        (void)fclose(file);
        sessions->count = 0U;
        sessions->load_failed = 1;
        return -1;
    }
    if (fclose(file) != 0) {
        sessions->load_failed = 1;
        return -1;
    }
    for (i = 0U; i < stored; i++) {
        size_t logical = total - 1U - i;
        sessions->items[i] = ring[logical % FS_MAX_SESSIONS];
    }
    sessions->count = stored;
    return (int)stored;
}

static int fs_sessions_compact(const FsSessions *sessions) {
    size_t capacity;
    char *buffer;
    size_t used;
    size_t i;
    if (sessions == NULL) {
        errno = EINVAL;
        return -1;
    }
    capacity = 32U + sessions->count * (FS_MAX_PATH + (2U * FS_MAX_TITLE) + 96U);
    buffer = (char *)malloc(capacity);
    if (buffer == NULL) {
        return -1;
    }
    used = (size_t)snprintf(buffer, capacity, "# ForgeShell sessions v1\n");
    for (i = sessions->count; i > 0U; i--) {
        const FsSession *item = &sessions->items[i - 1U];
        int written = snprintf(buffer + used, capacity - used, "%lld\t%u\t%d\t%s\t%s\t%s\n",
                               (long long)item->started_at, item->duration_seconds,
                               item->exit_status, item->title,
                               item->system_title, item->path);
        if (written < 0 || (size_t)written >= capacity - used) {
            free(buffer);
            errno = ENOSPC;
            return -1;
        }
        used += (size_t)written;
    }
    if (fs_write_atomic(sessions->path, buffer, used, 0644) != 0) {
        free(buffer);
        return -1;
    }
    free(buffer);
    return 0;
}

int fs_sessions_append(FsSessions *sessions, const FsSession *session) {
    FsSession clean;
    char line[FS_MAX_PATH + (2 * FS_MAX_TITLE) + 128];
    int written;
    int fd;
    size_t offset = 0U;
    struct stat st;
    if (sessions == NULL || session == NULL || sessions->path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    clean = *session;
    fs_clean_field(clean.title);
    fs_clean_field(clean.system_title);
    fs_clean_field(clean.path);
    written = snprintf(line, sizeof(line), "%lld\t%u\t%d\t%s\t%s\t%s\n",
                       (long long)clean.started_at, clean.duration_seconds,
                       clean.exit_status, clean.title, clean.system_title, clean.path);
    if (written < 0 || (size_t)written >= sizeof(line)) {
        errno = ENOSPC;
        return -1;
    }
    {
        char parent[FS_MAX_PATH];
        char *slash;
        if (fs_copy(parent, sizeof(parent), sessions->path) != 0) return -1;
        slash = strrchr(parent, '/');
        if (slash != NULL) {
            *slash = '\0';
            if (parent[0] != '\0' && fs_mkdir_p(parent, 0755) != 0) return -1;
        }
    }
    fd = open(sessions->path, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) {
        return -1;
    }
    while (offset < (size_t)written) {
        ssize_t count = write(fd, line + offset, (size_t)written - offset);
        if (count < 0) {
            if (errno == EINTR) continue;
            (void)close(fd);
            return -1;
        }
        if (count == 0) {
            (void)close(fd);
            errno = EIO;
            return -1;
        }
        offset += (size_t)count;
    }
    if (fsync(fd) != 0) {
        int saved_errno = errno;
        (void)close(fd);
        errno = saved_errno;
        return -1;
    }
    if (close(fd) != 0) {
        return -1;
    }
    if (sessions->count > 0U) {
        size_t move_count = sessions->count < FS_MAX_SESSIONS ?
                            sessions->count : FS_MAX_SESSIONS - 1U;
        memmove(&sessions->items[1], &sessions->items[0],
                move_count * sizeof(sessions->items[0]));
    }
    sessions->items[0] = clean;
    if (sessions->count < FS_MAX_SESSIONS) {
        sessions->count++;
    }
    if (stat(sessions->path, &st) == 0 && st.st_size > (off_t)(256U * 1024U)) {
        if (fs_sessions_compact(sessions) != 0) {
            fprintf(stderr, "ForgeShell: session log compaction was deferred\n");
        }
    }
    return 0;
}
