#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s (errno=%d)\n", \
                __FILE__, __LINE__, #condition, errno); \
        return 1; \
    } \
} while (0)

static int write_text(const char *path, const char *text, mode_t mode) {
    FILE *file = fopen(path, "w");
    if (file == NULL) return -1;
    if (fputs(text, file) == EOF || fclose(file) != 0) return -1;
    return chmod(path, mode);
}

static int make_dir(const char *path) {
    return fs_mkdir_p(path, 0755);
}

static int read_lines(const char *path, char lines[][FS_MAX_PATH], size_t max_lines,
                      size_t *line_count) {
    FILE *file = fopen(path, "r");
    char buffer[FS_MAX_PATH + 32];
    size_t count = 0U;
    if (file == NULL) return -1;
    while (count < max_lines && fgets(buffer, sizeof(buffer), file) != NULL) {
        char *line = fs_trim(buffer);
        if (fs_copy(lines[count], FS_MAX_PATH, line) != 0) {
            (void)fclose(file);
            return -1;
        }
        count++;
    }
    if (ferror(file)) {
        (void)fclose(file);
        return -1;
    }
    (void)fclose(file);
    *line_count = count;
    return 0;
}

static int find_game_by_suffix(const FsLibrary *library, const char *suffix) {
    size_t i;
    size_t suffix_length = strlen(suffix);
    for (i = 0U; i < library->game_count; i++) {
        size_t path_length = strlen(library->games[i].path);
        if (path_length >= suffix_length &&
            strcmp(library->games[i].path + path_length - suffix_length, suffix) == 0) {
            return (int)i;
        }
    }
    return -1;
}

