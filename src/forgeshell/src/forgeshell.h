#ifndef FORGESHELL_H
#define FORGESHELL_H

#include <dirent.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include <time.h>

#define FS_VERSION "0.6.4-beta"
#define FS_LOGICAL_W 320
#define FS_LOGICAL_H 240
#define FS_MAX_TOOLS 24
#define FS_MAX_PATH 384
#define FS_MAX_TITLE 96
#define FS_MAX_VALUE 512
#define FS_MAX_INPUT_LABEL 12
#define FS_MAX_SYSTEMS 32
#define FS_MAX_GAMES 2048
#define FS_MAX_SESSIONS 64
#define FS_MAX_FAVORITES 128
#define FS_MAX_OVERRIDES 256
#define FS_MAX_METADATA 2048
#define FS_MAX_SEARCH_RESULTS 64
#define FS_SCAN_BUDGET_DEFAULT 12
#define FS_MAX_SCAN_DIRS 128
#define FS_SCAN_SEEN_SLOTS 4096
#define FS_MAX_SCAN_DEPTH 4

#define FS_EXIT_GMENU 0
#define FS_EXIT_RECOVERY 42
#define FS_EXIT_REBOOT 43
#define FS_EXIT_POWEROFF 44


typedef enum {
    FS_ACTION_NONE = 0,
    FS_ACTION_UP,
    FS_ACTION_DOWN,
    FS_ACTION_LEFT,
    FS_ACTION_RIGHT,
    FS_ACTION_ACCEPT,
    FS_ACTION_BACK,
    FS_ACTION_FAVORITE,
    FS_ACTION_OPTIONS,
    FS_ACTION_PAGE_LEFT,
    FS_ACTION_PAGE_RIGHT,
    FS_ACTION_START,
    FS_ACTION_SELECT,
    FS_ACTION_POWER
} FsAction;

typedef struct {
    char profile_path[FS_MAX_PATH];
    char device_id[48];
    char device_name[FS_MAX_TITLE];
    char family[48];
    int screen_width;
    int screen_height;
    int screen_bpp;
    int fullscreen;
    int fake_battery;
    char data_root[FS_MAX_PATH];
    char rom_root[FS_MAX_PATH];
    char home[FS_MAX_PATH];
    char tool_root[FS_MAX_PATH];
    char launcher_provider[48];
    char provider_source[FS_MAX_VALUE];
    char fallback_command[FS_MAX_VALUE];
    char reboot_command[FS_MAX_VALUE];
    char poweroff_command[FS_MAX_VALUE];
    char cpu_helper[FS_MAX_PATH];
    char frontend_value[48];
    char tools_manifest[FS_MAX_PATH];
    int key_up;
    int key_down;
    int key_left;
    int key_right;
    int key_accept;
    int key_back;
    int key_favorite;
    int key_options;
    int key_page_left;
    int key_page_right;
    int key_start;
    int key_select;
    int key_power;
    char label_accept[FS_MAX_INPUT_LABEL];
    char label_back[FS_MAX_INPUT_LABEL];
    char label_favorite[FS_MAX_INPUT_LABEL];
    char label_options[FS_MAX_INPUT_LABEL];
    char label_page_left[FS_MAX_INPUT_LABEL];
    char label_page_right[FS_MAX_INPUT_LABEL];
    char label_start[FS_MAX_INPUT_LABEL];
    char label_select[FS_MAX_INPUT_LABEL];
    char label_power[FS_MAX_INPUT_LABEL];
    int cap_battery;
    int cap_cpu_profiles;
    int cap_brightness;
    int cap_volume;
    int cap_safe_shutdown;
    int cap_storage_health;
    int cap_system_info;
} FsPlatform;

typedef struct {
    char id[48];
    char title[FS_MAX_TITLE];
    char meta[FS_MAX_TITLE];
    char command[FS_MAX_VALUE];
    char requires[48];
} FsToolEntry;

typedef struct {
    FsToolEntry items[FS_MAX_TOOLS];
    size_t count;
    int load_failed;
} FsToolCatalog;

typedef struct {
    char launcher_mode[16];
    int scan_on_start;
    int large_text;
    int high_contrast;
    int scan_budget;
    int onboarding_complete;
    int safe_mode_next_boot;
    int metadata_enabled;
    int show_recovery_hint;
} FsConfig;

