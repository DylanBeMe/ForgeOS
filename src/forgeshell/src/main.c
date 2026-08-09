#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef FORGESHELL_DEFAULT_PROFILE
#define FORGESHELL_DEFAULT_PROFILE ""
#endif

static int ensure_default_config(const char *path) {
    static const char defaults[] =
        "# ForgeShell settings. Unknown keys are retained during updates.\n"
        "launcher_mode=gmenu2x\n"
        "scan_on_start=0\n"
        "large_text=0\n"
        "high_contrast=0\n"
        "scan_budget=12\n"
        "onboarding_complete=0\n"
        "safe_mode_next_boot=0\n"
        "metadata_enabled=1\n"
        "show_recovery_hint=1\n";
    if (fs_file_exists(path)) return 0;
    return fs_write_atomic(path, defaults, sizeof(defaults) - 1U, 0644);
}

static void write_startup_metrics(const FsPlatform *platform, long long elapsed_ms,
                                  const FsLibrary *library, int cache_present,
                                  int safe_mode) {
    char path[FS_MAX_PATH];
    char content[640];
    int written;
    if (platform == NULL || library == NULL ||
        fs_path_join(path, sizeof(path), platform->home, "state/startup-metrics.ini") != 0) return;
    written = snprintf(content, sizeof(content),
                       "version=%s\ndevice=%s\nresolution=%dx%d\nstartup_ms=%lld\n"
                       "systems=%u\ngames=%u\ncache_present=%d\n"
                       "cache_load_failed=%d\nsafe_mode=%d\n",
                       FS_VERSION, platform->device_id, platform->screen_width,
                       platform->screen_height, elapsed_ms,
                       (unsigned)library->system_count, (unsigned)library->game_count,
                       cache_present ? 1 : 0, library->cache_load_failed ? 1 : 0,
                       safe_mode ? 1 : 0);
    if (written > 0 && (size_t)written < sizeof(content)) {
        (void)fs_write_atomic(path, content, (size_t)written, 0644);
    }
}

static int make_path(char *out, size_t out_size, const char *home, const char *name) {
    if (fs_path_join(out, out_size, home, name) != 0) {
        fprintf(stderr, "ForgeShell: path is too long: %s/%s\n", home, name);
        return -1;
    }
    return 0;
}

static int parse_resolution(const char *value, int *width, int *height) {
    char *end = NULL;
    long w;
    long h;
    if (value == NULL || width == NULL || height == NULL) return -1;
    errno = 0;
    w = strtol(value, &end, 10);
    if (errno != 0 || end == value || (*end != 'x' && *end != 'X')) return -1;
    value = end + 1;
    errno = 0;
    h = strtol(value, &end, 10);
    if (errno != 0 || end == value || *end != '\0' || w < 160 || w > 3840 ||
        h < 120 || h > 2160) return -1;
    *width = (int)w;
    *height = (int)h;
    return 0;
}

static int path_has_prefix(const char *value, const char *prefix) {
    size_t length;
    if (value == NULL || prefix == NULL || prefix[0] == '\0') return 0;
    length = strlen(prefix);
    if (strncmp(value, prefix, length) != 0) return 0;
    if (strcmp(prefix, ".") == 0) return value[1] == '/' || value[1] == '\0';
    return value[length] == '/' || value[length] == '\0';
}

static int rebase_path_prefix(char *value, size_t value_size,
                              const char *old_root, const char *new_root) {
    char buffer[FS_MAX_VALUE];
    const char *suffix;
    int written;
    if (value == NULL || old_root == NULL || new_root == NULL) return -1;
    if (!path_has_prefix(value, old_root)) return 0;
    suffix = value + strlen(old_root);
    if (strcmp(old_root, ".") == 0 && suffix[0] == '/') suffix++;
    if (suffix[0] == '/') {
        written = snprintf(buffer, sizeof(buffer), "%s%s", new_root, suffix);
    } else if (suffix[0] != '\0') {
        written = snprintf(buffer, sizeof(buffer), "%s/%s", new_root, suffix);
    } else {
        written = snprintf(buffer, sizeof(buffer), "%s", new_root);
    }
    if (written < 0 || (size_t)written >= sizeof(buffer)) return -1;
    return fs_copy(value, value_size, buffer);
}