int main(void) {
    char temp_template[] = "/tmp/forgeshell-test-XXXXXX";
    char *temp = mkdtemp(temp_template);
    char config_path[FS_MAX_PATH];
    char theme_path[FS_MAX_PATH];
    char section_one[FS_MAX_PATH];
    char section_two[FS_MAX_PATH];
    char sections[FS_MAX_VALUE];
    char gba_roms[FS_MAX_PATH];
    char snes_roms[FS_MAX_PATH];
    char nested[FS_MAX_PATH];
    char path[FS_MAX_PATH];
    char cache_path[FS_MAX_PATH];
    char favorites_path[FS_MAX_PATH];
    char sessions_path[FS_MAX_PATH];
    char output_path[FS_MAX_PATH];
    char frontend_path[FS_MAX_PATH];
    char env_path[FS_MAX_PATH];
    char script_path[FS_MAX_PATH];
    FsConfig config;
    FsTheme theme;
    FsLibrary *library;
    FsFavorites favorites;
    FsSessions sessions;
    FsSession session;
    FsSystem runner_system;
    FsGame runner_game;
    char command[4096];
    char lines[16][FS_MAX_PATH];
    size_t line_count = 0U;
    size_t results[8];
    int gba_game;
    int snes_game;
    int iterations = 0;

    CHECK(temp != NULL);
    CHECK(sizeof(FsLibrary) < 250000U);
    CHECK(sizeof(FsMetadata) < 1024U);
    CHECK(fs_parse_int("12   ", 3, 1, 20) == 12);
    CHECK(fs_path_join(path, sizeof(path), "", "relative/path") == 0);
    CHECK(strcmp(path, "relative/path") == 0);

    CHECK(fs_path_join(config_path, sizeof(config_path), temp, "config.ini") == 0);
    CHECK(write_text(config_path,
        "# retained comment\nscan_on_start=0\nscan_on_start=1\n", 0644) == 0);
    CHECK(fs_config_set(config_path, "scan_on_start", "0") == 0);
    {
        const char *keys[] = {"launcher_mode", "onboarding_complete"};
        const char *values[] = {"forgeshell", "1"};
        CHECK(fs_config_set_many(config_path, keys, values, 2U) == 0);
        CHECK(fs_config_set_many(config_path, keys, values, 0U) < 0 && errno == EINVAL);
    }
    CHECK(snprintf(path, sizeof(path), "%s.bak", config_path) < (int)sizeof(path));
    CHECK(!fs_file_exists(path));
    CHECK(fs_config_load(config_path, &config) == 0);
    CHECK(config.scan_on_start == 0);
    CHECK(strcmp(config.launcher_mode, "forgeshell") == 0 && config.onboarding_complete == 1);
    CHECK(fs_config_save_last_good(config_path) == 0);
    {
        char *text = NULL;
        CHECK(fs_read_text(config_path, &text, 4096U) == 0);
        CHECK(strstr(text, "# retained comment") != NULL);
        CHECK(strstr(text, "scan_on_start=0") != NULL);
        CHECK(strstr(strstr(text, "scan_on_start=0") + 1, "scan_on_start=") == NULL);
        free(text);
    }
    CHECK(write_text(config_path, "# no recognized settings\nunknown_only=value\n", 0644) == 0);
    CHECK(fs_config_load(config_path, &config) == -1 && errno == EINVAL);
    CHECK(fs_config_set(config_path, "large_text", "1") < 0 && errno == EINVAL);
    CHECK(fs_config_restore_last_good(config_path) == 0);
    CHECK(fs_config_load(config_path, &config) == 0);
    CHECK(fs_config_set(config_path, "unknown_key", "value") < 0 && errno == EINVAL);
    CHECK(write_text(config_path, "scan_on_start=maybe\n", 0644) == 0);
    CHECK(fs_config_load(config_path, &config) < 0 && errno == EINVAL);
    CHECK(write_text(config_path, "scan_on_start=1\nthis is malformed\n", 0644) == 0);
    CHECK(fs_config_load(config_path, &config) < 0 && errno == EINVAL);
    CHECK(write_text(config_path, "scan_on_start=1\nscan_on_start=0\n", 0644) == 0);
    CHECK(fs_config_load(config_path, &config) < 0 && errno == EEXIST);
    {
        FILE *long_config = fopen(config_path, "w");
        size_t long_index;
        CHECK(long_config != NULL);
        CHECK(fputs("unknown=", long_config) != EOF);
        for (long_index = 0U; long_index < 2048U; long_index++) CHECK(fputc('A', long_config) != EOF);
        CHECK(fputc('\n', long_config) != EOF);
        CHECK(fclose(long_config) == 0);
        CHECK(fs_config_load(config_path, &config) < 0 && errno == EOVERFLOW);
    }
    CHECK(fs_config_restore_last_good(config_path) == 0);

    CHECK(fs_path_join(theme_path, sizeof(theme_path), temp, "theme.ini") == 0);
    CHECK(write_text(theme_path, "accent=#123456\nfont_body=15\nradius=7\n", 0644) == 0);
    CHECK(fs_theme_load(theme_path, &theme) == 0);
    CHECK(theme.accent == 0x123456U && theme.font_body == 15 && theme.radius == 7);
    CHECK(write_text(theme_path, "accent=#123456\nfont_body=bad\n", 0644) == 0);
    CHECK(fs_theme_load(theme_path, &theme) < 0 && errno == EINVAL);
    CHECK(theme.accent == 0x71E6C1U && theme.font_body == 13 && theme.radius == 6);
    CHECK(write_text(theme_path, "accent=#123456\naccent=#654321\n", 0644) == 0);
    CHECK(fs_theme_load(theme_path, &theme) < 0 && errno == EEXIST);
    CHECK(theme.accent == 0x71E6C1U);
    CHECK(write_text(theme_path, "unknown=#123456\n", 0644) == 0);
    CHECK(fs_theme_load(theme_path, &theme) < 0 && errno == EINVAL);

    {
        FsFavorites failed_favorites;
        FsSessions failed_sessions;
        FsOverrides failed_overrides;
        FsMetadata failed_metadata;
        const char *bad_state_path = "/dev/null/child";
        fs_favorites_init(&failed_favorites, bad_state_path);
        CHECK(fs_favorites_load(&failed_favorites) < 0 && failed_favorites.load_failed);
        fs_sessions_init(&failed_sessions, bad_state_path);
        CHECK(fs_sessions_load(&failed_sessions) < 0 && failed_sessions.load_failed);
        fs_overrides_init(&failed_overrides, bad_state_path);
        CHECK(fs_overrides_load(&failed_overrides) < 0 && failed_overrides.load_failed);
        fs_metadata_init(&failed_metadata, bad_state_path);
        CHECK(fs_metadata_load(&failed_metadata) < 0 && failed_metadata.load_failed);
        fs_metadata_close(&failed_metadata);
    }

    CHECK(fs_path_join(path, sizeof(path), temp, "not-a-directory") == 0);
    CHECK(write_text(path, "x\n", 0644) == 0);
    CHECK(fs_mkdir_p(path, 0755) != 0 && errno == ENOTDIR);

    CHECK(fs_path_join(section_one, sizeof(section_one), temp, "sections/emulators") == 0);
    CHECK(fs_path_join(section_two, sizeof(section_two), temp, "sections/cores") == 0);
    CHECK(fs_path_join(gba_roms, sizeof(gba_roms), temp, "roms/GBA") == 0);
    CHECK(fs_path_join(snes_roms, sizeof(snes_roms), temp, "roms/SNES") == 0);
    CHECK(fs_path_join(nested, sizeof(nested), gba_roms, "RPG/Translated") == 0);
    CHECK(make_dir(section_one) == 0 && make_dir(section_two) == 0);
    CHECK(make_dir(nested) == 0 && make_dir(snes_roms) == 0);

    CHECK(fs_path_join(path, sizeof(path), section_one, "gba") == 0);
    {
        char link[2048];
        CHECK(snprintf(link, sizeof(link),
            "title=Game Boy Advance\nexec=/bin/true\nparams=--rom=\"[selFullPath]\"\n"
            "selectordir=%s\nselectorfilter=.gba,.zip\n", gba_roms) < (int)sizeof(link));
        CHECK(write_text(path, link, 0644) == 0);
    }
    CHECK(fs_path_join(path, sizeof(path), section_two, "snes") == 0);
    {
        char link[2048];
        CHECK(snprintf(link, sizeof(link),
            "title=Super Nintendo\nexec=/bin/true\nselectordir=%s\nselectorfilter=.smc,.sfc\n",
            snes_roms) < (int)sizeof(link));
        CHECK(write_text(path, link, 0644) == 0);
    }
    CHECK(fs_path_join(path, sizeof(path), section_two, "z-gba-alternate") == 0);
    {
        char link[2048];
        CHECK(snprintf(link, sizeof(link),
            "title=ZZ GBA Alternate\nexec=/bin/true\nparams=--rom=\"[selFullPath]\"\n"
            "selectordir=%s\nselectorfilter=.gba,.zip\n", gba_roms) < (int)sizeof(link));
        CHECK(write_text(path, link, 0644) == 0);
    }

    CHECK(fs_path_join(path, sizeof(path), section_two, "application-without-selector") == 0);
    CHECK(write_text(path, "title=Not a system\nexec=/bin/true\n", 0644) == 0);

    CHECK(fs_path_join(path, sizeof(path), nested, "Golden Sun.gba") == 0);
    CHECK(write_text(path, "rom", 0644) == 0);
    CHECK(fs_path_join(path, sizeof(path), gba_roms, "Advance Wars.zip") == 0);
    CHECK(write_text(path, "rom", 0644) == 0);
    CHECK(fs_path_join(path, sizeof(path), gba_roms, "notes.txt") == 0);
    CHECK(write_text(path, "ignore", 0644) == 0);
    CHECK(fs_path_join(path, sizeof(path), snes_roms, "Chrono Trigger.sfc") == 0);
    CHECK(write_text(path, "rom", 0644) == 0);
    CHECK(fs_path_join(path, sizeof(path), nested, "loop") == 0);
    CHECK(symlink(gba_roms, path) == 0);

    CHECK(fs_path_join(cache_path, sizeof(cache_path), temp, "state/cache.tsv") == 0);
    library = (FsLibrary *)calloc(1U, sizeof(*library));
    CHECK(library != NULL);
    fs_library_init(library, cache_path);
    CHECK(snprintf(sections, sizeof(sections), "%s:%s", section_one, section_two) <
          (int)sizeof(sections));
    CHECK(fs_library_discover(library, sections) == 3);
    CHECK(strcmp(library->systems[0].title, "Game Boy Advance") == 0);
    CHECK(strcmp(library->systems[0].romdir, gba_roms) == 0);
    CHECK(strcmp(library->systems[0].romexts, ".gba,.zip") == 0);
    CHECK(strstr(library->systems[0].params, "[selFullPath]") != NULL);
    CHECK(library->systems[0].id[0] != '\0' &&
          strcmp(library->systems[0].id, library->systems[1].id) != 0);

    library->cache_load_failed = 1;
    fs_library_start_scan(library);
    while (library->scan_active && iterations++ < 10000) {
        (void)fs_library_scan_step(library, 1U);
    }
    CHECK(!library->scan_active && library->scan_complete);
    CHECK(library->staged_games == NULL);
    CHECK(!library->scan_truncated);
    CHECK(!library->cache_load_failed);
    CHECK(!library->cache_save_failed);
    CHECK(library->game_count == 3U);
    gba_game = find_game_by_suffix(library, "Golden Sun.gba");
    snes_game = find_game_by_suffix(library, "Chrono Trigger.sfc");
    CHECK(gba_game >= 0 && snes_game >= 0);
    CHECK(strcmp(library->systems[library->games[gba_game].system_index].title,
                 "Game Boy Advance") == 0);
    CHECK(fs_file_exists(cache_path));

    {
        FsSystem swap = library->systems[0];
        library->systems[0] = library->systems[1];
        library->systems[1] = swap;
        library->game_count = 0U;
        CHECK(fs_library_load_cache(library) == 3);
        gba_game = find_game_by_suffix(library, "Golden Sun.gba");
        snes_game = find_game_by_suffix(library, "Chrono Trigger.sfc");
        CHECK(gba_game >= 0 && snes_game >= 0);
        CHECK(strcmp(library->systems[library->games[gba_game].system_index].title,
                     "Game Boy Advance") == 0);
        CHECK(strcmp(library->systems[library->games[snes_game].system_index].title,
                     "Super Nintendo") == 0);
    }

    {
        char original_romdir[FS_MAX_PATH];
        CHECK(fs_copy(original_romdir, sizeof(original_romdir),
                      library->systems[0].romdir) == 0);
        CHECK(fs_copy(library->systems[0].romdir,
                      sizeof(library->systems[0].romdir), "/changed/rom/path") == 0);
        CHECK(fs_library_load_cache(library) == -1);
        CHECK(library->cache_load_failed && library->game_count == 0U);
        CHECK(fs_copy(library->systems[0].romdir,
                      sizeof(library->systems[0].romdir), original_romdir) == 0);
        iterations = 0;
        fs_library_start_scan(library);
        while (library->scan_active && iterations++ < 10000) {
            (void)fs_library_scan_step(library, 1U);
        }
        CHECK(library->game_count == 3U && !library->cache_load_failed);
    }

    {
        FILE *corrupt = fopen(cache_path, "w");
        size_t i;
        CHECK(corrupt != NULL);
        CHECK(fputs("FORGESHELL-CACHE-3\t0000000000000000\n", corrupt) != EOF);
        for (i = 0U; i < 2048U; i++) {
            CHECK(fputc('A', corrupt) != EOF);
        }
        CHECK(fputc('\n', corrupt) != EOF);
        CHECK(fclose(corrupt) == 0);
        CHECK(fs_library_load_cache(library) == -1);
        CHECK(library->game_count == 0U && library->cache_load_failed);
        iterations = 0;
        fs_library_start_scan(library);
        while (library->scan_active && iterations++ < 10000) {
            (void)fs_library_scan_step(library, 1U);
        }
        CHECK(library->game_count == 3U && !library->cache_load_failed);
        gba_game = find_game_by_suffix(library, "Golden Sun.gba");
        CHECK(gba_game >= 0);
    }

    CHECK(fs_path_join(favorites_path, sizeof(favorites_path), temp, "state/favorites.txt") == 0);
    fs_favorites_init(&favorites, favorites_path);
    CHECK(fs_favorites_toggle(&favorites, library->games[gba_game].path) == 1);
    CHECK(fs_favorites_contains(&favorites, library->games[gba_game].path));
    fs_library_apply_favorites(library, &favorites);
    CHECK(library->games[gba_game].favorite == 1);
    CHECK(fs_library_search(library, "golden", results, 8U) == 1U);
    CHECK(results[0] == (size_t)gba_game);
    CHECK(fs_favorites_toggle(&favorites, library->games[gba_game].path) == 0);
    CHECK(!fs_favorites_contains(&favorites, library->games[gba_game].path));
    CHECK(write_text(favorites_path, "/rom/duplicate.gba\n/rom/duplicate.gba\n", 0644) == 0);
    CHECK(fs_favorites_load(&favorites) == 0);
    CHECK(favorites.count == 1U);

    {
        FsFavorites broken;
        fs_favorites_init(&broken, temp);
        CHECK(fs_favorites_toggle(&broken, "/rom/one.gba") == -1);
        CHECK(broken.count == 0U);
        broken.count = 1U;
        CHECK(fs_copy(broken.paths[0], FS_MAX_PATH, "/rom/one.gba") == 0);
        CHECK(fs_favorites_toggle(&broken, "/rom/one.gba") == -1);
        CHECK(broken.count == 1U && strcmp(broken.paths[0], "/rom/one.gba") == 0);
    }

    CHECK(fs_path_join(sessions_path, sizeof(sessions_path), temp, "state/sessions.log") == 0);
    CHECK(write_text(sessions_path,
        "999999999999999999999\t1\t0\tBad\tBad\t/bad\n"
        "100\t12\t0\tOlder\tGBA\t/older.gba\n", 0644) == 0);
    fs_sessions_init(&sessions, sessions_path);
    CHECK(fs_sessions_load(&sessions) == 1);
    CHECK(strcmp(sessions.items[0].title, "Older") == 0);
    memset(&session, 0, sizeof(session));
    session.started_at = (time_t)200;
    session.duration_seconds = 34U;
    session.exit_status = 0;
    CHECK(fs_copy(session.title, sizeof(session.title), "Newer") == 0);
    CHECK(fs_copy(session.system_title, sizeof(session.system_title), "SNES") == 0);
    CHECK(fs_copy(session.path, sizeof(session.path), "/newer.sfc") == 0);
    CHECK(fs_sessions_append(&sessions, &session) == 0);
    CHECK(sessions.count == 2U && strcmp(sessions.items[0].title, "Newer") == 0);

    CHECK(fs_path_join(script_path, sizeof(script_path), temp, "capture.sh") == 0);
    CHECK(fs_path_join(output_path, sizeof(output_path), temp, "captured.txt") == 0);
    CHECK(fs_path_join(frontend_path, sizeof(frontend_path), temp, "frontend.txt") == 0);
    CHECK(fs_path_join(env_path, sizeof(env_path), temp, "override-env.txt") == 0);
    CHECK(write_text(script_path,
        "#!/bin/sh\n: > \"$FORGESHELL_TEST_OUTPUT\"\n"
        "printf '%s\\n' \"${FRONTEND:-}\" > \"$FORGESHELL_TEST_FRONTEND\"\n"
        "if [ -n \"${FORGESHELL_TEST_ENV_OUTPUT:-}\" ]; then "
        "printf '%s|%s|%s|%s|%s\\n' \"${FORGESHELL_CPU_PROFILE:-}\" "
        "\"${FORGESHELL_ASPECT:-}\" \"${FORGESHELL_SCALING:-}\" "
        "\"${FORGESHELL_FRAMESKIP:-}\" \"${FORGESHELL_BIOS:-}\" "
        "> \"$FORGESHELL_TEST_ENV_OUTPUT\"; fi\n"
        "for arg in \"$@\"; do printf '%s\\n' \"$arg\" >> \"$FORGESHELL_TEST_OUTPUT\"; done\n",
        0755) == 0);
    memset(&runner_system, 0, sizeof(runner_system));
    memset(&runner_game, 0, sizeof(runner_game));
    CHECK(fs_copy(runner_system.exec_path, sizeof(runner_system.exec_path), script_path) == 0);
    CHECK(fs_copy(runner_system.workdir, sizeof(runner_system.workdir), temp) == 0);
    CHECK(fs_copy(runner_system.title, sizeof(runner_system.title), "Capture") == 0);
    CHECK(fs_path_join(path, sizeof(path), temp, "ROMs/Game's Name.zip") == 0);
    CHECK(fs_path_join(nested, sizeof(nested), temp, "ROMs") == 0);
    CHECK(make_dir(nested) == 0);
    CHECK(write_text(path, "rom", 0644) == 0);
    CHECK(fs_copy(runner_game.path, sizeof(runner_game.path), path) == 0);
    CHECK(fs_copy(runner_game.title, sizeof(runner_game.title), "Game's Name") == 0);
    CHECK(setenv("FORGESHELL_TEST_OUTPUT", output_path, 1) == 0);
    CHECK(setenv("FORGESHELL_TEST_FRONTEND", frontend_path, 1) == 0);
    CHECK(setenv("FORGESHELL_TEST_ENV_OUTPUT", env_path, 1) == 0);

    CHECK(fs_copy(runner_system.params, sizeof(runner_system.params),
        "--label \"[selFile]\" --path '[selFullPath]' --parts \"[selPath][selFile][selExt]\"") == 0);
    CHECK(fs_runner_build_command(&runner_system, &runner_game, command, sizeof(command)) == 0);
    {
        char *argv[16];
        char argv_storage[4096];
        CHECK(fs_runner_build_argv(&runner_system, &runner_game, argv, 16U,
                                   argv_storage, sizeof(argv_storage)) == 0);
        CHECK(strcmp(argv[0], runner_system.exec_path) == 0);
        CHECK(strcmp(argv[1], "--label") == 0);
        CHECK(strcmp(argv[2], "Game's Name") == 0);
        CHECK(strcmp(argv[3], "--path") == 0);
        CHECK(strcmp(argv[4], path) == 0);
        CHECK(strcmp(argv[5], "--parts") == 0);
        CHECK(strcmp(argv[6], path) == 0 && argv[7] == NULL);
        CHECK(fs_copy(runner_system.params, sizeof(runner_system.params),
                      "--rom [rom] > /tmp/output") == 0);
        CHECK(fs_runner_build_argv(&runner_system, &runner_game, argv, 16U,
                                   argv_storage, sizeof(argv_storage)) == 1);
        CHECK(fs_copy(runner_system.params, sizeof(runner_system.params),
                      "--label \"$HOME\"") == 0);
        CHECK(fs_runner_build_argv(&runner_system, &runner_game, argv, 16U,
                                   argv_storage, sizeof(argv_storage)) == 1);
        CHECK(fs_copy(runner_system.params, sizeof(runner_system.params),
                      "--label \"a\\q\"") == 0);
        CHECK(fs_runner_build_argv(&runner_system, &runner_game, argv, 16U,
                                   argv_storage, sizeof(argv_storage)) == 0);
        CHECK(strcmp(argv[1], "--label") == 0 && strcmp(argv[2], "a\\q") == 0);
        CHECK(fs_copy(runner_system.params, sizeof(runner_system.params),
                      "--label \"[selFile]\" --path '[selFullPath]' --parts \"[selPath][selFile][selExt]\"") == 0);
    }
    CHECK(fs_runner_launch(&runner_system, &runner_game, &session) == 0);
    CHECK(read_lines(frontend_path, lines, 16U, &line_count) == 0);
    CHECK(line_count == 1U && strcmp(lines[0], "gmenu2x") == 0);
    CHECK(read_lines(output_path, lines, 16U, &line_count) == 0);
    CHECK(line_count == 6U);
    CHECK(strcmp(lines[0], "--label") == 0);
    CHECK(strcmp(lines[1], "Game's Name") == 0);
    CHECK(strcmp(lines[2], "--path") == 0);
    CHECK(strcmp(lines[3], path) == 0);
    CHECK(strcmp(lines[4], "--parts") == 0);
    CHECK(strcmp(lines[5], path) == 0);
    CHECK(fs_copy(runner_system.params, sizeof(runner_system.params),
                  "--broken \"[rom]") == 0);
    CHECK(fs_runner_build_command(&runner_system, &runner_game,
                                  command, sizeof(command)) < 0 && errno == EINVAL);

    runner_system.params[0] = '\0';
    CHECK(fs_runner_launch(&runner_system, &runner_game, &session) == 0);
    CHECK(read_lines(output_path, lines, 16U, &line_count) == 0);
    CHECK(line_count == 1U && strcmp(lines[0], path) == 0);

    CHECK(fs_copy(runner_system.params, sizeof(runner_system.params), "--fixed") == 0);
    CHECK(fs_runner_launch(&runner_system, &runner_game, &session) == 0);
    CHECK(read_lines(output_path, lines, 16U, &line_count) == 0);
    CHECK(line_count == 1U && strcmp(lines[0], "--fixed") == 0);
    CHECK(setenv("TMPDIR", "/dev/null", 1) == 0);
    CHECK(fs_runner_launch(&runner_system, &runner_game, &session) == 0);
    CHECK(unsetenv("TMPDIR") == 0);

    {
        char override_path[FS_MAX_PATH];
        char metadata_path[FS_MAX_PATH];
        char art_path[FS_MAX_PATH];
        char helper_path[FS_MAX_PATH];
        char helper_log[FS_MAX_PATH];
        char lkg_path[FS_MAX_PATH];
        FsOverrides overrides;
        FsOverrides loaded;
        FsMetadata metadata;
        FsGameOverride *item;
        const FsGameOverride *found;
        CHECK(fs_path_join(override_path, sizeof(override_path), temp,
                           "state/game-overrides.tsv") == 0);
        fs_overrides_init(&overrides, override_path);
        item = fs_overrides_get(&overrides, library->games[gba_game].path, 1);
        CHECK(item != NULL);
        CHECK(fs_copy(item->emulator_id, sizeof(item->emulator_id),
                      library->systems[library->games[gba_game].system_index].id) == 0);
        CHECK(fs_copy(item->cpu_profile, sizeof(item->cpu_profile), "balanced") == 0);
        CHECK(fs_copy(item->aspect, sizeof(item->aspect), "original") == 0);
        CHECK(fs_copy(item->scaling, sizeof(item->scaling), "nearest") == 0);
        item->frameskip = 1;
        CHECK(fs_copy(item->bios_path, sizeof(item->bios_path), "/bios/gba.bin") == 0);
        CHECK(fs_overrides_save(&overrides) == 0);
        fs_overrides_init(&loaded, override_path);
        CHECK(fs_overrides_load(&loaded) == 1);
        found = fs_overrides_find(&loaded, library->games[gba_game].path);
        CHECK(found != NULL && found->frameskip == 1);
        CHECK(strcmp(found->cpu_profile, "balanced") == 0);
        {
            char invalid[(2 * FS_MAX_PATH) + 256];
            CHECK(snprintf(invalid, sizeof(invalid),
                           "# overrides\n%s\tfirst\tbogus\tbroken\tfuzzy\t4\t\n"
                           "%s\tsecond\tperformance\tfullscreen\tsmooth\t5\t/bios/new.bin\n",
                           library->games[gba_game].path,
                           library->games[gba_game].path) < (int)sizeof(invalid));
            CHECK(write_text(override_path, invalid, 0644) == 0);
            fs_overrides_init(&loaded, override_path);
            CHECK(fs_overrides_load(&loaded) == 1);
            found = fs_overrides_find(&loaded, library->games[gba_game].path);
            CHECK(found != NULL && strcmp(found->emulator_id, "second") == 0);
            CHECK(strcmp(found->cpu_profile, "performance") == 0);
            CHECK(strcmp(found->aspect, "fullscreen") == 0);
            CHECK(strcmp(found->scaling, "smooth") == 0 && found->frameskip == 5);
        }
        CHECK(fs_overrides_reset(&loaded, library->games[gba_game].path) == 0);
        CHECK(fs_overrides_find(&loaded, library->games[gba_game].path) == NULL);

        CHECK(fs_path_join(metadata_path, sizeof(metadata_path), temp,
                           "library/metadata.tsv") == 0);
        CHECK(fs_path_join(path, sizeof(path), temp, "library") == 0);
        CHECK(make_dir(path) == 0);
        CHECK(fs_path_join(art_path, sizeof(art_path), temp, "library/art/test.bmp") == 0);
        {
            char metadata_text[(2 * FS_MAX_PATH) + FS_MAX_TITLE + 32];
            CHECK(snprintf(metadata_text, sizeof(metadata_text),
                           "# metadata\n%s\tOld Title\t\n%s\tGolden Sun (Custom)\t%s\n",
                           library->games[gba_game].path,
                           library->games[gba_game].path, art_path) < (int)sizeof(metadata_text));
            CHECK(write_text(metadata_path, metadata_text, 0644) == 0);
        }
        fs_metadata_init(&metadata, metadata_path);
        CHECK(fs_metadata_load(&metadata) == 1);
        fs_metadata_apply(&metadata, library);
        CHECK(strcmp(library->games[gba_game].title, "Golden Sun (Custom)") == 0);
        CHECK(strcmp(library->games[gba_game].art_path, art_path) == 0);
        CHECK(library->games[gba_game].metadata_applied == 1);
        CHECK(fs_library_system_supports_path(
            &library->systems[library->games[gba_game].system_index],
            library->games[gba_game].path));
        {
            FsSystem alternate = library->systems[library->games[gba_game].system_index];
            FsSystem wrong_root = alternate;
            CHECK(fs_copy(alternate.id, sizeof(alternate.id), "gba-alt") == 0);
            CHECK(fs_copy(wrong_root.id, sizeof(wrong_root.id), "zip-other") == 0);
            CHECK(fs_copy(wrong_root.romdir, sizeof(wrong_root.romdir), snes_roms) == 0);
            CHECK(fs_library_systems_compatible(
                &library->systems[library->games[gba_game].system_index], &alternate,
                library->games[gba_game].path));
            CHECK(!fs_library_systems_compatible(
                &library->systems[library->games[gba_game].system_index], &wrong_root,
                library->games[gba_game].path));
            CHECK(!fs_library_system_supports_path(&wrong_root,
                                                   library->games[gba_game].path));
            CHECK(fs_copy(alternate.romexts, sizeof(alternate.romexts),
                          "*.GBA; zip | .7z") == 0);
            CHECK(fs_library_system_supports_path(&alternate,
                                                   library->games[gba_game].path));
            CHECK(fs_copy(alternate.romexts, sizeof(alternate.romexts), "*.*") == 0);
            CHECK(fs_library_system_supports_path(&alternate,
                                                   library->games[gba_game].path));
            CHECK(fs_copy(alternate.romexts, sizeof(alternate.romexts), ".zip") == 0);
            CHECK(!fs_library_system_supports_path(&alternate,
                                                    library->games[gba_game].path));
        }

        CHECK(snprintf(lkg_path, sizeof(lkg_path), "%s.last-good", config_path) <
              (int)sizeof(lkg_path));
        CHECK(fs_file_exists(lkg_path));
        CHECK(fs_config_save_last_good(config_path) == 0);
        CHECK(write_text(config_path, "launcher_mode=forgeshell\nlarge_text=1\n", 0644) == 0);
        CHECK(fs_config_restore_last_good(config_path) == 0);
        CHECK(fs_config_load(config_path, &config) == 0 && config.large_text == 0);

        CHECK(fs_path_join(helper_path, sizeof(helper_path), temp, "cpu-helper.sh") == 0);
        CHECK(fs_path_join(helper_log, sizeof(helper_log), temp, "cpu-helper.log") == 0);
        {
            char helper_script[FS_MAX_PATH + 96];
            CHECK(snprintf(helper_script, sizeof(helper_script),
                           "#!/bin/sh\nprintf '%%s\\n' \"$*\" >> '%s'\nexit 0\n",
                           helper_log) < (int)sizeof(helper_script));
            CHECK(write_text(helper_path, helper_script, 0755) == 0);
        }
        CHECK(setenv("FORGESHELL_CPU_HELPER", helper_path, 1) == 0);
        memset(&overrides, 0, sizeof(overrides));
        item = &overrides.items[0];
        CHECK(fs_copy(item->cpu_profile, sizeof(item->cpu_profile), "balanced") == 0);
        CHECK(fs_copy(item->aspect, sizeof(item->aspect), "original") == 0);
        CHECK(fs_copy(item->scaling, sizeof(item->scaling), "nearest") == 0);
        CHECK(fs_copy(item->bios_path, sizeof(item->bios_path), "/bios/gba.bin") == 0);
        item->frameskip = 1;
        CHECK(setenv("TMPDIR", "/dev/null", 1) == 0);
        CHECK(fs_runner_launch_override(&runner_system, &runner_game, item, &session) == 125);
        CHECK(unsetenv("TMPDIR") == 0);
        CHECK(fs_runner_launch_override(&runner_system, &runner_game, item, &session) == 0);
        CHECK(read_lines(env_path, lines, 16U, &line_count) == 0);
        CHECK(line_count == 1U &&
              strcmp(lines[0], "balanced|original|nearest|1|/bios/gba.bin") == 0);
        CHECK(read_lines(helper_log, lines, 16U, &line_count) == 0);
        CHECK(line_count == 2U && strncmp(lines[0], "apply balanced ", 15U) == 0 &&
              strncmp(lines[1], "restore ", 8U) == 0);
        CHECK(unsetenv("FORGESHELL_CPU_HELPER") == 0);
        fs_metadata_close(&metadata);
    }

    fs_library_close(library);
    free(library);
    return 0;
}