typedef struct {
    uint32_t background;
    uint32_t panel;
    uint32_t panel_alt;
    uint32_t accent;
    uint32_t accent_soft;
    uint32_t text;
    uint32_t muted;
    uint32_t danger;
    uint32_t border;
    int radius;
    int font_small;
    int font_body;
    int font_title;
    char font_regular[FS_MAX_PATH];
    char font_bold[FS_MAX_PATH];
} FsTheme;

typedef struct {
    char id[48];
    char title[FS_MAX_TITLE];
    char exec_path[FS_MAX_PATH];
    char params[FS_MAX_VALUE];
    char workdir[FS_MAX_PATH];
    char romdir[FS_MAX_PATH];
    char romexts[FS_MAX_VALUE];
    char source_path[FS_MAX_PATH];
} FsSystem;

typedef struct {
    char title[FS_MAX_TITLE];
    char path[FS_MAX_PATH];
    int system_index;
    off_t size;
    time_t mtime;
    int favorite;
    char art_path[FS_MAX_PATH];
    int metadata_applied;
} FsGame;

typedef struct {
    char path[FS_MAX_PATH];
    char title[FS_MAX_TITLE];
    char system_title[FS_MAX_TITLE];
    time_t started_at;
    unsigned duration_seconds;
    int exit_status;
} FsSession;

typedef struct {
    FsSystem systems[FS_MAX_SYSTEMS];
    size_t system_count;
    FsGame games[FS_MAX_GAMES];
    size_t game_count;
    FsGame *staged_games;
    size_t staged_count;
    uint16_t *scan_seen;
    DIR *scan_dir;
    char scan_dirs[FS_MAX_SCAN_DIRS][FS_MAX_PATH];
    unsigned char scan_dir_system[FS_MAX_SCAN_DIRS];
    unsigned char scan_dir_depth[FS_MAX_SCAN_DIRS];
    size_t scan_head;
    size_t scan_tail;
    int scan_current_system;
    int scan_current_depth;
    char scan_current_path[FS_MAX_PATH];
    int scan_active;
    int scan_complete;
    int scan_truncated;
    int scan_start_failed;
    int cache_load_failed;
    int cache_save_failed;
    long long scan_started_ms;
    long long last_scan_ms;
    char cache_path[FS_MAX_PATH];
} FsLibrary;


typedef struct {
    char path[FS_MAX_PATH];
    char emulator_id[48];
    char cpu_profile[16];
    char aspect[16];
    char scaling[16];
    char bios_path[FS_MAX_PATH];
    int frameskip;
} FsGameOverride;

typedef struct {
    FsGameOverride items[FS_MAX_OVERRIDES];
    size_t count;
    char path[FS_MAX_PATH];
    int load_failed;
} FsOverrides;

typedef struct {
    char path[FS_MAX_PATH];
    char title[FS_MAX_TITLE];
    char art_path[FS_MAX_PATH];
    size_t source_order;
} FsMetadataEntry;

typedef struct {
    FsMetadataEntry *items;
    size_t count;
    size_t capacity;
    char path[FS_MAX_PATH];
    int load_failed;
} FsMetadata;

typedef struct {
    FsSession items[FS_MAX_SESSIONS];
    size_t count;
    char path[FS_MAX_PATH];
    int load_failed;
} FsSessions;

typedef struct {
    char paths[FS_MAX_FAVORITES][FS_MAX_PATH];
    size_t count;
    char path[FS_MAX_PATH];
    int load_failed;
} FsFavorites;

/* platform.c */
void fs_platform_defaults(FsPlatform *platform);
int fs_platform_load(const char *path, FsPlatform *platform);
int fs_platform_apply_override(FsPlatform *platform, const char *key, const char *value);
FsAction fs_platform_translate_key(const FsPlatform *platform, int key);
const char *fs_platform_action_label(const FsPlatform *platform, FsAction action);
int fs_platform_actions_share_key(const FsPlatform *platform, FsAction left, FsAction right);
int fs_platform_has_capability(const FsPlatform *platform, const char *capability);
int fs_platform_run_command(const char *command);
int fs_platform_validate(const FsPlatform *platform, char *error, size_t error_size);
int fs_platform_compute_viewport(int screen_width, int screen_height,
                                 int logical_width, int logical_height,
                                 int *x, int *y, int *width, int *height);

/* tools.c */
void fs_tools_init(FsToolCatalog *catalog);
int fs_tools_load(FsToolCatalog *catalog, const FsPlatform *platform);

