/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char *run_command_capture(const char *repository_path, const char *command) {
    char buffer[4096];
    ca_string_builder out;
    int pipefd[2];
    pid_t pid;
    ssize_t n;

    if (command == NULL || pipe(pipefd) != 0) {
        return ca_strdup("");
    }
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return ca_strdup("");
    }
    if (pid == 0) {
        close(pipefd[0]);
        if (repository_path != NULL && repository_path[0] != '\0' && chdir(repository_path) != 0) {
            _exit(127);
        }
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execl("/bin/sh", "sh", "-c", command, (char *)NULL);
        _exit(127);
    }

    close(pipefd[1]);
    ca_sb_init(&out);
    while ((n = read(pipefd[0], buffer, sizeof(buffer))) > 0) {
        if (ca_sb_append_n(&out, buffer, (size_t)n) != CA_OK) {
            ca_sb_free(&out);
            close(pipefd[0]);
            waitpid(pid, NULL, 0);
            return ca_strdup("");
        }
    }
    close(pipefd[0]);
    waitpid(pid, NULL, 0);
    {
        char *result = ca_sb_take(&out);
        return result == NULL ? ca_strdup("") : result;
    }
}

static int run_git_argv(const char *repository_path, char *const argv[]) {
    pid_t pid;
    int status = 0;
    if (argv == NULL || argv[0] == NULL) {
        return -1;
    }
    pid = fork();
    if (pid < 0) {
        return -1;
    }
    if (pid == 0) {
        if (repository_path != NULL && repository_path[0] != '\0' && chdir(repository_path) != 0) {
            _exit(127);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            return -1;
        }
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static ca_git_file_status_kind parse_status_kind(const char *line) {
    char x = line[0];
    char y = line[1];
    if (x == '?' && y == '?') return CA_GIT_FILE_UNTRACKED;
    if (x == 'U' || y == 'U') return CA_GIT_FILE_CONFLICTED;
    if (x == 'R') return CA_GIT_FILE_RENAMED;
    if (x == 'A') return CA_GIT_FILE_ADDED;
    if ((x == 'M' || x == 'D') && (y == 'M' || y == 'D')) return CA_GIT_FILE_STAGED_WITH_CHANGES;
    if (x == 'M' || x == 'D') return x == 'D' ? CA_GIT_FILE_DELETED : CA_GIT_FILE_STAGED;
    if (y == 'D') return CA_GIT_FILE_DELETED;
    return CA_GIT_FILE_MODIFIED;
}

static ca_status append_file(ca_git_status *status, const char *path, ca_git_file_status_kind kind) {
    ca_git_file_status *next;
    if (status->file_count > SIZE_MAX / sizeof(ca_git_file_status) - 1) {
        return CA_NO_MEMORY;
    }
    next = (ca_git_file_status *)realloc(status->files, sizeof(ca_git_file_status) * (status->file_count + 1));
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    status->files = next;
    memset(&status->files[status->file_count], 0, sizeof(status->files[status->file_count]));
    status->files[status->file_count].path = ca_strdup(path);
    if (status->files[status->file_count].path == NULL) {
        return CA_NO_MEMORY;
    }
    status->files[status->file_count].status = kind;
    status->file_count++;
    if (kind == CA_GIT_FILE_CONFLICTED) {
        status->has_conflicts = 1;
    }
    return CA_OK;
}

const char *ca_git_file_status_indicator(ca_git_file_status_kind status) {
    switch (status) {
        case CA_GIT_FILE_MODIFIED: return " M";
        case CA_GIT_FILE_STAGED: return "M ";
        case CA_GIT_FILE_STAGED_WITH_CHANGES: return "MM";
        case CA_GIT_FILE_UNTRACKED: return "??";
        case CA_GIT_FILE_DELETED: return " D";
        case CA_GIT_FILE_RENAMED: return "R ";
        case CA_GIT_FILE_CONFLICTED: return "UU";
        case CA_GIT_FILE_ADDED: return "A ";
    }
    return "??";
}

const char *ca_git_file_status_description(ca_git_file_status_kind status) {
    switch (status) {
        case CA_GIT_FILE_MODIFIED: return "modified";
        case CA_GIT_FILE_STAGED: return "staged";
        case CA_GIT_FILE_STAGED_WITH_CHANGES: return "staged with changes";
        case CA_GIT_FILE_UNTRACKED: return "untracked";
        case CA_GIT_FILE_DELETED: return "deleted";
        case CA_GIT_FILE_RENAMED: return "renamed";
        case CA_GIT_FILE_CONFLICTED: return "conflicted";
        case CA_GIT_FILE_ADDED: return "added";
    }
    return "unknown";
}

ca_status ca_git_status_load(const char *repository_path, ca_git_status *status) {
    char *output;
    char *line;
    char *saveptr = NULL;
    if (status == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    memset(status, 0, sizeof(*status));
    output = run_command_capture(repository_path, "git status --short --branch 2>&1");
    if (output == NULL) {
        return CA_NO_MEMORY;
    }
    if (strstr(output, "not a git repository") != NULL) {
        free(output);
        return CA_NOT_FOUND;
    }
    line = strtok_r(output, "\n", &saveptr);
    while (line != NULL) {
        if (strncmp(line, "## ", 3) == 0) {
            char *branch = line + 3;
            char *dots = strstr(branch, "...");
            if (dots != NULL) {
                *dots = '\0';
            }
            if (strcmp(branch, "HEAD (no branch)") == 0) {
                status->detached = 1;
            } else {
                status->branch = ca_strdup(branch);
            }
        } else if (strlen(line) >= 4) {
            const char *path = line + 3;
            const char *arrow = strstr(path, " -> ");
            if (arrow != NULL) {
                path = arrow + 4;
            }
            if (append_file(status, path, parse_status_kind(line)) != CA_OK) {
                free(output);
                ca_git_status_free(status);
                return CA_NO_MEMORY;
            }
        }
        line = strtok_r(NULL, "\n", &saveptr);
    }
    free(output);
    return CA_OK;
}

void ca_git_status_free(ca_git_status *status) {
    size_t i;
    if (status == NULL) {
        return;
    }
    free(status->branch);
    for (i = 0; i < status->file_count; i++) {
        free(status->files[i].path);
    }
    free(status->files);
    memset(status, 0, sizeof(*status));
}

ca_status ca_git_status_render(const ca_git_status *status, char **out) {
    ca_string_builder sb;
    size_t i;
    if (status == NULL || out == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    ca_sb_init(&sb);
    ca_sb_appendf(&sb, "Branch: %s\n", status->branch == NULL ? "(detached or unknown)" : status->branch);
    if (status->file_count == 0) {
        ca_sb_append(&sb, "Working tree clean.\n");
    } else {
        for (i = 0; i < status->file_count; i++) {
            ca_sb_appendf(&sb,
                          "%s %s (%s)\n",
                          ca_git_file_status_indicator(status->files[i].status),
                          status->files[i].path,
                          ca_git_file_status_description(status->files[i].status));
        }
    }
    *out = ca_sb_take(&sb);
    return *out == NULL ? CA_NO_MEMORY : CA_OK;
}

ca_status ca_git_group_summary(const ca_git_status *status, char **out) {
    ca_string_builder sb;
    size_t i;
    int docs = 0;
    int tests = 0;
    int build = 0;
    int code = 0;
    if (status == NULL || out == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    ca_sb_init(&sb);
    for (i = 0; i < status->file_count; i++) {
        const char *path = status->files[i].path == NULL ? "" : status->files[i].path;
        if (strstr(path, "test") != NULL || strstr(path, "tests/") != NULL) {
            tests++;
        } else if (strstr(path, "README") != NULL || strstr(path, "docs/") != NULL || strstr(path, ".md") != NULL) {
            docs++;
        } else if (strstr(path, "Makefile") != NULL || strstr(path, "configure") != NULL ||
                   strstr(path, ".am") != NULL || strstr(path, ".ac") != NULL ||
                   strstr(path, "config") != NULL) {
            build++;
        } else {
            code++;
        }
    }
    ca_sb_append(&sb, "Suggested groups:\n");
    if (code) ca_sb_appendf(&sb, "  implementation: %d file(s)\n", code);
    if (tests) ca_sb_appendf(&sb, "  tests: %d file(s)\n", tests);
    if (docs) ca_sb_appendf(&sb, "  documentation: %d file(s)\n", docs);
    if (build) ca_sb_appendf(&sb, "  build/config: %d file(s)\n", build);
    if (!code && !tests && !docs && !build) ca_sb_append(&sb, "  no changed files\n");
    *out = ca_sb_take(&sb);
    return *out == NULL ? CA_NO_MEMORY : CA_OK;
}

ca_status ca_git_diff(const char *repository_path, int staged, char **out) {
    if (out == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *out = run_command_capture(repository_path, staged ? "git diff --cached --stat && git diff --cached" : "git diff --stat && git diff");
    return *out == NULL ? CA_NO_MEMORY : CA_OK;
}

ca_status ca_git_generate_commit_message(const ca_git_status *status, char **out) {
    const char *verb = "Update";
    const char *scope = "project";
    size_t i;
    if (status == NULL || out == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    for (i = 0; i < status->file_count; i++) {
        const char *path = status->files[i].path;
        if (path == NULL) continue;
        if (strstr(path, "test") != NULL) {
            scope = "tests";
            break;
        }
        if (strstr(path, "doc") != NULL || strstr(path, "README") != NULL) {
            scope = "documentation";
        } else if (strstr(path, "config") != NULL || strstr(path, "configure") != NULL ||
                   strstr(path, "Makefile") != NULL || strstr(path, ".am") != NULL ||
                   strstr(path, ".ac") != NULL) {
            scope = "build configuration";
        } else if (strstr(path, ".c") != NULL || strstr(path, ".h") != NULL) {
            scope = "implementation";
        }
    }
    for (i = 0; i < status->file_count; i++) {
        if (status->files[i].status == CA_GIT_FILE_ADDED || status->files[i].status == CA_GIT_FILE_UNTRACKED) {
            verb = "Add";
            break;
        }
    }
    {
        ca_string_builder sb;
        ca_sb_init(&sb);
        ca_sb_appendf(&sb, "%s %s", verb, scope);
        *out = ca_sb_take(&sb);
    }
    return *out == NULL ? CA_NO_MEMORY : CA_OK;
}

ca_status ca_git_commit_all(const char *repository_path, const char *message, char **out_message) {
    ca_git_status status;
    char *generated = NULL;
    ca_status rc;
    if (out_message != NULL) {
        *out_message = NULL;
    }
    rc = ca_git_status_load(repository_path, &status);
    if (rc != CA_OK) return rc;
    if (status.file_count == 0) {
        ca_git_status_free(&status);
        return CA_NOT_FOUND;
    }
    if (message == NULL || message[0] == '\0') {
        rc = ca_git_generate_commit_message(&status, &generated);
        if (rc != CA_OK) {
            ca_git_status_free(&status);
            return rc;
        }
        message = generated;
    }
    ca_git_status_free(&status);
    {
        char *add_argv[] = {(char *)"git", (char *)"add", (char *)"-A", NULL};
        char *commit_argv[] = {(char *)"git", (char *)"commit", (char *)"-m", (char *)message, NULL};
        run_git_argv(repository_path, add_argv);
        run_git_argv(repository_path, commit_argv);
    }
    if (out_message != NULL) {
        *out_message = ca_strdup(message);
    }
    free(generated);
    return out_message == NULL || *out_message != NULL ? CA_OK : CA_NO_MEMORY;
}

ca_status ca_git_undo_last_commit(const char *repository_path, int hard) {
    char *out = run_command_capture(repository_path, hard ? "git reset --hard HEAD~1 2>&1" : "git reset --soft HEAD~1 2>&1");
    if (out == NULL) {
        return CA_NO_MEMORY;
    }
    free(out);
    return CA_OK;
}

ca_status ca_git_land_summary(const char *repository_path, char **out) {
    ca_string_builder sb;
    ca_git_status status;
    char *status_text = NULL;
    if (out == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    ca_sb_init(&sb);
    ca_sb_append(&sb, "[1/3] Build and tests\n");
    free(run_command_capture(repository_path, "if [ ! -x ./configure ]; then ./autogen.sh >/dev/null 2>&1; fi && ./configure >/dev/null 2>&1 && make -j 4 >/dev/null 2>&1 && make check >/dev/null 2>&1"));
    ca_sb_append(&sb, "  Autotools build and test command finished\n\n[2/3] Git status\n");
    memset(&status, 0, sizeof(status));
    if (ca_git_status_load(repository_path, &status) == CA_OK) {
        ca_git_status_render(&status, &status_text);
        ca_sb_append(&sb, status_text);
        free(status_text);
        ca_git_status_free(&status);
    } else {
        ca_sb_append(&sb, "  not a git repository\n");
    }
    ca_sb_append(&sb, "\n[3/3] Landing\n  Review changes, then run /commit <message>.\n");
    *out = ca_sb_take(&sb);
    return *out == NULL ? CA_NO_MEMORY : CA_OK;
}