static int rebase_colon_paths(char *value, size_t value_size,
                              const char *old_root, const char *new_root) {
    char source[FS_MAX_VALUE];
    char output[FS_MAX_VALUE] = "";
    char *cursor;
    char *part;
    size_t used = 0U;
    if (fs_copy(source, sizeof(source), value) != 0) return -1;
    cursor = source;
    while ((part = strsep(&cursor, ":")) != NULL) {
        char rebased[FS_MAX_PATH];
        size_t length;
        if (fs_copy(rebased, sizeof(rebased), part) != 0 ||
            rebase_path_prefix(rebased, sizeof(rebased), old_root, new_root) != 0) return -1;
        length = strlen(rebased);
        if (used + length + (used > 0U ? 1U : 0U) + 1U > sizeof(output)) return -1;
        if (used > 0U) output[used++] = ':';
        memcpy(output + used, rebased, length);
        used += length;
        output[used] = '\0';
    }
    return fs_copy(value, value_size, output);
}

static int apply_data_root(FsPlatform *platform, const char *root) {
    char old_root[FS_MAX_PATH];
    if (platform == NULL || root == NULL || root[0] == '\0' ||
        fs_copy(old_root, sizeof(old_root), platform->data_root) != 0) return -1;
    if (rebase_path_prefix(platform->rom_root, sizeof(platform->rom_root), old_root, root) != 0 ||
        rebase_path_prefix(platform->home, sizeof(platform->home), old_root, root) != 0 ||
        rebase_path_prefix(platform->tool_root, sizeof(platform->tool_root), old_root, root) != 0 ||
        rebase_colon_paths(platform->provider_source, sizeof(platform->provider_source), old_root, root) != 0 ||
        rebase_path_prefix(platform->fallback_command, sizeof(platform->fallback_command), old_root, root) != 0 ||
        rebase_path_prefix(platform->cpu_helper, sizeof(platform->cpu_helper), old_root, root) != 0 ||
        rebase_path_prefix(platform->tools_manifest, sizeof(platform->tools_manifest), old_root, root) != 0) return -1;
    return fs_copy(platform->data_root, sizeof(platform->data_root), root);
}

static int apply_home(FsPlatform *platform, const char *home, int preserve_source) {
    char old_home[FS_MAX_PATH];
    if (platform == NULL || home == NULL || home[0] == '\0' ||
        fs_copy(old_home, sizeof(old_home), platform->home) != 0) return -1;
    if (rebase_path_prefix(platform->tools_manifest, sizeof(platform->tools_manifest),
                           old_home, home) != 0) return -1;
    if (!preserve_source &&
        rebase_colon_paths(platform->provider_source, sizeof(platform->provider_source),
                           old_home, home) != 0) return -1;
    return fs_copy(platform->home, sizeof(platform->home), home);
}

static int apply_tool_root(FsPlatform *platform, const char *tool_root) {
    char old_root[FS_MAX_PATH];
    if (platform == NULL || tool_root == NULL || tool_root[0] == '\0' ||
        fs_copy(old_root, sizeof(old_root), platform->tool_root) != 0) return -1;
    if (rebase_path_prefix(platform->cpu_helper, sizeof(platform->cpu_helper),
                           old_root, tool_root) != 0) return -1;
    return fs_copy(platform->tool_root, sizeof(platform->tool_root), tool_root);
}

