/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

#define CA_TOOL_READ_MAX 100000u
#define CA_TOOL_BASH_MAX_OUTPUT 50000u
#define CA_TOOL_LIST_MAX_FILES 500u
#define CA_TOOL_SEARCH_MAX_MATCHES 50u

struct ca_tool_registry {
    ca_tool_definition *tools;
    size_t tool_count;
};

static void tool_definition_clear(ca_tool_definition *tool) {
    if (tool == NULL) {
        return;
    }
    free(tool->name);
    free(tool->description);
    free(tool->input_schema_json);
    memset(tool, 0, sizeof(*tool));
}

static ca_status tool_definition_set(ca_tool_definition *tool,
                                     const char *name,
                                     const char *description,
                                     const char *input_schema_json,
                                     ca_tool_fn function,
                                     void *userdata) {
    if (tool == NULL || name == NULL || function == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    tool_definition_clear(tool);
    tool->name = ca_strdup(name);
    tool->description = ca_strdup(description == NULL ? "" : description);
    tool->input_schema_json = ca_strdup(input_schema_json == NULL ? "{\"type\":\"object\"}" : input_schema_json);
    tool->function = function;
    tool->userdata = userdata;
    if (tool->name == NULL || tool->description == NULL || tool->input_schema_json == NULL) {
        tool_definition_clear(tool);
        return CA_NO_MEMORY;
    }
    return CA_OK;
}

static ca_status tool_definition_clone_one(const ca_tool_definition *src, ca_tool_definition *dst) {
    if (src == NULL || dst == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof(*dst));
    return tool_definition_set(dst,
                               src->name,
                               src->description,
                               src->input_schema_json,
                               src->function,
                               src->userdata);
}

static ca_status tool_definitions_clone(const ca_tool_definition *src,
                                        size_t count,
                                        ca_tool_definition **dst) {
    size_t i;
    if (dst == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *dst = NULL;
    if (count == 0) {
        return CA_OK;
    }
    *dst = (ca_tool_definition *)calloc(count, sizeof(ca_tool_definition));
    if (*dst == NULL) {
        return CA_NO_MEMORY;
    }
    for (i = 0; i < count; i++) {
        if (tool_definition_clone_one(&src[i], &(*dst)[i]) != CA_OK) {
            ca_tool_definitions_free(*dst, count);
            *dst = NULL;
            return CA_NO_MEMORY;
        }
    }
    return CA_OK;
}

static ca_status required_string_field(const char *input_json,
                                       const char *field,
                                       const char *parse_prefix,
                                       char **value,
                                       char **output) {
    ca_status status;
    if (input_json == NULL || value == NULL || output == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *value = NULL;
    status = ca_json_get_string(input_json, field, value);
    if (status != CA_OK) {
        return ca_set_error(output, "%s: missing or invalid string field '%s'", parse_prefix, field);
    }
    return CA_OK;
}

static ca_status check_write_permission(const ca_config *config,
                                        const char *tool_name,
                                        const char *path,
                                        char **output) {
    ca_permission_result permission;
    if (config == NULL) {
        return CA_OK;
    }
    permission = ca_permission_check_path(config, path, 1);
    if (permission == CA_PERMISSION_ALLOW) {
        return CA_OK;
    }
    if (permission == CA_PERMISSION_DENY) {
        if (strcmp(tool_name, "edit_file") == 0) {
            return ca_set_error(output, "Permission denied: Cannot edit %s", path);
        }
        return ca_set_error(output, "Permission denied: Cannot write to %s", path);
    }
    if (strcmp(tool_name, "edit_file") == 0) {
        return ca_set_error(output, "ErrorCategory::Permission|Editing %s requires confirmation", path);
    }
    return ca_set_error(output, "ErrorCategory::Permission|Writing to %s requires confirmation", path);
}

static size_t count_lines_prefix(const char *text, size_t max_len) {
    size_t i;
    size_t lines = 0;
    for (i = 0; i < max_len && text[i] != '\0'; i++) {
        if (text[i] == '\n') {
            lines++;
        }
    }
    if (max_len > 0 && text[0] != '\0' && text[max_len - 1] != '\n') {
        lines++;
    }
    return lines;
}

static size_t count_occurrences(const char *haystack, const char *needle) {
    size_t count = 0;
    size_t needle_len;
    const char *p;
    if (haystack == NULL || needle == NULL || needle[0] == '\0') {
        return 0;
    }
    needle_len = strlen(needle);
    p = haystack;
    while ((p = strstr(p, needle)) != NULL) {
        count++;
        p += needle_len;
    }
    return count;
}

static ca_status ensure_parent_dirs(const char *path) {
    char tmp[PATH_MAX];
    char *p;
    size_t len;

    if (path == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    len = strlen(path);
    if (len >= sizeof(tmp)) {
        return CA_INVALID_ARGUMENT;
    }
    memcpy(tmp, path, len + 1);
    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                return CA_IO_ERROR;
            }
            *p = '/';
        }
    }
    return CA_OK;
}

static ca_status read_file_all(const char *path, char **content, size_t *len) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return CA_IO_ERROR;
    }
    {
        ca_status status = ca_read_all(file, content, len, 0);
        fclose(file);
        return status;
    }
}

