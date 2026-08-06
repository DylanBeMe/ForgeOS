#define _POSIX_C_SOURCE 200809L
#include "forgeshell.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

typedef struct {
    const char *token;
    const char *replacement;
} FsParamReplacement;

static int fs_append_text(char *out, size_t out_size, size_t *used, const char *text) {
    size_t length;
    if (out == NULL || used == NULL || text == NULL) {
        errno = EINVAL;
        return -1;
    }
    length = strlen(text);
    if (*used + length >= out_size) {
        errno = ENOSPC;
        return -1;
    }
    memcpy(out + *used, text, length);
    *used += length;
    out[*used] = '\0';
    return 0;
}

static int fs_append_char(char *out, size_t out_size, size_t *used, char value) {
    if (out == NULL || used == NULL || *used + 1U >= out_size) {
        errno = out == NULL || used == NULL ? EINVAL : ENOSPC;
        return -1;
    }
    out[*used] = value;
    (*used)++;
    out[*used] = '\0';
    return 0;
}

static int fs_expand_params(const char *params,
                            const FsParamReplacement *replacements,
                            size_t replacement_count,
                            char *out, size_t out_size) {
    size_t used = 0U;
    size_t i = 0U;
    char quote = '\0';
    if (params == NULL || replacements == NULL || out == NULL || out_size == 0U) {
        errno = EINVAL;
        return -1;
    }
    out[0] = '\0';
    while (params[i] != '\0') {
        size_t replacement_index;
        int matched = 0;
        for (replacement_index = 0U; replacement_index < replacement_count;
             replacement_index++) {
            size_t token_length = strlen(replacements[replacement_index].token);
            if (token_length > 0U &&
                strncmp(params + i, replacements[replacement_index].token,
                        token_length) == 0) {
                char reopen[2] = { quote, '\0' };
                if (quote != '\0' && fs_append_text(out, out_size, &used, reopen) != 0) {
                    return -1;
                }
                if (fs_append_text(out, out_size, &used,
                                   replacements[replacement_index].replacement) != 0) {
                    return -1;
                }
                if (quote != '\0' && fs_append_text(out, out_size, &used, reopen) != 0) {
                    return -1;
                }
                i += token_length;
                matched = 1;
                break;
            }
        }
        if (matched) {
            continue;
        }
        if (fs_append_char(out, out_size, &used, params[i]) != 0) {
            return -1;
        }
        if (params[i] == '\\' && quote != '\'' && params[i + 1U] != '\0') {
            i++;
            if (fs_append_char(out, out_size, &used, params[i]) != 0) {
                return -1;
            }
        } else if (params[i] == '\'' && quote != '"') {
            quote = quote == '\'' ? '\0' : '\'';
        } else if (params[i] == '"' && quote != '\'') {
            quote = quote == '"' ? '\0' : '"';
        }
        i++;
    }
    if (quote != '\0') {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int fs_runner_build_command(const FsSystem *system, const FsGame *game,
                            char *command, size_t command_size) {
    char quoted_exec[(FS_MAX_PATH * 4) + 3];
    char quoted_full[(FS_MAX_PATH * 4) + 3];
    char quoted_dir[(FS_MAX_PATH * 4) + 3];
    char quoted_file[(FS_MAX_PATH * 4) + 3];
    char quoted_ext[(FS_MAX_PATH * 4) + 3];
    char directory[FS_MAX_PATH];
    char filename[FS_MAX_PATH];
    char extension[FS_MAX_PATH];
    char expanded[4096];
    const char *base;
    const char *dot;
    char *slash;
    FsParamReplacement replacements[8];
    int written;
    if (system == NULL || game == NULL || command == NULL || command_size == 0U) {
        errno = EINVAL;
        return -1;
    }
    base = strrchr(game->path, '/');
    base = base == NULL ? game->path : base + 1;
    if (fs_copy(directory, sizeof(directory), game->path) != 0) {
        return -1;
    }
    slash = strrchr(directory, '/');
    if (slash == NULL) {
        (void)fs_copy(directory, sizeof(directory), "./");
    } else {
        slash[1] = '\0';
    }
    dot = strrchr(base, '.');
    if (dot == NULL || dot == base) {
        if (fs_copy(filename, sizeof(filename), base) != 0 ||
            fs_copy(extension, sizeof(extension), "") != 0) {
            return -1;
        }
    } else {
        size_t stem_length = (size_t)(dot - base);
        if (stem_length >= sizeof(filename) || fs_copy(extension, sizeof(extension), dot) != 0) {
            errno = ENAMETOOLONG;
            return -1;
        }
        memcpy(filename, base, stem_length);
        filename[stem_length] = '\0';
    }
    if (fs_shell_quote(system->exec_path, quoted_exec, sizeof(quoted_exec)) != 0 ||
        fs_shell_quote(game->path, quoted_full, sizeof(quoted_full)) != 0 ||
        fs_shell_quote(directory, quoted_dir, sizeof(quoted_dir)) != 0 ||
        fs_shell_quote(filename, quoted_file, sizeof(quoted_file)) != 0 ||
        fs_shell_quote(extension, quoted_ext, sizeof(quoted_ext)) != 0) {
        return -1;
    }
    replacements[0] = (FsParamReplacement){ "[selFullPath]", quoted_full };
    replacements[1] = (FsParamReplacement){ "[selPath]", quoted_dir };
    replacements[2] = (FsParamReplacement){ "[selFile]", quoted_file };
    replacements[3] = (FsParamReplacement){ "[selExt]", quoted_ext };
    replacements[4] = (FsParamReplacement){ "[rom]", quoted_full };
    replacements[5] = (FsParamReplacement){ "[ROM]", quoted_full };
    replacements[6] = (FsParamReplacement){ "%ROM%", quoted_full };
    replacements[7] = (FsParamReplacement){ "%f", quoted_full };
    if (fs_expand_params(system->params, replacements,
                         sizeof(replacements) / sizeof(replacements[0]),
                         expanded, sizeof(expanded)) != 0) {
        return -1;
    }
    if (system->params[0] == '\0') {
        written = snprintf(command, command_size, "%s %s", quoted_exec, quoted_full);
    } else {
        written = snprintf(command, command_size, "%s %s", quoted_exec, expanded);
    }
    if (written < 0 || (size_t)written >= command_size) {
        errno = ENOSPC;
        return -1;
    }
    return 0;
}

static const char *fs_runner_profile_helper_path(void) {
    const char *helper = getenv("FORGESHELL_CPU_HELPER");
    return helper != NULL && helper[0] != '\0' && access(helper, X_OK) == 0 ? helper : NULL;
}

static int fs_runner_profile_helper(const char *action, const char *profile,
                                    const char *state_path) {
    const char *helper = fs_runner_profile_helper_path();
    pid_t child;
    int status = 0;
    if (helper == NULL) return 1;
    child = fork();
    if (child < 0) return -1;
    if (child == 0) {
        if (strcmp(action, "apply") == 0) {
            execl(helper, helper, "apply", profile, state_path, (char *)NULL);
        } else {
            execl(helper, helper, "restore", state_path, (char *)NULL);
        }
        _exit(127);
    }
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) return -1;
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int fs_runner_set_override_env(const FsGameOverride *override) {
    char frameskip[16];
    if (override == NULL) return 0;
    if (setenv("FORGESHELL_CPU_PROFILE", override->cpu_profile, 1) != 0 ||
        setenv("FORGESHELL_ASPECT", override->aspect, 1) != 0 ||
        setenv("FORGESHELL_SCALING", override->scaling, 1) != 0 ||
        setenv("FORGESHELL_BIOS", override->bios_path, 1) != 0) return -1;
    if (snprintf(frameskip, sizeof(frameskip), "%d", override->frameskip) >=
        (int)sizeof(frameskip)) return -1;
    return setenv("FORGESHELL_FRAMESKIP", frameskip, 1);
}

int fs_runner_launch_override(const FsSystem *system, const FsGame *game,
                              const FsGameOverride *override, FsSession *session) {
    char command[4096];
    char profile_state[FS_MAX_PATH] = "";
    pid_t child;
    int status = 0;
    int profile_applied = 0;
    int profile_requested = 0;
    int profile_fd;
    long long started_mono;
    long long finished_mono;
    if (system == NULL || game == NULL || session == NULL) {
        errno = EINVAL;
        return -1;
    }
    memset(session, 0, sizeof(*session));
    session->started_at = time(NULL);
    (void)fs_copy(session->path, sizeof(session->path), game->path);
    (void)fs_copy(session->title, sizeof(session->title), game->title);
    (void)fs_copy(session->system_title, sizeof(session->system_title), system->title);
    if (fs_runner_build_command(system, game, command, sizeof(command)) != 0) {
        session->exit_status = 127;
        return session->exit_status;
    }
    profile_requested = override != NULL && override->cpu_profile[0] != '\0' &&
                        fs_casecmp(override->cpu_profile, "default") != 0;
    if (profile_requested && fs_runner_profile_helper_path() != NULL) {
        const char *temp_root = getenv("TMPDIR");
        int written;
        if (temp_root == NULL || temp_root[0] == '\0') temp_root = "/tmp";
        written = snprintf(profile_state, sizeof(profile_state),
                           "%s/forgeshell-cpu-XXXXXX", temp_root);
        if (written < 0 || (size_t)written >= sizeof(profile_state)) {
            session->exit_status = 125;
            return session->exit_status;
        }
        profile_fd = mkstemp(profile_state);
        if (profile_fd < 0) {
            session->exit_status = 125;
            return session->exit_status;
        }
        if (close(profile_fd) != 0 || unlink(profile_state) != 0) {
            (void)unlink(profile_state);
            session->exit_status = 125;
            return session->exit_status;
        }
        profile_applied = fs_runner_profile_helper("apply", override->cpu_profile,
                                                   profile_state) == 0;
        if (!profile_applied) (void)unlink(profile_state);
    }
    started_mono = fs_monotonic_seconds();
    child = fork();
    if (child < 0) {
        if (profile_applied && fs_runner_profile_helper("restore", "", profile_state) == 0) {
            (void)unlink(profile_state);
        }
        session->exit_status = 125;
        return session->exit_status;
    }
    if (child == 0) {
        const char *frontend = getenv("FORGESHELL_FRONTEND_VALUE");
        if (frontend == NULL || frontend[0] == '\0') frontend = "gmenu2x";
        if (setenv("FRONTEND", frontend, 1) != 0 ||
            fs_runner_set_override_env(override) != 0) {
            _exit(126);
        }
        if (system->workdir[0] != '\0' && chdir(system->workdir) != 0) {
            _exit(126);
        }
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }
    for (;;) {
        pid_t result = waitpid(child, &status, 0);
        if (result == child) break;
        if (result < 0 && errno == EINTR) continue;
        if (result < 0) {
            if (profile_applied && fs_runner_profile_helper("restore", "", profile_state) == 0) {
                (void)unlink(profile_state);
            }
            session->exit_status = 125;
            return session->exit_status;
        }
    }
    if (profile_applied) {
        int restore_status = fs_runner_profile_helper("restore", "", profile_state);
        if (restore_status != 0) {
            fprintf(stderr, "ForgeShell: CPU profile restore failed; state retained at %s\n",
                    profile_state);
        } else {
            (void)unlink(profile_state);
        }
    } else if (profile_state[0] != '\0') {
        (void)unlink(profile_state);
    }
    finished_mono = fs_monotonic_seconds();
    if (finished_mono >= started_mono) {
        long long duration = finished_mono - started_mono;
        session->duration_seconds = duration > 31536000LL ? 31536000U : (unsigned)duration;
    }
    if (WIFEXITED(status)) session->exit_status = WEXITSTATUS(status);
    else if (WIFSIGNALED(status)) session->exit_status = 128 + WTERMSIG(status);
    else session->exit_status = 255;
    return session->exit_status;
}

int fs_runner_launch(const FsSystem *system, const FsGame *game,
                     FsSession *session) {
    return fs_runner_launch_override(system, game, NULL, session);
}
