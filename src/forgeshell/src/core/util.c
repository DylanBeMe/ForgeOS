#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

char *fs_trim(char *text) {
    char *end;
    if (text == NULL) {
        return NULL;
    }
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    end = text + strlen(text);
    while (end > text && isspace((unsigned char)end[-1])) {
        end--;
    }
    *end = '\0';
    return text;
}

int fs_copy(char *dst, size_t dst_size, const char *src) {
    size_t len;
    if (dst == NULL || dst_size == 0U || src == NULL) {
        errno = EINVAL;
        return -1;
    }
    len = strlen(src);
    if (len >= dst_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    memcpy(dst, src, len + 1U);
    return 0;
}

int fs_path_join(char *dst, size_t dst_size, const char *left, const char *right) {
    int written;
    const char *separator;
    if (dst == NULL || left == NULL || right == NULL || dst_size == 0U) {
        errno = EINVAL;
        return -1;
    }
    separator = left[0] == '\0' ? "" :
                (left[strlen(left) - 1U] == '/' ? "" : "/");
    while (*right == '/') {
        right++;
    }
    written = snprintf(dst, dst_size, "%s%s%s", left, separator, right);
    if (written < 0 || (size_t)written >= dst_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return 0;
}

static int fs_ensure_directory(const char *path, mode_t mode) {
    struct stat st;
    int mkdir_errno;
    if (mkdir(path, mode) == 0) {
        return 0;
    }
    mkdir_errno = errno;
    if (mkdir_errno != EEXIST) {
        errno = mkdir_errno;
        return -1;
    }
    if (stat(path, &st) != 0) {
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        errno = ENOTDIR;
        return -1;
    }
    return 0;
}

int fs_mkdir_p(const char *path, mode_t mode) {
    char copy[FS_MAX_PATH];
    char *cursor;
    if (path == NULL || path[0] == '\0' || fs_copy(copy, sizeof(copy), path) != 0) {
        errno = EINVAL;
        return -1;
    }
    for (cursor = copy + 1; *cursor != '\0'; cursor++) {
        if (*cursor == '/') {
            *cursor = '\0';
            if (fs_ensure_directory(copy, mode) != 0) {
                return -1;
            }
            *cursor = '/';
        }
    }
    return fs_ensure_directory(copy, mode);
}

int fs_file_exists(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int fs_dir_exists(const char *path) {
    struct stat st;
    return path != NULL && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int fs_parent_dir(const char *path, char *out, size_t out_size) {
    char *slash;
    if (fs_copy(out, out_size, path) != 0) {
        return -1;
    }
    slash = strrchr(out, '/');
    if (slash == NULL) {
        return fs_copy(out, out_size, ".");
    }
    if (slash == out) {
        slash[1] = '\0';
    } else {
        *slash = '\0';
    }
    return 0;
}

int fs_write_atomic(const char *path, const void *data, size_t size, mode_t mode) {
    char parent[FS_MAX_PATH];
    char temp[FS_MAX_PATH];
    int fd = -1;
    int dir_fd = -1;
    size_t offset = 0U;
    const unsigned char *bytes = (const unsigned char *)data;
    int saved_errno;

    if (path == NULL || data == NULL || fs_parent_dir(path, parent, sizeof(parent)) != 0) {
        errno = EINVAL;
        return -1;
    }
    if (fs_mkdir_p(parent, 0755) != 0) {
        return -1;
    }
    if (snprintf(temp, sizeof(temp), "%s.tmp.XXXXXX", path) >= (int)sizeof(temp)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    fd = mkstemp(temp);
    if (fd < 0) {
        return -1;
    }
    if (fchmod(fd, mode) != 0) {
        goto fail;
    }
    while (offset < size) {
        ssize_t written = write(fd, bytes + offset, size - offset);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            goto fail;
        }
        if (written == 0) {
            errno = EIO;
            goto fail;
        }
        offset += (size_t)written;
    }
    if (fsync(fd) != 0) {
        goto fail;
    }
    if (close(fd) != 0) {
        fd = -1;
        goto fail;
    }
    fd = -1;
    if (rename(temp, path) != 0) {
        goto fail;
    }
    dir_fd = open(parent, O_RDONLY | O_DIRECTORY);
    if (dir_fd >= 0) {
        (void)fsync(dir_fd);
        (void)close(dir_fd);
    }
    return 0;

fail:
    saved_errno = errno;
    if (fd >= 0) {
        (void)close(fd);
    }
    (void)unlink(temp);
    errno = saved_errno;
    return -1;
}

int fs_read_text(const char *path, char **out, size_t max_size) {
    FILE *file;
    long length;
    char *buffer;
    size_t read_count;
    if (path == NULL || out == NULL || max_size == 0U) {
        errno = EINVAL;
        return -1;
    }
    *out = NULL;
    file = fopen(path, "rb");
    if (file == NULL) {
        return -1;
    }
    if (fseek(file, 0L, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        (unsigned long)length > max_size || fseek(file, 0L, SEEK_SET) != 0) {
        (void)fclose(file);
        errno = EFBIG;
        return -1;
    }
    buffer = (char *)malloc((size_t)length + 1U);
    if (buffer == NULL) {
        (void)fclose(file);
        return -1;
    }
    read_count = fread(buffer, 1U, (size_t)length, file);
    if (read_count != (size_t)length || ferror(file)) {
        free(buffer);
        (void)fclose(file);
        errno = EIO;
        return -1;
    }
    buffer[read_count] = '\0';
    (void)fclose(file);
    *out = buffer;
    return 0;
}

int fs_parse_bool(const char *value, int fallback) {
    if (value == NULL) {
        return fallback;
    }
    if (fs_casecmp(value, "1") == 0 || fs_casecmp(value, "true") == 0 ||
        fs_casecmp(value, "yes") == 0 || fs_casecmp(value, "on") == 0) {
        return 1;
    }
    if (fs_casecmp(value, "0") == 0 || fs_casecmp(value, "false") == 0 ||
        fs_casecmp(value, "no") == 0 || fs_casecmp(value, "off") == 0) {
        return 0;
    }
    return fallback;
}

int fs_parse_int(const char *value, int fallback, int min_value, int max_value) {
    char *end = NULL;
    long parsed;
    if (value == NULL || value[0] == '\0') {
        return fallback;
    }
    errno = 0;
    parsed = strtol(value, &end, 10);
    if (errno != 0 || end == value) {
        return fallback;
    }
    while (*end != '\0' && isspace((unsigned char)*end)) {
        end++;
    }
    if (*end != '\0' || parsed < min_value || parsed > max_value) {
        return fallback;
    }
    return (int)parsed;
}

uint32_t fs_parse_color(const char *value, uint32_t fallback) {
    char *end = NULL;
    unsigned long parsed;
    const char *cursor = value;
    if (cursor == NULL) {
        return fallback;
    }
    if (*cursor == '#') {
        cursor++;
    }
    if (strlen(cursor) != 6U) {
        return fallback;
    }
    errno = 0;
    parsed = strtoul(cursor, &end, 16);
    if (errno != 0 || end == cursor || *end != '\0' || parsed > 0xFFFFFFUL) {
        return fallback;
    }
    return (uint32_t)parsed;
}

int fs_casecmp(const char *a, const char *b) {
    unsigned char ca;
    unsigned char cb;
    if (a == NULL || b == NULL) {
        return (a == b) ? 0 : (a == NULL ? -1 : 1);
    }
    while (*a != '\0' && *b != '\0') {
        ca = (unsigned char)tolower((unsigned char)*a);
        cb = (unsigned char)tolower((unsigned char)*b);
        if (ca != cb) {
            return (int)ca - (int)cb;
        }
        a++;
        b++;
    }
    return (int)(unsigned char)*a - (int)(unsigned char)*b;
}

int fs_case_contains(const char *haystack, const char *needle) {
    size_t needle_len;
    const char *cursor;
    if (haystack == NULL || needle == NULL) {
        return 0;
    }
    needle_len = strlen(needle);
    if (needle_len == 0U) {
        return 1;
    }
    for (cursor = haystack; *cursor != '\0'; cursor++) {
        size_t i;
        for (i = 0U; i < needle_len; i++) {
            if (cursor[i] == '\0' ||
                tolower((unsigned char)cursor[i]) != tolower((unsigned char)needle[i])) {
                break;
            }
        }
        if (i == needle_len) {
            return 1;
        }
    }
    return 0;
}

void fs_title_from_path(const char *path, char *title, size_t title_size) {
    const char *base;
    char temp[FS_MAX_TITLE];
    char *dot;
    char *cursor;
    if (title == NULL || title_size == 0U) {
        return;
    }
    title[0] = '\0';
    if (path == NULL) {
        return;
    }
    base = strrchr(path, '/');
    base = base == NULL ? path : base + 1;
    if (fs_copy(temp, sizeof(temp), base) != 0) {
        (void)snprintf(temp, sizeof(temp), "%.*s", (int)sizeof(temp) - 1, base);
    }
    dot = strrchr(temp, '.');
    if (dot != NULL && dot != temp) {
        *dot = '\0';
    }
    for (cursor = temp; *cursor != '\0'; cursor++) {
        if (*cursor == '_' || *cursor == '.') {
            *cursor = ' ';
        }
    }
    (void)fs_copy(title, title_size, temp);
}

int fs_shell_quote(const char *text, char *out, size_t out_size) {
    size_t used = 0U;
    const char *cursor;
    if (text == NULL || out == NULL || out_size < 3U) {
        errno = EINVAL;
        return -1;
    }
    out[used++] = '\'';
    for (cursor = text; *cursor != '\0'; cursor++) {
        const char *piece = (*cursor == '\'') ? "'\\''" : NULL;
        if (piece != NULL) {
            size_t len = strlen(piece);
            if (used + len >= out_size) {
                errno = ENOSPC;
                return -1;
            }
            memcpy(out + used, piece, len);
            used += len;
        } else {
            if (used + 1U >= out_size) {
                errno = ENOSPC;
                return -1;
            }
            out[used++] = *cursor;
        }
    }
    if (used + 2U > out_size) {
        errno = ENOSPC;
        return -1;
    }
    out[used++] = '\'';
    out[used] = '\0';
    return 0;
}

int fs_replace_all(const char *src, const char *needle, const char *replacement,
                   char *out, size_t out_size) {
    size_t used = 0U;
    size_t needle_len;
    size_t replacement_len;
    const char *cursor;
    if (src == NULL || needle == NULL || replacement == NULL || out == NULL ||
        out_size == 0U || needle[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    needle_len = strlen(needle);
    replacement_len = strlen(replacement);
    cursor = src;
    while (*cursor != '\0') {
        const char *match = strstr(cursor, needle);
        size_t chunk = match == NULL ? strlen(cursor) : (size_t)(match - cursor);
        if (used + chunk + (match == NULL ? 0U : replacement_len) >= out_size) {
            errno = ENOSPC;
            return -1;
        }
        memcpy(out + used, cursor, chunk);
        used += chunk;
        if (match == NULL) {
            break;
        }
        memcpy(out + used, replacement, replacement_len);
        used += replacement_len;
        cursor = match + needle_len;
    }
    out[used] = '\0';
    return 0;
}

long long fs_monotonic_milliseconds(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return ((long long)ts.tv_sec * 1000LL) + ((long long)ts.tv_nsec / 1000000LL);
    }
#endif
    {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) == 0) {
            return ((long long)tv.tv_sec * 1000LL) + ((long long)tv.tv_usec / 1000LL);
        }
    }
    return 0LL;
}

long long fs_monotonic_seconds(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        return (long long)ts.tv_sec;
    }
#endif
    {
        struct timeval tv;
        if (gettimeofday(&tv, NULL) == 0) {
            return (long long)tv.tv_sec;
        }
    }
    return 0LL;
}
