/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef __OpenBSD__
#include <unistd.h>
#endif

#ifdef __FreeBSD__
#include <sys/capsicum.h>
#endif

#ifdef __APPLE__
#if defined(__has_include)
#if __has_include(<sandbox.h>)
#define CODEAGENT_HAVE_MACOS_SANDBOX 1
#include <sandbox.h>
#endif
#endif
#endif

#ifdef __linux__
#include <sys/prctl.h>
#include <sys/syscall.h>
#if defined(__has_include)
#if __has_include(<linux/landlock.h>)
#define CODEAGENT_HAVE_LINUX_LANDLOCK 1
#include <linux/landlock.h>
#endif
#endif
#if defined(CODEAGENT_HAVE_LINUX_LANDLOCK) && \
    (!defined(__NR_landlock_create_ruleset) || !defined(__NR_landlock_add_rule) || !defined(__NR_landlock_restrict_self))
#undef CODEAGENT_HAVE_LINUX_LANDLOCK
#endif
#ifndef O_PATH
#define O_PATH 0
#endif
#endif

#ifndef LANDLOCK_ACCESS_FS_REFER
#define LANDLOCK_ACCESS_FS_REFER 0
#endif
#ifndef LANDLOCK_ACCESS_FS_TRUNCATE
#define LANDLOCK_ACCESS_FS_TRUNCATE 0
#endif

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int path_has_trusted_prefix(const char *path, const char *trusted) {
    size_t trusted_len;
    if (path == NULL || trusted == NULL || trusted[0] == '\0') {
        return 0;
    }
    trusted_len = strlen(trusted);
    if (strncmp(path, trusted, trusted_len) != 0) {
        return 0;
    }
    return path[trusted_len] == '\0' || trusted[trusted_len - 1] == '/' || path[trusted_len] == '/';
}

static int relative_path_is_workspace_safe(const char *path) {
    const char *p;
    if (path == NULL || path[0] == '\0' || path[0] == '/') {
        return 0;
    }
    p = path;
    while (*p != '\0') {
        size_t len = 0;
        while (p[len] != '\0' && p[len] != '/') {
            len++;
        }
        if (len == 2 && p[0] == '.' && p[1] == '.') {
            return 0;
        }
        p += len;
        if (*p == '/') {
            p++;
        }
    }
    return 1;
}

static int path_is_trusted(const ca_config *config, const char *path) {
    size_t i;
    if (config == NULL || path == NULL) {
        return 0;
    }
    for (i = 0; i < config->trusted_path_count; i++) {
        if (path_has_trusted_prefix(path, config->trusted_paths[i])) {
            return 1;
        }
    }
    return 0;
}

static int canonicalize_policy_path(const char *path,
                                    int write_access,
                                    char *resolved,
                                    size_t resolved_size) {
    char tmp[PATH_MAX];
    char parent[PATH_MAX];
    char parent_resolved[PATH_MAX];
    char *slash;
    const char *leaf;
    size_t parent_len;
    size_t leaf_len;

    if (path == NULL || resolved == NULL || resolved_size == 0) {
        return -1;
    }
    if (realpath(path, resolved) != NULL) {
        return 0;
    }
    if (!write_access || strlen(path) >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, path, strlen(path) + 1);
    slash = strrchr(tmp, '/');
    if (slash == NULL) {
        memcpy(parent, ".", 2);
        leaf = tmp;
    } else if (slash == tmp) {
        memcpy(parent, "/", 2);
        leaf = slash + 1;
    } else {
        *slash = '\0';
        if (strlen(tmp) >= sizeof(parent)) {
            return -1;
        }
        memcpy(parent, tmp, strlen(tmp) + 1);
        leaf = slash + 1;
    }
    if (leaf[0] == '\0' || realpath(parent, parent_resolved) == NULL) {
        return -1;
    }
    parent_len = strlen(parent_resolved);
    leaf_len = strlen(leaf);
    if (strcmp(parent_resolved, "/") == 0) {
        if (leaf_len + 2 > resolved_size) {
            return -1;
        }
        snprintf(resolved, resolved_size, "/%s", leaf);
        return 0;
    }
    if (parent_len + 1 + leaf_len + 1 > resolved_size) {
        return -1;
    }
    snprintf(resolved, resolved_size, "%s/%s", parent_resolved, leaf);
    return 0;
}