ca_status ca_tool_read_file(const char *input_json, char **output, void *userdata) {
    char *path = NULL;
    char *content = NULL;
    size_t len = 0;
    (void)userdata;

    if (output == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *output = NULL;
    if (required_string_field(input_json, "path", "Failed to parse input", &path, output) != CA_OK) {
        return CA_ERROR;
    }
    if (path[0] == '\0') {
        free(path);
        return ca_set_error(output, "path cannot be empty");
    }
    if (read_file_all(path, &content, &len) != CA_OK) {
        ca_status status = ca_set_error(output, "Failed to read file: %s", strerror(errno));
        free(path);
        return status;
    }
    if (len > CA_TOOL_READ_MAX) {
        ca_string_builder sb;
        size_t line_count = count_lines_prefix(content, CA_TOOL_READ_MAX);
        ca_sb_init(&sb);
        ca_sb_append_n(&sb, content, CA_TOOL_READ_MAX);
        ca_sb_appendf(&sb,
                      "\n\n... [Truncated: showing first %u characters, %lu lines. File is %lu bytes total]",
                      CA_TOOL_READ_MAX,
                      (unsigned long)line_count,
                      (unsigned long)len);
        free(content);
        content = ca_sb_take(&sb);
    }
    *output = content;
    free(path);
    return CA_OK;
}

ca_status ca_tool_write_file(const char *input_json, char **output, void *userdata) {
    char *path = NULL;
    char *content = NULL;
    FILE *file;
    const ca_config *config = (const ca_config *)userdata;

    if (output == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *output = NULL;
    if (required_string_field(input_json, "path", "Failed to parse input", &path, output) != CA_OK) {
        return CA_ERROR;
    }
    if (path[0] == '\0') {
        free(path);
        return ca_set_error(output, "path cannot be empty");
    }
    if (required_string_field(input_json, "content", "Failed to parse input", &content, output) != CA_OK) {
        free(path);
        return CA_ERROR;
    }
    if (check_write_permission(config, "write_file", path, output) != CA_OK) {
        free(path);
        free(content);
        return CA_PERMISSION_REQUIRED;
    }
    if (ensure_parent_dirs(path) != CA_OK) {
        free(path);
        free(content);
        return ca_set_error(output, "Failed to create directory: %s", strerror(errno));
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        free(path);
        free(content);
        return ca_set_error(output, "Failed to write file: %s", strerror(errno));
    }
    if (fwrite(content, 1, strlen(content), file) != strlen(content) || fclose(file) != 0) {
        free(path);
        free(content);
        return ca_set_error(output, "Failed to write file: %s", strerror(errno));
    }
    ca_set_error(output, "Successfully wrote %lu bytes to %s", (unsigned long)strlen(content), path);
    free(path);
    free(content);
    return CA_OK;
}

ca_status ca_tool_edit_file(const char *input_json, char **output, void *userdata) {
    char *path = NULL;
    char *old_str = NULL;
    char *new_str = NULL;
    char *content = NULL;
    char *first;
    ca_string_builder sb;
    FILE *file;
    const ca_config *config = (const ca_config *)userdata;

    if (output == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *output = NULL;
    if (required_string_field(input_json, "path", "Invalid input", &path, output) != CA_OK) {
        return CA_ERROR;
    }
    if (path[0] == '\0') {
        free(path);
        return ca_set_error(output, "path cannot be empty");
    }
    if (required_string_field(input_json, "old_str", "Invalid input", &old_str, output) != CA_OK) {
        free(path);
        return CA_ERROR;
    }
    if (required_string_field(input_json, "new_str", "Invalid input", &new_str, output) != CA_OK) {
        free(path);
        free(old_str);
        return CA_ERROR;
    }
    if (strcmp(old_str, new_str) == 0) {
        free(path);
        free(old_str);
        free(new_str);
        return ca_set_error(output, "old_str and new_str must be different");
    }
    if (check_write_permission(config, "edit_file", path, output) != CA_OK) {
        free(path);
        free(old_str);
        free(new_str);
        return CA_PERMISSION_REQUIRED;
    }

    if (read_file_all(path, &content, NULL) != CA_OK) {
        if (old_str[0] != '\0') {
            ca_status status = ca_set_error(output, "file '%s' does not exist", path);
            free(path);
            free(old_str);
            free(new_str);
            return status;
        }
        if (ensure_parent_dirs(path) != CA_OK) {
            free(path);
            free(old_str);
            free(new_str);
            return ca_set_error(output, "Failed to create directory: %s", strerror(errno));
        }
        file = fopen(path, "wb");
        if (file == NULL) {
            free(path);
            free(old_str);
            free(new_str);
            return ca_set_error(output, "Failed to create file: %s", strerror(errno));
        }
        if (fwrite(new_str, 1, strlen(new_str), file) != strlen(new_str) || fclose(file) != 0) {
            free(path);
            free(old_str);
            free(new_str);
            return ca_set_error(output, "Failed to create file: %s", strerror(errno));
        }
        ca_set_error(output, "Successfully created file %s", path);
        free(path);
        free(old_str);
        free(new_str);
        return CA_OK;
    }

    ca_sb_init(&sb);
    if (old_str[0] == '\0') {
        ca_sb_append(&sb, content);
        ca_sb_append(&sb, new_str);
    } else {
        size_t matches = count_occurrences(content, old_str);
        if (matches == 0) {
            ca_sb_free(&sb);
            free(path);
            free(old_str);
            free(new_str);
            free(content);
            return ca_set_error(output, "old_str not found in file");
        }
        if (matches > 1) {
            ca_sb_free(&sb);
            free(path);
            free(old_str);
            free(new_str);
            free(content);
            return ca_set_error(output, "old_str found %lu times in file, must be unique", (unsigned long)matches);
        }
        first = strstr(content, old_str);
        ca_sb_append_n(&sb, content, (size_t)(first - content));
        ca_sb_append(&sb, new_str);
        ca_sb_append(&sb, first + strlen(old_str));
    }

    file = fopen(path, "wb");
    if (file == NULL) {
        ca_sb_free(&sb);
        free(path);
        free(old_str);
        free(new_str);
        free(content);
        return ca_set_error(output, "Failed to write file: %s", strerror(errno));
    }
    {
        char *next = ca_sb_take(&sb);
        if (fwrite(next, 1, strlen(next), file) != strlen(next)) {
            free(next);
            fclose(file);
            free(path);
            free(old_str);
            free(new_str);
            free(content);
            return ca_set_error(output, "Failed to write file: %s", strerror(errno));
        }
        free(next);
    }
    if (fclose(file) != 0) {
        free(path);
        free(old_str);
        free(new_str);
        free(content);
        return ca_set_error(output, "Failed to write file: %s", strerror(errno));
    }
    *output = ca_strdup("OK");
    free(path);
    free(old_str);
    free(new_str);
    free(content);
    return CA_OK;
}

static int should_skip(const char *name) {
    return strcmp(name, ".git") == 0 || strcmp(name, ".devenv") == 0 || strcmp(name, "target") == 0 ||
           strcmp(name, "node_modules") == 0;
}

static ca_status list_dir(ca_string_builder *sb, const char *root, const char *rel, int depth, size_t *count) {
    char full[PATH_MAX];
    DIR *dir;
    struct dirent *entry;

    if (depth > 3 || *count >= CA_TOOL_LIST_MAX_FILES) {
        return CA_OK;
    }
    snprintf(full, sizeof(full), "%s/%s", root, rel == NULL ? "" : rel);
    dir = opendir(full);
    if (dir == NULL) {
        return CA_IO_ERROR;
    }
    while ((entry = readdir(dir)) != NULL && *count < CA_TOOL_LIST_MAX_FILES) {
        char child_rel[PATH_MAX];
        char child_full[PATH_MAX];
        struct stat st;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || should_skip(entry->d_name)) {
            continue;
        }
        if (rel != NULL && rel[0] != '\0') {
            snprintf(child_rel, sizeof(child_rel), "%s/%s", rel, entry->d_name);
        } else {
            snprintf(child_rel, sizeof(child_rel), "%s", entry->d_name);
        }
        snprintf(child_full, sizeof(child_full), "%s/%s", root, child_rel);
        if (stat(child_full, &st) != 0) {
            continue;
        }
        {
            char with_suffix[PATH_MAX + 2];
            char *escaped;
            snprintf(with_suffix, sizeof(with_suffix), "%s%s", child_rel, S_ISDIR(st.st_mode) ? "/" : "");
            escaped = ca_json_escape(with_suffix);
            ca_sb_appendf(sb, "%s  \"%s\"", *count == 0 ? "" : ",\n", escaped == NULL ? "" : escaped);
            free(escaped);
        }
        (*count)++;
        if (S_ISDIR(st.st_mode)) {
            list_dir(sb, root, child_rel, depth + 1, count);
        }
    }
    closedir(dir);
    return CA_OK;
}

ca_status ca_tool_list_files(const char *input_json, char **output, void *userdata) {
    char *path = NULL;
    ca_string_builder sb;
    size_t count = 0;
    (void)userdata;

    if (output == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *output = NULL;
    if (input_json == NULL) {
        return ca_set_error(output, "Invalid input: input JSON is required");
    }
    if (ca_json_get_string(input_json, "path", &path) != CA_OK) {
        path = ca_strdup(".");
    }
    ca_sb_init(&sb);
    ca_sb_append(&sb, "[\n");
    if (list_dir(&sb, path, "", 1, &count) != CA_OK) {
        ca_sb_free(&sb);
        free(path);
        return ca_set_error(output, "Error walking directory: %s", strerror(errno));
    }
    if (count >= CA_TOOL_LIST_MAX_FILES) {
        ca_sb_append(&sb, ",\n  \"... (truncated, more files exist)\"");
    }
    ca_sb_append(&sb, "\n]");
    *output = ca_sb_take(&sb);
    free(path);
    return CA_OK;
}

static ca_status read_tmpfile(FILE *file, char **out) {
    size_t len = 0;
    if (fseek(file, 0, SEEK_SET) != 0) {
        return CA_IO_ERROR;
    }
    return ca_read_all(file, out, &len, 0);
}

static ca_status run_argv_capture(char *const argv[],
                                  char **stdout_out,
                                  char **stderr_out,
                                  int *exit_code) {
    FILE *stdout_file;
    FILE *stderr_file;
    pid_t pid;
    int status;

    if (argv == NULL || stdout_out == NULL || stderr_out == NULL || exit_code == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *stdout_out = NULL;
    *stderr_out = NULL;
    *exit_code = -1;
    stdout_file = tmpfile();
    stderr_file = tmpfile();
    if (stdout_file == NULL || stderr_file == NULL) {
        if (stdout_file != NULL) {
            fclose(stdout_file);
        }
        if (stderr_file != NULL) {
            fclose(stderr_file);
        }
        return CA_IO_ERROR;
    }

    pid = fork();
    if (pid < 0) {
        fclose(stdout_file);
        fclose(stderr_file);
        return CA_IO_ERROR;
    }
    if (pid == 0) {
        dup2(fileno(stdout_file), STDOUT_FILENO);
        dup2(fileno(stderr_file), STDERR_FILENO);
        execvp(argv[0], argv);
        _exit(127);
    }

    if (waitpid(pid, &status, 0) < 0) {
        fclose(stdout_file);
        fclose(stderr_file);
        return CA_IO_ERROR;
    }
    fflush(stdout_file);
    fflush(stderr_file);
    if (WIFEXITED(status)) {
        *exit_code = WEXITSTATUS(status);
    }
    if (read_tmpfile(stdout_file, stdout_out) != CA_OK ||
        read_tmpfile(stderr_file, stderr_out) != CA_OK) {
        fclose(stdout_file);
        fclose(stderr_file);
        free(*stdout_out);
        free(*stderr_out);
        *stdout_out = NULL;
        *stderr_out = NULL;
        return CA_IO_ERROR;
    }
    fclose(stdout_file);
    fclose(stderr_file);
    return CA_OK;
}

static char *trimmed_or_empty(char *text) {
    char *trimmed = ca_trim_copy(text == NULL ? "" : text);
    free(text);
    return trimmed == NULL ? ca_strdup("") : trimmed;
}

static ca_status truncate_output(char **output) {
    size_t len;
    char *current;
    char *truncated;
    if (output == NULL || *output == NULL) {
        return CA_OK;
    }
    len = strlen(*output);
    if (len <= CA_TOOL_BASH_MAX_OUTPUT) {
        return CA_OK;
    }
    current = *output;
    truncated = NULL;
    if (ca_set_error(&truncated,
                     "%.*s\n\n... [Truncated: showing first %u characters of %lu total]",
                     (int)CA_TOOL_BASH_MAX_OUTPUT,
                     current,
                     CA_TOOL_BASH_MAX_OUTPUT,
                     (unsigned long)len) != CA_OK) {
        return CA_NO_MEMORY;
    }
    free(current);
    *output = truncated;
    return CA_OK;
}

ca_status ca_tool_bash(const char *input_json, char **output, void *userdata) {
    char *command = NULL;
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    int exit_code = 0;
    (void)userdata;

    if (output == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *output = NULL;
    if (required_string_field(input_json, "command", "Failed to parse input", &command, output) != CA_OK) {
        return CA_ERROR;
    }
    if (command[0] == '\0') {
        free(command);
        return ca_set_error(output, "command cannot be empty");
    }
    if (ca_contains(command, "rm -rf /") || ca_contains(command, "rm -rf /*") ||
        ca_contains(command, "> /dev/sda") || ca_contains(command, "mkfs") ||
        ca_contains(command, ":(){:|:&};:")) {
        ca_status status = ca_set_error(output,
                                        "Refusing to execute potentially dangerous command containing '%s'",
                                        ca_contains(command, "rm -rf /") ? "rm -rf /" :
                                        ca_contains(command, "rm -rf /*") ? "rm -rf /*" :
                                        ca_contains(command, "> /dev/sda") ? "> /dev/sda" :
                                        ca_contains(command, "mkfs") ? "mkfs" :
                                        ":(){:|:&};:");
        free(command);
        return status;
    }
    {
        char *argv[4];
        argv[0] = (char *)"bash";
        argv[1] = (char *)"-c";
        argv[2] = command;
        argv[3] = NULL;
        if (run_argv_capture(argv, &stdout_text, &stderr_text, &exit_code) != CA_OK) {
            free(command);
            return ca_set_error(output, "Failed to execute command: %s", strerror(errno));
        }
    }
    stdout_text = trimmed_or_empty(stdout_text);
    stderr_text = trimmed_or_empty(stderr_text);
    if (stdout_text == NULL || stderr_text == NULL) {
        free(command);
        free(stdout_text);
        free(stderr_text);
        return CA_NO_MEMORY;
    }
    if (exit_code != 0) {
        ca_set_error(output, "Command failed with exit code: %d\nstdout: %s\nstderr: %s", exit_code, stdout_text, stderr_text);
        free(command);
        free(stdout_text);
        free(stderr_text);
        return CA_ERROR;
    }
    *output = stdout_text[0] == '\0' ? ca_strdup(stderr_text) : ca_strdup(stdout_text);
    if (*output == NULL) {
        free(command);
        free(stdout_text);
        free(stderr_text);
        return CA_NO_MEMORY;
    }
    if (truncate_output(output) != CA_OK) {
        free(command);
        free(stdout_text);
        free(stderr_text);
        return CA_NO_MEMORY;
    }
    free(command);
    free(stdout_text);
    free(stderr_text);
    return CA_OK;
}

ca_status ca_tool_code_search(const char *input_json, char **output, void *userdata) {
    char *pattern = NULL;
    char *path = NULL;
    char *file_type = NULL;
    int case_sensitive = 0;
    char *stdout_text = NULL;
    char *stderr_text = NULL;
    int exit_code = 0;
    (void)userdata;

    if (output == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *output = NULL;
    if (required_string_field(input_json, "pattern", "Failed to parse input", &pattern, output) != CA_OK) {
        return CA_ERROR;
    }
    if (pattern[0] == '\0') {
        free(pattern);
        return ca_set_error(output, "pattern is required");
    }
    if (ca_json_get_string(input_json, "path", &path) != CA_OK) {
        path = ca_strdup(".");
    }
    ca_json_get_string(input_json, "file_type", &file_type);
    ca_json_get_bool(input_json, "case_sensitive", 0, &case_sensitive);

    {
        char *argv[11];
        int argc = 0;
        argv[argc++] = (char *)"rg";
        argv[argc++] = (char *)"--line-number";
        argv[argc++] = (char *)"--with-filename";
        argv[argc++] = (char *)"--color=never";
        if (!case_sensitive) {
            argv[argc++] = (char *)"--ignore-case";
        }
        if (file_type != NULL) {
            argv[argc++] = (char *)"--type";
            argv[argc++] = file_type;
        }
        argv[argc++] = pattern;
        argv[argc++] = path;
        argv[argc] = NULL;
        if (run_argv_capture(argv, &stdout_text, &stderr_text, &exit_code) != CA_OK) {
            free(pattern);
            free(path);
            free(file_type);
            free(stdout_text);
            free(stderr_text);
            return ca_set_error(output, "Failed to execute ripgrep: %s", strerror(errno));
        }
    }
    if (exit_code == 1) {
        *output = ca_strdup("No matches found");
        free(pattern);
        free(path);
        free(file_type);
        free(stdout_text);
        free(stderr_text);
        return CA_OK;
    }
    if (exit_code != 0) {
        char *stderr_trimmed = trimmed_or_empty(stderr_text);
        ca_status status = ca_set_error(output, "search failed: %s", stderr_trimmed == NULL ? "" : stderr_trimmed);
        free(stderr_trimmed);
        free(pattern);
        free(path);
        free(file_type);
        free(stdout_text);
        return status;
    }
    {
        char *trimmed = trimmed_or_empty(stdout_text);
        size_t lines = 0;
        size_t i;
        if (trimmed == NULL) {
            free(pattern);
            free(path);
            free(file_type);
            free(stderr_text);
            return CA_NO_MEMORY;
        }
        for (i = 0; trimmed[i] != '\0'; i++) {
            if (trimmed[i] == '\n') {
                lines++;
            }
        }
        if (trimmed[0] != '\0') {
            lines++;
        }
        if (lines > CA_TOOL_SEARCH_MAX_MATCHES) {
            ca_string_builder sb;
            size_t copied = 0;
            const char *start = trimmed;
            const char *p = trimmed;
            ca_sb_init(&sb);
            while (*p != '\0' && copied < CA_TOOL_SEARCH_MAX_MATCHES) {
                if (*p == '\n') {
                    ca_sb_append_n(&sb, start, (size_t)(p - start));
                    copied++;
                    if (copied < CA_TOOL_SEARCH_MAX_MATCHES) {
                        ca_sb_append(&sb, "\n");
                    }
                    start = p + 1;
                }
                p++;
            }
            if (copied < CA_TOOL_SEARCH_MAX_MATCHES && start[0] != '\0') {
                if (copied > 0) {
                    ca_sb_append(&sb, "\n");
                }
                ca_sb_append(&sb, start);
                copied++;
            }
            ca_sb_appendf(&sb, "\n... (showing first 50 of %lu matches)", (unsigned long)lines);
            *output = ca_sb_take(&sb);
            free(trimmed);
        } else {
            *output = trimmed;
        }
    }
    free(pattern);
    free(path);
    free(file_type);
    free(stderr_text);
    return CA_OK;
}

ca_tool_error_category ca_tool_categorize_error(const char *error) {
    if (ca_contains(error, "permission denied") || ca_contains(error, "Permission denied") ||
        ca_contains(error, "requires confirmation")) {
        return CA_TOOL_ERROR_PERMISSION;
    }
    if (ca_contains(error, "timeout") || ca_contains(error, "timed out")) {
        return CA_TOOL_ERROR_TIMEOUT;
    }
    if (ca_contains(error, "connection") || ca_contains(error, "network") || ca_contains(error, "rate limit")) {
        return CA_TOOL_ERROR_NETWORK;
    }
    if (ca_contains(error, "not found") || ca_contains(error, "No such file")) {
        return CA_TOOL_ERROR_RESOURCE;
    }
    if (ca_contains(error, "error:") || ca_contains(error, "cannot find") || ca_contains(error, "unresolved")) {
        return CA_TOOL_ERROR_CODE;
    }
    return CA_TOOL_ERROR_UNKNOWN;
}

ca_status ca_builtin_tools(ca_tool_definition **tools, size_t *tool_count) {
    ca_tool_definition *defs;
    static const char *read_file_schema =
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"The path of the file to read (relative to current directory or absolute).\"}},\"required\":[\"path\"]}";
    static const char *write_file_schema =
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"The path of the file to write (relative to current directory or absolute).\"},\"content\":{\"type\":\"string\",\"description\":\"The content to write to the file.\"}},\"required\":[\"path\",\"content\"]}";
    static const char *edit_file_schema =
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"The path to the file.\"},\"old_str\":{\"type\":\"string\",\"description\":\"Text to search for - must match exactly and must only have one match exactly.\"},\"new_str\":{\"type\":\"string\",\"description\":\"Text to replace old_str with.\"}},\"required\":[\"path\",\"old_str\",\"new_str\"]}";
    static const char *list_files_schema =
        "{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\",\"description\":\"Optional path to list files from. Defaults to current directory if not provided.\"}}}";
    static const char *bash_schema =
        "{\"type\":\"object\",\"properties\":{\"command\":{\"type\":\"string\",\"description\":\"The bash command to execute.\"}},\"required\":[\"command\"]}";
    static const char *code_search_schema =
        "{\"type\":\"object\",\"properties\":{\"pattern\":{\"type\":\"string\",\"description\":\"The search pattern or regex to look for.\"},\"path\":{\"type\":\"string\",\"description\":\"Optional path to search in (file or directory). Defaults to current directory.\"},\"file_type\":{\"type\":\"string\",\"description\":\"Optional file extension to limit search to (e.g., 'rs', 'js', 'py').\"},\"case_sensitive\":{\"type\":\"boolean\",\"description\":\"Whether the search should be case sensitive (default: false).\"}},\"required\":[\"pattern\"]}";

    if (tools == NULL || tool_count == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    defs = (ca_tool_definition *)calloc(6, sizeof(ca_tool_definition));
    if (defs == NULL) {
        return CA_NO_MEMORY;
    }
    defs[0].name = ca_strdup("read_file");
    defs[0].description = ca_strdup("Read the contents of a file. Use this when you want to see what's inside a file. Do not use this with directory names.");
    defs[0].input_schema_json = ca_strdup(read_file_schema);
    defs[0].function = ca_tool_read_file;
    defs[1].name = ca_strdup("write_file");
    defs[1].description = ca_strdup("Create a new file or overwrite an existing file with the given content. Use this when you need to create a new file from scratch.");
    defs[1].input_schema_json = ca_strdup(write_file_schema);
    defs[1].function = ca_tool_write_file;
    defs[2].name = ca_strdup("edit_file");
    defs[2].description = ca_strdup("Make edits to a text file. Replaces 'old_str' with 'new_str' in the given file. 'old_str' and 'new_str' MUST be different from each other. The old_str must appear exactly once in the file. If the file doesn't exist and old_str is empty, the file will be created with new_str as content.");
    defs[2].input_schema_json = ca_strdup(edit_file_schema);
    defs[2].function = ca_tool_edit_file;
    defs[3].name = ca_strdup("list_files");
    defs[3].description = ca_strdup("List files and directories at a given path. If no path is provided, lists files in the current directory. Useful for exploring the codebase structure.");
    defs[3].input_schema_json = ca_strdup(list_files_schema);
    defs[3].function = ca_tool_list_files;
    defs[4].name = ca_strdup("bash");
    defs[4].description = ca_strdup("Execute a bash command and return its output. Use this to run shell commands like 'make check', 'pkg-config --libs codeagent', 'git status', etc.");
    defs[4].input_schema_json = ca_strdup(bash_schema);
    defs[4].function = ca_tool_bash;
    defs[5].name = ca_strdup("code_search");
    defs[5].description = ca_strdup("Search for code patterns using ripgrep (rg). Use this to find code patterns, function definitions, variable usage, or any text in the codebase. You can filter by file type (e.g., 'rs', 'js', 'py').");
    defs[5].input_schema_json = ca_strdup(code_search_schema);
    defs[5].function = ca_tool_code_search;

    *tools = defs;
    *tool_count = 6;
    return CA_OK;
}

void ca_tool_definitions_free(ca_tool_definition *tools, size_t tool_count) {
    size_t i;
    if (tools == NULL) {
        return;
    }
    for (i = 0; i < tool_count; i++) {
        tool_definition_clear(&tools[i]);
    }
    free(tools);
}

ca_tool_registry *ca_tool_registry_new(void) {
    return (ca_tool_registry *)calloc(1, sizeof(ca_tool_registry));
}

void ca_tool_registry_free(ca_tool_registry *registry) {
    if (registry == NULL) {
        return;
    }
    ca_tool_definitions_free(registry->tools, registry->tool_count);
    free(registry);
}

ca_status ca_tool_registry_register(ca_tool_registry *registry,
                                    const char *name,
                                    const char *description,
                                    const char *input_schema_json,
                                    ca_tool_fn function,
                                    void *userdata) {
    ca_tool_definition *next;
    size_t i;
    if (registry == NULL || name == NULL || function == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    for (i = 0; i < registry->tool_count; i++) {
        if (registry->tools[i].name != NULL && strcmp(registry->tools[i].name, name) == 0) {
            return tool_definition_set(&registry->tools[i],
                                       name,
                                       description,
                                       input_schema_json,
                                       function,
                                       userdata);
        }
    }
    next = (ca_tool_definition *)realloc(registry->tools,
                                         sizeof(ca_tool_definition) * (registry->tool_count + 1));
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    registry->tools = next;
    memset(&registry->tools[registry->tool_count], 0, sizeof(registry->tools[registry->tool_count]));
    if (tool_definition_set(&registry->tools[registry->tool_count],
                            name,
                            description,
                            input_schema_json,
                            function,
                            userdata) != CA_OK) {
        return CA_NO_MEMORY;
    }
    registry->tool_count++;
    return CA_OK;
}

ca_status ca_tool_registry_add_builtin(ca_tool_registry *registry) {
    ca_tool_definition *tools = NULL;
    size_t tool_count = 0;
    size_t i;
    ca_status status;
    if (registry == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    status = ca_builtin_tools(&tools, &tool_count);
    if (status != CA_OK) {
        return status;
    }
    for (i = 0; i < tool_count; i++) {
        status = ca_tool_registry_register(registry,
                                           tools[i].name,
                                           tools[i].description,
                                           tools[i].input_schema_json,
                                           tools[i].function,
                                           tools[i].userdata);
        if (status != CA_OK) {
            ca_tool_definitions_free(tools, tool_count);
            return status;
        }
    }
    ca_tool_definitions_free(tools, tool_count);
    return CA_OK;
}

size_t ca_tool_registry_count(const ca_tool_registry *registry) {
    return registry == NULL ? 0 : registry->tool_count;
}

ca_status ca_tool_registry_definitions(const ca_tool_registry *registry,
                                       ca_tool_definition **tools,
                                       size_t *tool_count) {
    if (registry == NULL || tools == NULL || tool_count == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *tool_count = 0;
    if (tool_definitions_clone(registry->tools, registry->tool_count, tools) != CA_OK) {
        return CA_NO_MEMORY;
    }
    *tool_count = registry->tool_count;
    return CA_OK;
}

ca_status ca_tool_registry_execute(ca_tool_registry *registry,
                                   const char *name,
                                   const char *input_json,
                                   char **output) {
    if (registry == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    return ca_tool_execute(registry->tools, registry->tool_count, name, input_json, output);
}

ca_status ca_tool_execute(ca_tool_definition *tools,
                          size_t tool_count,
                          const char *name,
                          const char *input_json,
                          char **output) {
    size_t i;
    if (tools == NULL || name == NULL || output == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    for (i = 0; i < tool_count; i++) {
        if (tools[i].name != NULL && strcmp(tools[i].name, name) == 0) {
            if (tools[i].function == NULL) {
                return ca_set_error(output, "Tool has no function: %s", name);
            }
            return tools[i].function(input_json, output, tools[i].userdata);
        }
    }
    return ca_set_error(output, "Unknown tool: %s", name);
}