/* util.c */
char *fs_trim(char *text);
int fs_copy(char *dst, size_t dst_size, const char *src);
int fs_path_join(char *dst, size_t dst_size, const char *left, const char *right);
int fs_mkdir_p(const char *path, mode_t mode);
int fs_file_exists(const char *path);
int fs_dir_exists(const char *path);
int fs_write_atomic(const char *path, const void *data, size_t size, mode_t mode);
int fs_read_text(const char *path, char **out, size_t max_size);
int fs_parse_bool(const char *value, int fallback);
int fs_parse_int(const char *value, int fallback, int min_value, int max_value);
uint32_t fs_parse_color(const char *value, uint32_t fallback);
int fs_casecmp(const char *a, const char *b);
int fs_case_contains(const char *haystack, const char *needle);
void fs_title_from_path(const char *path, char *title, size_t title_size);
int fs_shell_quote(const char *text, char *out, size_t out_size);
int fs_replace_all(const char *src, const char *needle, const char *replacement,
                   char *out, size_t out_size);
long long fs_monotonic_seconds(void);
long long fs_monotonic_milliseconds(void);

/* config.c */
void fs_config_defaults(FsConfig *config);
int fs_config_load(const char *path, FsConfig *config);
int fs_config_set(const char *path, const char *key, const char *value);
int fs_config_set_many(const char *path, const char *const *keys,
                       const char *const *values, size_t count);
int fs_config_save_last_good(const char *path);
int fs_config_restore_last_good(const char *path);
void fs_theme_defaults(FsTheme *theme);
int fs_theme_load(const char *path, FsTheme *theme);

/* library.c */
void fs_library_init(FsLibrary *library, const char *cache_path);
void fs_library_close(FsLibrary *library);
int fs_library_discover(FsLibrary *library, const char *section_dirs);
int fs_library_discover_platform(FsLibrary *library, const FsPlatform *platform);
int fs_library_load_cache(FsLibrary *library);
int fs_library_save_cache(const FsLibrary *library);
int fs_library_start_scan(FsLibrary *library);
int fs_library_scan_step(FsLibrary *library, size_t budget);
void fs_library_apply_favorites(FsLibrary *library, const FsFavorites *favorites);
size_t fs_library_search(const FsLibrary *library, const char *query,
                         size_t *indices, size_t max_indices);
ssize_t fs_library_find_path(const FsLibrary *library, const char *path);
int fs_library_system_supports_path(const FsSystem *system, const char *path);
int fs_library_systems_compatible(const FsSystem *source, const FsSystem *candidate,
                                  const char *path);

/* session.c */
void fs_favorites_init(FsFavorites *favorites, const char *path);
int fs_favorites_load(FsFavorites *favorites);
int fs_favorites_contains(const FsFavorites *favorites, const char *path);
int fs_favorites_toggle(FsFavorites *favorites, const char *path);
void fs_sessions_init(FsSessions *sessions, const char *path);
int fs_sessions_load(FsSessions *sessions);
int fs_sessions_append(FsSessions *sessions, const FsSession *session);

/* overrides.c */
void fs_overrides_init(FsOverrides *overrides, const char *path);
int fs_overrides_load(FsOverrides *overrides);
const FsGameOverride *fs_overrides_find(const FsOverrides *overrides, const char *path);
FsGameOverride *fs_overrides_get(FsOverrides *overrides, const char *path, int create);
int fs_overrides_save(const FsOverrides *overrides);
int fs_overrides_reset(FsOverrides *overrides, const char *path);

/* metadata.c */
void fs_metadata_init(FsMetadata *metadata, const char *path);
int fs_metadata_load(FsMetadata *metadata);
void fs_metadata_close(FsMetadata *metadata);
void fs_metadata_apply(const FsMetadata *metadata, FsLibrary *library);

/* runner.c */
int fs_runner_build_command(const FsSystem *system, const FsGame *game,
                            char *command, size_t command_size);
int fs_runner_launch(const FsSystem *system, const FsGame *game,
                     FsSession *session);
int fs_runner_launch_override(const FsSystem *system, const FsGame *game,
                              const FsGameOverride *override, FsSession *session);

/* ui.c */
int fs_ui_run(const FsPlatform *platform, FsToolCatalog *tools, FsConfig *config, FsTheme *theme,
              FsLibrary *library, FsFavorites *favorites, FsSessions *sessions,
              FsOverrides *overrides, FsMetadata *metadata, int boot_mode, int safe_mode);

#endif
