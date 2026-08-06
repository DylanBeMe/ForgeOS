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

static int fs_split_metadata_line(char *line, char *fields[3]) {
    size_t i;
    char *cursor = line;
    for (i = 0U; i < 3U; i++) {
        char *tab;
        fields[i] = cursor;
        if (i == 2U) {
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

static int fs_metadata_compare(const void *left, const void *right) {
    const FsMetadataEntry *a = (const FsMetadataEntry *)left;
    const FsMetadataEntry *b = (const FsMetadataEntry *)right;
    int result = strcmp(a->path, b->path);
    if (result != 0) return result;
    if (a->source_order < b->source_order) return -1;
    if (a->source_order > b->source_order) return 1;
    return 0;
}

static const FsMetadataEntry *fs_metadata_find(const FsMetadata *metadata,
                                                const char *path) {
    size_t low = 0U;
    size_t high;
    if (metadata == NULL || path == NULL) return NULL;
    high = metadata->count;
    while (low < high) {
        size_t mid = low + (high - low) / 2U;
        int cmp = strcmp(path, metadata->items[mid].path);
        if (cmp == 0) return &metadata->items[mid];
        if (cmp < 0) high = mid; else low = mid + 1U;
    }
    return NULL;
}

static int fs_metadata_reserve(FsMetadata *metadata, size_t needed) {
    size_t capacity;
    FsMetadataEntry *items;
    if (needed <= metadata->capacity) return 0;
    capacity = metadata->capacity == 0U ? 32U : metadata->capacity;
    while (capacity < needed && capacity < FS_MAX_METADATA) {
        size_t next = capacity * 2U;
        capacity = next > FS_MAX_METADATA ? FS_MAX_METADATA : next;
    }
    if (capacity < needed) {
        errno = ENOSPC;
        return -1;
    }
    items = (FsMetadataEntry *)realloc(metadata->items,
                                       capacity * sizeof(metadata->items[0]));
    if (items == NULL) return -1;
    metadata->items = items;
    metadata->capacity = capacity;
    return 0;
}

void fs_metadata_init(FsMetadata *metadata, const char *path) {
    if (metadata == NULL) return;
    memset(metadata, 0, sizeof(*metadata));
    if (path != NULL) (void)fs_copy(metadata->path, sizeof(metadata->path), path);
}

void fs_metadata_close(FsMetadata *metadata) {
    if (metadata == NULL) return;
    free(metadata->items);
    metadata->items = NULL;
    metadata->count = 0U;
    metadata->capacity = 0U;
}

int fs_metadata_load(FsMetadata *metadata) {
    FILE *file;
    char line[(2 * FS_MAX_PATH) + FS_MAX_TITLE + 16];
    size_t source_order = 0U;
    if (metadata == NULL || metadata->path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    fs_metadata_close(metadata);
    metadata->load_failed = 0;
    file = fopen(metadata->path, "r");
    if (file == NULL) {
        if (errno == ENOENT) return 0;
        metadata->load_failed = 1;
        return -1;
    }
    while (fgets(line, sizeof(line), file) != NULL) {
        char *fields[3];
        FsMetadataEntry *entry;
        int complete = fs_line_complete(file, line);
        if (complete < 0) { metadata->load_failed = 1; break; }
        if (complete == 0 || line[0] == '#' || line[0] == '\n') continue;
        if (fs_split_metadata_line(line, fields) != 0 || fields[0][0] == '\0') continue;
        if (metadata->count >= FS_MAX_METADATA ||
            fs_metadata_reserve(metadata, metadata->count + 1U) != 0) {
            metadata->load_failed = 1;
            break;
        }
        entry = &metadata->items[metadata->count];
        memset(entry, 0, sizeof(*entry));
        if (fs_copy(entry->path, sizeof(entry->path), fields[0]) != 0 ||
            fs_copy(entry->title, sizeof(entry->title), fields[1]) != 0 ||
            fs_copy(entry->art_path, sizeof(entry->art_path), fields[2]) != 0) continue;
        entry->source_order = source_order++;
        metadata->count++;
    }
    if (ferror(file)) metadata->load_failed = 1;
    if (fclose(file) != 0) metadata->load_failed = 1;
    if (metadata->count > 1U) {
        size_t read_index = 0U;
        size_t write_index = 0U;
        qsort(metadata->items, metadata->count, sizeof(metadata->items[0]),
              fs_metadata_compare);
        while (read_index < metadata->count) {
            size_t group_end = read_index + 1U;
            while (group_end < metadata->count &&
                   strcmp(metadata->items[read_index].path,
                          metadata->items[group_end].path) == 0) {
                group_end++;
            }
            metadata->items[write_index++] = metadata->items[group_end - 1U];
            read_index = group_end;
        }
        metadata->count = write_index;
    }
    if (metadata->load_failed) {
        fs_metadata_close(metadata);
        return -1;
    }
    return (int)metadata->count;
}

void fs_metadata_apply(const FsMetadata *metadata, FsLibrary *library) {
    size_t i;
    if (metadata == NULL || library == NULL) return;
    for (i = 0U; i < library->game_count; i++) {
        const FsMetadataEntry *entry = fs_metadata_find(metadata, library->games[i].path);
        library->games[i].metadata_applied = 0;
        library->games[i].art_path[0] = '\0';
        if (entry == NULL) continue;
        if (entry->title[0] != '\0') {
            (void)fs_copy(library->games[i].title,
                          sizeof(library->games[i].title), entry->title);
        }
        if (entry->art_path[0] != '\0') {
            (void)fs_copy(library->games[i].art_path,
                          sizeof(library->games[i].art_path), entry->art_path);
        }
        library->games[i].metadata_applied = 1;
    }
}