static int trusted_path_allows_canonical(const ca_config *config, const char *canonical) {
    size_t i;
    char trusted_resolved[PATH_MAX];
    if (config == NULL || canonical == NULL) {
        return 0;
    }
    for (i = 0; i < config->trusted_path_count; i++) {
        const char *trusted = config->trusted_paths[i];
        if (trusted != NULL &&
            realpath(trusted, trusted_resolved) != NULL &&
            path_has_trusted_prefix(canonical, trusted_resolved)) {
            return 1;
        }
    }
    return 0;
}

static int workspace_allows_canonical(const char *canonical) {
    char cwd[PATH_MAX];
    if (canonical == NULL || getcwd(cwd, sizeof(cwd)) == NULL) {
        return 0;
    }
    return path_has_trusted_prefix(canonical, cwd);
}

static int path_is_unsafe_hardlink(const char *path) {
    struct stat st;
    if (path == NULL || stat(path, &st) != 0) {
        return 0;
    }
    return S_ISREG(st.st_mode) && st.st_nlink > 1;
}

const char *ca_sandbox_mode_name(ca_sandbox_mode mode) {
    switch (mode) {
    case CA_SANDBOX_DISABLED: return "disabled";
    case CA_SANDBOX_READ_ONLY: return "read_only";
    case CA_SANDBOX_WORKSPACE_WRITE: return "workspace_write";
    case CA_SANDBOX_FULL_ACCESS: return "full_access";
    }
    return "unknown";
}