static void print_usage(FILE *stream) {
    fprintf(stream,
            "Usage: forgeshell [options]\n"
            "  --profile PATH       Device profile\n"
            "  --windowed           Run in a desktop window\n"
            "  --fullscreen         Force fullscreen\n"
            "  --resolution WxH     Override physical resolution\n"
            "  --data-root PATH     Sandbox data and ROM paths\n"
            "  --home PATH          Override ForgeShell state directory\n"
            "  --source PATH        Override launcher-provider source\n"
            "  --fake-battery N     Simulator battery percentage\n"
            "  --safe-mode          Disable scan metadata and overrides\n"
            "  --boot               Enable boot recovery handshake\n"
            "  --validate-profile   Validate and print the selected profile\n"
            "  --version            Print version\n");
}

int main(int argc, char **argv) {
    const char *profile_path = getenv("FORGESHELL_PROFILE");
    const char *env_home = getenv("FORGESHELL_HOME");
    const char *env_source = getenv("FORGESHELL_SECTIONS");
    const char *env_data = getenv("FORGE_DATA_ROOT");
    const char *env_rom = getenv("FORGE_ROM_ROOT");
    const char *env_tools = getenv("FORGE_TOOL_ROOT");
    const char *cli_home = NULL;
    const char *cli_source = NULL;
    const char *cli_data = NULL;
    int cli_width = 0;
    int cli_height = 0;
    int cli_fullscreen = -1;
    int cli_fake_battery = -2;
    int boot_mode = getenv("FORGESHELL_BOOT") != NULL;
    int safe_mode = getenv("FORGESHELL_SAFE_MODE") != NULL;
    int validate_only = 0;
    int restored_config = 0;
    int i;
    char error[160];
    char config_path[FS_MAX_PATH];
    char theme_path[FS_MAX_PATH];
    char cache_path[FS_MAX_PATH];
    char favorites_path[FS_MAX_PATH];
    char sessions_path[FS_MAX_PATH];
    char overrides_path[FS_MAX_PATH];
    char metadata_path[FS_MAX_PATH];
    FsPlatform platform;
    FsToolCatalog tools;
    FsConfig config;
    FsTheme theme;
    FsLibrary *library;
    FsFavorites favorites;
    FsSessions sessions;
    FsOverrides overrides;
    FsMetadata metadata;
    int exit_action;
    int cache_present;
    long long startup_started_ms = fs_monotonic_milliseconds();

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--version") == 0) {
            printf("ForgeShell %s\n", FS_VERSION);
            return 0;
        } else if (strcmp(argv[i], "--help") == 0) {
            print_usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--boot") == 0) boot_mode = 1;
        else if (strcmp(argv[i], "--safe-mode") == 0) safe_mode = 1;
        else if (strcmp(argv[i], "--validate-profile") == 0) validate_only = 1;
        else if (strcmp(argv[i], "--windowed") == 0) cli_fullscreen = 0;
        else if (strcmp(argv[i], "--fullscreen") == 0) cli_fullscreen = 1;
        else if (strcmp(argv[i], "--profile") == 0 && i + 1 < argc) profile_path = argv[++i];
        else if (strcmp(argv[i], "--home") == 0 && i + 1 < argc) cli_home = argv[++i];
        else if (strcmp(argv[i], "--source") == 0 && i + 1 < argc) cli_source = argv[++i];
        else if (strcmp(argv[i], "--data-root") == 0 && i + 1 < argc) cli_data = argv[++i];
        else if (strcmp(argv[i], "--resolution") == 0 && i + 1 < argc) {
            if (parse_resolution(argv[++i], &cli_width, &cli_height) != 0) {
                fprintf(stderr, "ForgeShell: invalid resolution\n");
                return 2;
            }
        } else if (strcmp(argv[i], "--fake-battery") == 0 && i + 1 < argc) {
            cli_fake_battery = fs_parse_int(argv[++i], -2, 0, 100);
            if (cli_fake_battery == -2) {
                fprintf(stderr, "ForgeShell: invalid fake battery value\n");
                return 2;
            }
        } else {
            fprintf(stderr, "ForgeShell: unknown or incomplete option: %s\n", argv[i]);
            print_usage(stderr);
            return 2;
        }
    }
    if ((profile_path == NULL || profile_path[0] == '\0') && FORGESHELL_DEFAULT_PROFILE[0] != '\0') {
        profile_path = FORGESHELL_DEFAULT_PROFILE;
    }
    if (fs_platform_load(profile_path, &platform) != 0) {
        perror("ForgeShell platform profile");
        return FS_EXIT_RECOVERY;
    }
    if (env_data != NULL && env_data[0] != '\0' &&
        apply_data_root(&platform, env_data) != 0) return FS_EXIT_RECOVERY;
    if (env_rom != NULL && fs_copy(platform.rom_root, sizeof(platform.rom_root), env_rom) != 0) return FS_EXIT_RECOVERY;
    if (env_tools != NULL && apply_tool_root(&platform, env_tools) != 0) return FS_EXIT_RECOVERY;
    if (env_home != NULL && apply_home(&platform, env_home, env_source != NULL) != 0) return FS_EXIT_RECOVERY;
    if (env_source != NULL && fs_copy(platform.provider_source, sizeof(platform.provider_source), env_source) != 0) return FS_EXIT_RECOVERY;
    if (cli_data != NULL && apply_data_root(&platform, cli_data) != 0) return FS_EXIT_RECOVERY;
    if (cli_home != NULL && apply_home(&platform, cli_home, cli_source != NULL) != 0) return FS_EXIT_RECOVERY;
    if (cli_source != NULL && fs_copy(platform.provider_source, sizeof(platform.provider_source), cli_source) != 0) return FS_EXIT_RECOVERY;
    if (cli_width > 0) { platform.screen_width = cli_width; platform.screen_height = cli_height; }
    if (cli_fullscreen >= 0) platform.fullscreen = cli_fullscreen;
    if (cli_fake_battery >= 0) platform.fake_battery = cli_fake_battery;
    if (platform.fake_battery >= 0) {
        char battery[8];
        int written = snprintf(battery, sizeof(battery), "%d", platform.fake_battery);
        if (written < 0 || (size_t)written >= sizeof(battery) ||
            setenv("FORGESHELL_FAKE_BATTERY", battery, 1) != 0) {
            perror("ForgeShell simulator environment");
            return FS_EXIT_RECOVERY;
        }
    }
    if (fs_platform_validate(&platform, error, sizeof(error)) != 0) {
        fprintf(stderr, "ForgeShell platform invalid: %s\n", error);
        return FS_EXIT_RECOVERY;
    }
    if (validate_only) {
        printf("Profile OK: %s (%s), %dx%d, provider=%s\n",
               platform.device_name, platform.device_id, platform.screen_width,
               platform.screen_height, platform.launcher_provider);
        printf("data_root=%s\nrom_root=%s\nhome=%s\ntool_root=%s\n"
               "source=%s\ncpu_helper=%s\ntools_manifest=%s\n",
               platform.data_root, platform.rom_root, platform.home,
               platform.tool_root, platform.provider_source, platform.cpu_helper,
               platform.tools_manifest);
        return 0;
    }
    if (setenv("FORGESHELL_CPU_HELPER", platform.cpu_helper, 1) != 0 ||
        setenv("FORGESHELL_FRONTEND_VALUE", platform.frontend_value, 1) != 0 ||
        setenv("FORGE_DATA_ROOT", platform.data_root, 1) != 0 ||
        setenv("FORGE_ROM_ROOT", platform.rom_root, 1) != 0 ||
        setenv("FORGE_TOOL_ROOT", platform.tool_root, 1) != 0) {
        perror("ForgeShell runtime environment");
        return FS_EXIT_RECOVERY;
    }
    fs_tools_init(&tools);
    (void)fs_tools_load(&tools, &platform);

    if (fs_mkdir_p(platform.home, 0755) != 0 ||
        make_path(config_path, sizeof(config_path), platform.home, "config.ini") != 0 ||
        make_path(theme_path, sizeof(theme_path), platform.home, "theme.ini") != 0 ||
        make_path(cache_path, sizeof(cache_path), platform.home, "state/library-cache.tsv") != 0 ||
        make_path(favorites_path, sizeof(favorites_path), platform.home, "state/favorites.txt") != 0 ||
        make_path(sessions_path, sizeof(sessions_path), platform.home, "state/sessions.log") != 0 ||
        make_path(overrides_path, sizeof(overrides_path), platform.home, "state/game-overrides.tsv") != 0 ||
        make_path(metadata_path, sizeof(metadata_path), platform.home, "library/metadata.tsv") != 0) {
        perror("ForgeShell setup");
        return FS_EXIT_RECOVERY;
    }
    if (ensure_default_config(config_path) != 0) {
        perror("ForgeShell configuration");
        return FS_EXIT_RECOVERY;
    }
    if (fs_config_load(config_path, &config) != 0) {
        if (fs_config_restore_last_good(config_path) != 0 ||
            fs_config_load(config_path, &config) != 0) {
            perror("ForgeShell configuration");
            return FS_EXIT_RECOVERY;
        }
        restored_config = 1;
        safe_mode = 1;
    }
    if (fs_theme_load(theme_path, &theme) != 0) {
        fprintf(stderr, "ForgeShell theme invalid; using the built-in Midnight Mint theme: %s\n",
                strerror(errno));
        fs_theme_defaults(&theme);
    }
    if (boot_mode && config.safe_mode_next_boot) safe_mode = 1;
    library = (FsLibrary *)calloc(1U, sizeof(*library));
    if (library == NULL) {
        perror("ForgeShell library allocation");
        return FS_EXIT_RECOVERY;
    }
    fs_library_init(library, cache_path);
    if (fs_library_discover_platform(library, &platform) < 0) {
        fprintf(stderr, "ForgeShell: emulator discovery failed for provider %s\n",
                platform.launcher_provider);
    }
    cache_present = fs_file_exists(cache_path);
    if (fs_library_load_cache(library) < 0) library->cache_load_failed = 1;
    fs_favorites_init(&favorites, favorites_path);
    (void)fs_favorites_load(&favorites);
    fs_sessions_init(&sessions, sessions_path);
    (void)fs_sessions_load(&sessions);
    fs_overrides_init(&overrides, overrides_path);
    if (!safe_mode) (void)fs_overrides_load(&overrides);
    fs_metadata_init(&metadata, metadata_path);
    if (!safe_mode && config.metadata_enabled) (void)fs_metadata_load(&metadata);
    fs_library_apply_favorites(library, &favorites);
    if (!safe_mode && config.metadata_enabled) fs_metadata_apply(&metadata, library);
    if (!safe_mode && (config.scan_on_start || !cache_present || library->cache_load_failed)) {
        if (fs_library_start_scan(library) != 0) {
            fprintf(stderr, "ForgeShell: library scan could not allocate memory\n");
        }
    }
    if (restored_config) fprintf(stderr, "ForgeShell: restored last-known-good configuration\n");
    {
        long long finished = fs_monotonic_milliseconds();
        write_startup_metrics(&platform, finished >= startup_started_ms ?
                              finished - startup_started_ms : 0LL,
                              library, cache_present, safe_mode);
    }
    exit_action = fs_ui_run(&platform, &tools, &config, &theme, library, &favorites,
                            &sessions, &overrides, &metadata, boot_mode, safe_mode);
    fs_metadata_close(&metadata);
    fs_library_close(library);
    free(library);
    if (exit_action == FS_EXIT_REBOOT || exit_action == FS_EXIT_POWEROFF) {
        const char *command = exit_action == FS_EXIT_REBOOT ?
                              platform.reboot_command : platform.poweroff_command;
        sync();
        if (fs_platform_run_command(command) != 0) {
            fprintf(stderr, "ForgeShell: power command failed: %s\n", command);
            return FS_EXIT_RECOVERY;
        }
        return 0;
    }
    return exit_action;
}
