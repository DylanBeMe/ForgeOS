#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"
#include <SDL/SDL.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHECK(expr) do { \
    if (!(expr)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s (errno=%d)\n", \
                __FILE__, __LINE__, #expr, errno); \
        return 1; \
    } \
} while (0)

static int write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "w");
    if (file == NULL) return -1;
    if (fputs(text, file) == EOF || fclose(file) != 0) return -1;
    return 0;
}

int main(void) {
    char root[] = "/tmp/forgeshell-platform-test-XXXXXX";
    char profile[FS_MAX_PATH];
    char systems[FS_MAX_PATH];
    char tools[FS_MAX_PATH];
    char roms[FS_MAX_PATH];
    char profile_text[4096];
    char systems_text[2048];
    char tools_text[2048];
    char error[160];
    FsPlatform platform;
    FsToolCatalog catalog;
    FsLibrary library;
    int x;
    int y;
    int width;
    int height;

    fs_platform_defaults(&platform);
    CHECK(platform.key_accept == SDLK_RETURN &&
          strcmp(fs_platform_action_label(&platform, FS_ACTION_ACCEPT), "ENTER") == 0);
    CHECK(platform.key_back == SDLK_ESCAPE &&
          strcmp(fs_platform_action_label(&platform, FS_ACTION_BACK), "ESC") == 0);
    CHECK(platform.key_favorite == SDLK_SPACE &&
          strcmp(fs_platform_action_label(&platform, FS_ACTION_FAVORITE), "SPACE") == 0);

    CHECK(mkdtemp(root) != NULL);
    CHECK(fs_path_join(profile, sizeof(profile), root, "device.ini") == 0);
    CHECK(fs_path_join(systems, sizeof(systems), root, "systems.ini") == 0);
    CHECK(fs_path_join(tools, sizeof(tools), root, "tools.tsv") == 0);
    CHECK(fs_path_join(roms, sizeof(roms), root, "roms") == 0);
    CHECK(fs_mkdir_p(roms, 0755) == 0);

    CHECK(snprintf(profile_text, sizeof(profile_text),
        "[device]\nid=test-device\nname=Test Device\nfamily=linux-sdl\n"
        "[ui]\nscreen_width=480\nscreen_height=272\nscreen_bpp=32\nfullscreen=0\n"
        "[storage]\ndata_root=%s\nrom_root=${data_root}/roms\nhome=${data_root}/home\ntool_root=${data_root}/tools\n"
        "[launcher]\nprovider=forge-manifest\nsource=%s\nfallback_command=/bin/true\nfrontend_value=forgeshell\n"
        "[maintenance]\nmanifest=%s\n[performance]\ncpu_helper=\n"
        "[power]\nreboot=/bin/true\npoweroff=/bin/true\n"
        "[input]\nup=UP\ndown=DOWN\nleft=LEFT\nright=RIGHT\naccept=RETURN\nback=ESCAPE\n"
        "favorite=SPACE\noptions=RCTRL\npage_left=TAB\npage_right=BACKSPACE\nstart=LALT\nselect=LCTRL\npower=LSHIFT\n"
        "[labels]\naccept=ENTER\nback=ESC\nfavorite=SPACE\noptions=RCTRL\n"
        "page_left=TAB\npage_right=BKSP\nstart=ALT\nselect=CTRL\npower=SHIFT\n"
        "[capabilities]\nbattery=0\ncpu_profiles=0\nbrightness=0\nvolume=0\nsafe_shutdown=0\nstorage_health=1\nsystem_info=1\n",
        root, systems, tools) < (int)sizeof(profile_text));
    CHECK(write_text(profile, profile_text) == 0);

    CHECK(snprintf(systems_text, sizeof(systems_text),
        "[system.demo]\ntitle=Demo\nexec=/bin/echo\nparams=Run [rom]\n"
        "workdir=%s\nromdir=${rom_root}\nromexts=.rom,.zip\n",
        root) < (int)sizeof(systems_text));
    CHECK(write_text(systems, systems_text) == 0);
    CHECK(snprintf(tools_text, sizeof(tools_text),
        "info\tSystem Overview\tHost\t/bin/true\tsystem_info\n"
        "cpu\tCPU Profile\tSpeed\t/bin/false\tcpu_profiles\n") < (int)sizeof(tools_text));
    CHECK(write_text(tools, tools_text) == 0);

    CHECK(fs_platform_load(profile, &platform) == 0);
    CHECK(fs_platform_validate(&platform, error, sizeof(error)) == 0);
    CHECK(strcmp(platform.device_id, "test-device") == 0);
    CHECK(strcmp(platform.rom_root, roms) == 0);
    CHECK(platform.screen_width == 480 && platform.screen_height == 272);
    CHECK(fs_platform_translate_key(&platform, platform.key_accept) == FS_ACTION_ACCEPT);
    CHECK(strcmp(fs_platform_action_label(&platform, FS_ACTION_ACCEPT), "ENTER") == 0);
    CHECK(!fs_platform_actions_share_key(&platform, FS_ACTION_START, FS_ACTION_OPTIONS));
    CHECK(fs_platform_has_capability(&platform, "system_info") == 1);
    CHECK(fs_platform_has_capability(&platform, "cpu_profiles") == 0);
    CHECK(fs_platform_has_capability(NULL, "always") == 1);
    CHECK(fs_platform_has_capability(NULL, "system_info") == 0);
    CHECK(fs_platform_has_capability(&platform, NULL) == 0);

    CHECK(fs_platform_compute_viewport(480, 272, 320, 240,
                                       &x, &y, &width, &height) == 0);
    CHECK(x == 59 && y == 0 && width == 362 && height == 272);
    CHECK(fs_platform_compute_viewport(640, 360, 320, 240,
                                       &x, &y, &width, &height) == 0);
    CHECK(x == 80 && y == 0 && width == 480 && height == 360);

    fs_tools_init(&catalog);
    CHECK(fs_tools_load(&catalog, &platform) == 0);
    CHECK(catalog.count == 1U);
    CHECK(strcmp(catalog.items[0].id, "info") == 0);

    fs_library_init(&library, NULL);
    CHECK(fs_library_discover_platform(&library, &platform) == 1);
    CHECK(library.system_count == 1U);
    CHECK(strcmp(library.systems[0].id, "demo") == 0);
    CHECK(strcmp(library.systems[0].romdir, roms) == 0);
    fs_library_close(&library);

    CHECK(strcat(systems_text,
        "\n[system.demo]\ntitle=Duplicate\nexec=/bin/true\nromdir=${rom_root}\n") != NULL);
    CHECK(write_text(systems, systems_text) == 0);
    fs_library_init(&library, NULL);
    CHECK(fs_library_discover_platform(&library, &platform) < 0);
    CHECK(errno == EEXIST);
    fs_library_close(&library);
    CHECK(write_text(systems,
        "[system.demo]\ntitle=Demo\nexec=/bin/echo\nparams=Run [rom]\n"
        "workdir=/tmp\nromdir=${rom_root}\nromexts=.rom,.zip\n") == 0);
    CHECK(write_text(systems,
        "[system.demo]\ntitle=Demo\nexec=/bin/echo\nromdir=${rom_root}\nunknown=value\n") == 0);
    fs_library_init(&library, NULL);
    CHECK(fs_library_discover_platform(&library, &platform) < 0 && errno == EINVAL);
    fs_library_close(&library);
    CHECK(write_text(systems,
        "[system.demo]\ntitle=Demo\nexec=/bin/echo\nromdir=${rom_root}\nselectordir=${rom_root}\n") == 0);
    fs_library_init(&library, NULL);
    CHECK(fs_library_discover_platform(&library, &platform) < 0 && errno == EEXIST);
    fs_library_close(&library);
    CHECK(write_text(systems,
        "[system.demo]\ntitle=Demo\nexec=/bin/echo\nromdir=${missing_root}\n") == 0);
    fs_library_init(&library, NULL);
    CHECK(fs_library_discover_platform(&library, &platform) < 0 && errno == EINVAL);
    fs_library_close(&library);
    CHECK(write_text(systems,
        "[system.demo]\ntitle=Demo\nexec=/bin/echo\nparams=Run [rom]\n"
        "workdir=/tmp\nromdir=${rom_root}\nromexts=.rom,.zip\n") == 0);

    {
        char *accept = strstr(profile_text, "accept=RETURN");
        CHECK(accept != NULL);
        memcpy(accept, "accept=BADKEY", strlen("accept=BADKEY"));
        accept[strlen("accept=BADKEY")] = '\n';
        CHECK(write_text(profile, profile_text) == 0);
        CHECK(fs_platform_load(profile, &platform) < 0);
    }

    CHECK(write_text(profile,
        "[device]\nid=test-device\nname=Test Device\nfamily=linux-sdl\n"
        "[ui]\nscreen_width=480\nscreen_height=272\nscreen_bpp=32\nfullscreen=0\n"
        "[storage]\ndata_root=/tmp\nrom_root=/tmp/roms\nhome=/tmp/home\ntool_root=/tmp/tools\n"
        "[launcher]\nprovider=forge-manifest\nsource=/tmp/systems.ini\nfallback_command=/bin/true\nfrontend_value=forgeshell\n"
        "[maintenance]\nmanifest=/tmp/tools.tsv\n[performance]\ncpu_helper=\n"
        "[power]\nreboot=/bin/true\npoweroff=/bin/true\n"
        "[input]\nup=UP\ndown=DOWN\nleft=LEFT\nright=RIGHT\naccept=RETURN\nback=ESCAPE\n"
        "favorite=SPACE\noptions=RCTRL\npage_left=TAB\npage_right=BACKSPACE\nstart=LALT\nselect=LCTRL\npower=LSHIFT\n"
        "[labels]\naccept=ENTER\nback=ESC\nfavorite=SPACE\noptions=RCTRL\n"
        "page_left=TAB\npage_right=BKSP\nstart=ALT\nselect=CTRL\npower=SHIFT\n"
        "[capabilities]\nbattery=0\ncpu_profiles=0\nbrightness=0\nvolume=0\nsafe_shutdown=0\nstorage_health=1\nsystem_info=1\n") == 0);
    CHECK(fs_platform_load(profile, &platform) == 0);
    CHECK(fs_platform_apply_override(&platform, "ui.fullscreen", "perhaps") < 0 &&
          errno == EINVAL);
    CHECK(fs_platform_apply_override(&platform, "ui.screen_bpp", "20") < 0 &&
          errno == EINVAL);
    CHECK(fs_platform_apply_override(&platform, "capabilities.battery", "sometimes") < 0 &&
          errno == EINVAL);
    platform.key_down = platform.key_up;
    CHECK(fs_platform_validate(&platform, error, sizeof(error)) != 0);
    CHECK(strstr(error, "share") != NULL);
    CHECK(fs_platform_load(profile, &platform) == 0);
    platform.key_start = platform.key_options;
    CHECK(fs_platform_validate(&platform, error, sizeof(error)) == 0);
    CHECK(fs_platform_actions_share_key(&platform, FS_ACTION_START, FS_ACTION_OPTIONS));

    CHECK(write_text(profile, "[device]\nid=incomplete\nname=Incomplete\nfamily=linux-sdl\n") == 0);
    CHECK(fs_platform_load(profile, &platform) < 0 && errno == EINVAL);
    CHECK(write_text(profile, "[unknown]\nvalue=1\n") == 0);
    CHECK(fs_platform_load(profile, &platform) < 0);
    CHECK(write_text(profile, "[device\nid=test\n") == 0);
    CHECK(fs_platform_load(profile, &platform) < 0);
    CHECK(write_text(profile, "[device]\nthis is not an assignment\n") == 0);
    CHECK(fs_platform_load(profile, &platform) < 0);
    CHECK(write_text(profile, "[device]\nid=test\nunknown=value\n") == 0);
    CHECK(fs_platform_load(profile, &platform) < 0);
    CHECK(write_text(profile, "[device] trailing\nid=test\n") == 0);
    CHECK(fs_platform_load(profile, &platform) < 0);
    CHECK(write_text(profile, "[device]\nid=test\nid=duplicate\n") == 0);
    CHECK(fs_platform_load(profile, &platform) < 0 && errno == EEXIST);
    {
        FILE *long_profile = fopen(profile, "w");
        size_t long_index;
        CHECK(long_profile != NULL);
        CHECK(fputs("[device]\nname=", long_profile) != EOF);
        for (long_index = 0U; long_index < 2048U; long_index++) CHECK(fputc('A', long_profile) != EOF);
        CHECK(fputc('\n', long_profile) != EOF);
        CHECK(fclose(long_profile) == 0);
        CHECK(fs_platform_load(profile, &platform) < 0);
    }

    CHECK(fs_copy(platform.tools_manifest, sizeof(platform.tools_manifest), tools) == 0);
    CHECK(write_text(tools,
        "dup\tOne\tFirst\t/bin/true\talways\n"
        "dup\tTwo\tSecond\t/bin/true\talways\n") == 0);
    fs_tools_init(&catalog);
    CHECK(fs_tools_load(&catalog, &platform) < 0);
    CHECK(catalog.load_failed);
    CHECK(write_text(tools,
        "hidden\tOne\tFirst\t/bin/true\tcpu_profiles\n"
        "hidden\tTwo\tSecond\t/bin/true\talways\n") == 0);
    fs_tools_init(&catalog);
    CHECK(fs_tools_load(&catalog, &platform) < 0 && errno == EEXIST);
    CHECK(catalog.load_failed);
    CHECK(write_text(tools,
        "bad id\tBad\tInvalid\t/bin/true\talways\n") == 0);
    fs_tools_init(&catalog);
    CHECK(fs_tools_load(&catalog, &platform) < 0 && errno == EINVAL);
    CHECK(catalog.load_failed);
    CHECK(write_text(tools,
        "badcap\tBad\tUnknown\t/bin/true\tnot_a_capability\n") == 0);
    fs_tools_init(&catalog);
    CHECK(fs_tools_load(&catalog, &platform) < 0 && errno == EINVAL);
    CHECK(catalog.load_failed);
    CHECK(write_text(tools,
        "badvar\tBad\tUnknown\t${unsupported}/tool\talways\n") == 0);
    fs_tools_init(&catalog);
    CHECK(fs_tools_load(&catalog, &platform) < 0 && errno == EINVAL);
    CHECK(catalog.load_failed);
    {
        FILE *long_tools = fopen(tools, "w");
        size_t long_index;
        CHECK(long_tools != NULL);
        CHECK(fputs("tool\tTitle\tMeta\t", long_tools) != EOF);
        for (long_index = 0U; long_index < 2048U; long_index++) CHECK(fputc('A', long_tools) != EOF);
        CHECK(fputs("\talways\n", long_tools) != EOF);
        CHECK(fclose(long_tools) == 0);
        fs_tools_init(&catalog);
        CHECK(fs_tools_load(&catalog, &platform) < 0);
        CHECK(catalog.load_failed);
    }

    CHECK(unlink(profile) == 0);
    CHECK(unlink(systems) == 0);
    CHECK(unlink(tools) == 0);
    CHECK(rmdir(roms) == 0);
    {
        char home[FS_MAX_PATH];
        CHECK(fs_path_join(home, sizeof(home), root, "home") == 0);
        (void)rmdir(home);
    }
    CHECK(rmdir(root) == 0);
    return 0;
}