ca_status ca_sandbox_mode_parse(const char *text, ca_sandbox_mode *mode) {
    if (text == NULL || mode == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    if (strcmp(text, "disabled") == 0 || strcmp(text, "off") == 0) {
        *mode = CA_SANDBOX_DISABLED;
        return CA_OK;
    }
    if (strcmp(text, "read_only") == 0 || strcmp(text, "read-only") == 0) {
        *mode = CA_SANDBOX_READ_ONLY;
        return CA_OK;
    }
    if (strcmp(text, "workspace_write") == 0 || strcmp(text, "workspace-write") == 0) {
        *mode = CA_SANDBOX_WORKSPACE_WRITE;
        return CA_OK;
    }
    if (strcmp(text, "full_access") == 0 || strcmp(text, "full-access") == 0) {
        *mode = CA_SANDBOX_FULL_ACCESS;
        return CA_OK;
    }
    return CA_INVALID_ARGUMENT;
}

static void sandbox_add_escaped(ca_string_builder *sb, const char *text) {
    const char *p = text == NULL ? "" : text;
    while (*p != '\0') {
        if (*p == '"' || *p == '\\') {
            ca_sb_append_n(sb, "\\", 1);
        }
        ca_sb_append_n(sb, p, 1);
        p++;
    }
}

ca_status ca_config_set_sandbox_mode(ca_config *config, ca_sandbox_mode mode) {
    if (config == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    if (mode != CA_SANDBOX_DISABLED &&
        mode != CA_SANDBOX_READ_ONLY &&
        mode != CA_SANDBOX_WORKSPACE_WRITE &&
        mode != CA_SANDBOX_FULL_ACCESS) {
        return CA_INVALID_ARGUMENT;
    }
    config->sandbox_mode = mode;
    return CA_OK;
}

ca_status ca_sandbox_check_path(const ca_config *config,
                                const char *path,
                                int write_access,
                                char **output) {
    ca_sandbox_mode mode;
    if (path == NULL || path[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    if (config == NULL) {
        return CA_OK;
    }
    mode = config->sandbox_mode;
    if (mode == CA_SANDBOX_DISABLED || mode == CA_SANDBOX_FULL_ACCESS) {
        return CA_OK;
    }
    if (mode == CA_SANDBOX_READ_ONLY && write_access) {
        return ca_set_error(output, "Sandbox denied: read-only mode blocks writes to %s", path);
    }
    {
        char canonical[PATH_MAX];
        if (canonicalize_policy_path(path, write_access, canonical, sizeof(canonical)) == 0 &&
            ((relative_path_is_workspace_safe(path) && workspace_allows_canonical(canonical)) ||
             trusted_path_allows_canonical(config, canonical))) {
            if (path_is_unsafe_hardlink(path)) {
                return ca_set_error(output,
                                    "Sandbox denied: %s is a hardlink and may alias data outside the sandbox",
                                    path);
            }
            return CA_OK;
        }
        if (path_is_trusted(config, path)) {
            return ca_set_error(output,
                                "Sandbox denied: %s access to %s resolves outside trusted paths",
                                write_access ? "write" : "read",
                                path);
        }
    }
    return ca_set_error(output,
                        "Sandbox denied: %s access to %s is outside workspace/trusted paths",
                        write_access ? "write" : "read",
                        path);
}

int ca_sandbox_requires_native_process(const ca_config *config) {
    if (config == NULL ||
        config->sandbox_mode == CA_SANDBOX_DISABLED ||
        config->sandbox_mode == CA_SANDBOX_FULL_ACCESS) {
        return 0;
    }
#ifdef __OpenBSD__
    return 0;
#elif defined(__linux__) && defined(CODEAGENT_HAVE_LINUX_LANDLOCK)
    return 0;
#elif defined(__APPLE__) && defined(CODEAGENT_HAVE_MACOS_SANDBOX)
    return 0;
#elif defined(__FreeBSD__)
    return 0;
#else
    return 1;
#endif
}

#ifdef __linux__
#ifdef CODEAGENT_HAVE_LINUX_LANDLOCK
static __u64 landlock_read_execute_rights(void) {
    return LANDLOCK_ACCESS_FS_EXECUTE |
           LANDLOCK_ACCESS_FS_READ_FILE |
           LANDLOCK_ACCESS_FS_READ_DIR;
}

static __u64 landlock_write_rights(void) {
    return LANDLOCK_ACCESS_FS_WRITE_FILE |
           LANDLOCK_ACCESS_FS_REMOVE_DIR |
           LANDLOCK_ACCESS_FS_REMOVE_FILE |
           LANDLOCK_ACCESS_FS_MAKE_CHAR |
           LANDLOCK_ACCESS_FS_MAKE_DIR |
           LANDLOCK_ACCESS_FS_MAKE_REG |
           LANDLOCK_ACCESS_FS_MAKE_SOCK |
           LANDLOCK_ACCESS_FS_MAKE_FIFO |
           LANDLOCK_ACCESS_FS_MAKE_BLOCK |
           LANDLOCK_ACCESS_FS_MAKE_SYM |
           LANDLOCK_ACCESS_FS_REFER |
           LANDLOCK_ACCESS_FS_TRUNCATE;
}

static int landlock_add_path_rule(int ruleset_fd, const char *path, __u64 allowed_access) {
    struct landlock_path_beneath_attr path_beneath;
    int fd;
    if (path == NULL || path[0] == '\0') {
        return 0;
    }
    fd = open(path, O_PATH | O_CLOEXEC);
    if (fd < 0) {
        return -1;
    }
    memset(&path_beneath, 0, sizeof(path_beneath));
    path_beneath.allowed_access = allowed_access;
    path_beneath.parent_fd = fd;
    if (syscall(__NR_landlock_add_rule,
                ruleset_fd,
                LANDLOCK_RULE_PATH_BENEATH,
                &path_beneath,
                0) != 0) {
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int apply_linux_landlock(const ca_config *config) {
    struct landlock_ruleset_attr ruleset;
    __u64 handled;
    __u64 workspace_access;
    size_t i;
    int ruleset_fd;

    memset(&ruleset, 0, sizeof(ruleset));
    handled = landlock_read_execute_rights() | landlock_write_rights();
    ruleset.handled_access_fs = handled;
    workspace_access = landlock_read_execute_rights();
    if (config->sandbox_mode == CA_SANDBOX_WORKSPACE_WRITE) {
        workspace_access |= landlock_write_rights();
    }

    ruleset_fd = (int)syscall(__NR_landlock_create_ruleset, &ruleset, sizeof(ruleset), 0);
    if (ruleset_fd < 0) {
        return -1;
    }
    if (landlock_add_path_rule(ruleset_fd, ".", workspace_access) != 0 ||
        landlock_add_path_rule(ruleset_fd, "/bin", landlock_read_execute_rights()) != 0 ||
        landlock_add_path_rule(ruleset_fd, "/usr/bin", landlock_read_execute_rights()) != 0 ||
        landlock_add_path_rule(ruleset_fd, "/usr/local/bin", landlock_read_execute_rights()) != 0 ||
        landlock_add_path_rule(ruleset_fd, "/lib", landlock_read_execute_rights()) != 0 ||
        landlock_add_path_rule(ruleset_fd, "/lib64", landlock_read_execute_rights()) != 0 ||
        landlock_add_path_rule(ruleset_fd, "/usr/lib", landlock_read_execute_rights()) != 0 ||
        landlock_add_path_rule(ruleset_fd, "/usr/lib64", landlock_read_execute_rights()) != 0) {
        close(ruleset_fd);
        return -1;
    }
    for (i = 0; i < config->trusted_path_count; i++) {
        if (landlock_add_path_rule(ruleset_fd, config->trusted_paths[i], workspace_access) != 0) {
            close(ruleset_fd);
            return -1;
        }
    }
    if (prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0 ||
        syscall(__NR_landlock_restrict_self, ruleset_fd, 0) != 0) {
        close(ruleset_fd);
        return -1;
    }
    close(ruleset_fd);
    return 0;
}
#endif
#endif

#ifdef __APPLE__
#ifdef CODEAGENT_HAVE_MACOS_SANDBOX
static ca_status append_macos_subpath_rule(ca_string_builder *sb, const char *permission, const char *path) {
    if (path == NULL || path[0] == '\0') {
        return CA_OK;
    }
    if (ca_sb_append(sb, "(allow ") != CA_OK ||
        ca_sb_append(sb, permission) != CA_OK ||
        ca_sb_append(sb, " (subpath \"") != CA_OK) {
        return CA_NO_MEMORY;
    }
    sandbox_add_escaped(sb, path);
    return ca_sb_append(sb, "\"))\n");
}

static int apply_macos_sandbox(const ca_config *config) {
    ca_string_builder profile;
    char cwd[4096];
    char *error = NULL;
    const char *fs_permission = config->sandbox_mode == CA_SANDBOX_READ_ONLY ? "file-read*" : "file*";
    size_t i;
    int rc;

    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        return -1;
    }
    ca_sb_init(&profile);
    if (ca_sb_append(&profile, "(version 1)\n(deny default)\n(allow process*)\n(allow sysctl*)\n") != CA_OK ||
        ca_sb_append(&profile, "(allow file-read-metadata)\n(allow file-read* (subpath \"/System/Library\"))\n") != CA_OK ||
        ca_sb_append(&profile, "(allow file-read* (subpath \"/usr/lib\"))\n(allow file-read* (subpath \"/bin\"))\n") != CA_OK ||
        ca_sb_append(&profile, "(allow file-read* (subpath \"/usr/bin\"))\n(allow file-read* (subpath \"/usr/local/bin\"))\n") != CA_OK ||
        append_macos_subpath_rule(&profile, fs_permission, cwd) != CA_OK) {
        ca_sb_free(&profile);
        return -1;
    }
    for (i = 0; i < config->trusted_path_count; i++) {
        if (append_macos_subpath_rule(&profile, fs_permission, config->trusted_paths[i]) != CA_OK) {
            ca_sb_free(&profile);
            return -1;
        }
    }
    if (config->sandbox_network) {
        if (ca_sb_append(&profile, "(allow network*)\n") != CA_OK) {
            ca_sb_free(&profile);
            return -1;
        }
    }
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
    rc = sandbox_init(profile.data, 0, &error);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    ca_sb_free(&profile);
    if (error != NULL) {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
        sandbox_free_error(error);
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
    }
    return rc == 0 ? 0 : -1;
}
#endif
#endif

int ca_sandbox_apply_child(const ca_config *config, int needs_network) {
    if (config == NULL ||
        config->sandbox_mode == CA_SANDBOX_DISABLED ||
        config->sandbox_mode == CA_SANDBOX_FULL_ACCESS) {
        return 0;
    }
#ifdef __OpenBSD__
    {
        size_t i;
        int read_only = config->sandbox_mode == CA_SANDBOX_READ_ONLY;
        const char *perms = read_only ? "r" : "rwc";
        const char *promises = (needs_network || config->sandbox_network)
                                   ? (read_only ? "stdio rpath proc exec inet dns" : "stdio rpath wpath cpath proc exec inet dns")
                                   : (read_only ? "stdio rpath proc exec" : "stdio rpath wpath cpath proc exec");
        if (unveil(".", perms) != 0) {
            return -1;
        }
        if (unveil("/bin", "rx") != 0 ||
            unveil("/usr/bin", "rx") != 0 ||
            unveil("/usr/local/bin", "rx") != 0) {
            return -1;
        }
        for (i = 0; i < config->trusted_path_count; i++) {
            if (config->trusted_paths[i] != NULL && unveil(config->trusted_paths[i], perms) != 0) {
                return -1;
            }
        }
        if (unveil(NULL, NULL) != 0) {
            return -1;
        }
        return pledge(promises, NULL) == 0 ? 0 : -1;
    }
#elif defined(__linux__) && defined(CODEAGENT_HAVE_LINUX_LANDLOCK)
    (void)needs_network;
    return apply_linux_landlock(config);
#elif defined(__APPLE__) && defined(CODEAGENT_HAVE_MACOS_SANDBOX)
    (void)needs_network;
    return apply_macos_sandbox(config);
#elif defined(__FreeBSD__)
    (void)needs_network;
    return cap_enter() == 0 ? 0 : -1;
#else
    (void)needs_network;
    return 0;
#endif
}
