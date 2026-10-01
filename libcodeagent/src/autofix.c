/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <ctype.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static int ensure_dir(const char *path) {
    char tmp[PATH_MAX];
    char *p;
    size_t len;
    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    len = strlen(path);
    if (len >= sizeof(tmp)) {
        return -1;
    }
    memcpy(tmp, path, len + 1);
    for (p = tmp + 1; *p != '\0'; p++) {
        if (*p == '/') {
            *p = '\0';
            if (mkdir(tmp, 0777) != 0 && errno != EEXIST) {
                return -1;
            }
            *p = '/';
        }
    }
    return mkdir(tmp, 0777) == 0 || errno == EEXIST ? 0 : -1;
}

static const char *find_line_with(const char *text, const char *needle) {
    const char *p = text;
    size_t needle_len = strlen(needle);
    while (p != NULL && *p != '\0') {
        const char *end = strchr(p, '\n');
        size_t len = end == NULL ? strlen(p) : (size_t)(end - p);
        if (len >= needle_len && strstr(p, needle) != NULL && strstr(p, needle) < p + len) {
            return p;
        }
        p = end == NULL ? NULL : end + 1;
    }
    return NULL;
}

static char *line_copy(const char *line) {
    const char *end;
    if (line == NULL) {
        return ca_strdup("");
    }
    end = strchr(line, '\n');
    return end == NULL ? ca_strdup(line) : ca_trim_copy(line);
}

ca_status ca_diagnostic_parse(const char *output, ca_diagnostic *diagnostic) {
    const char *line;
    char *raw;
    char *colon;
    if (output == NULL || diagnostic == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    memset(diagnostic, 0, sizeof(*diagnostic));
    line = find_line_with(output, "error");
    if (line == NULL) {
        line = find_line_with(output, "warning");
    }
    raw = line_copy(line == NULL ? output : line);
    if (raw == NULL) {
        return CA_NO_MEMORY;
    }
    diagnostic->raw = ca_strdup(output);
    diagnostic->severity = ca_contains(raw, "warning") ? ca_strdup("warning") : ca_strdup("error");
    diagnostic->message = ca_strdup(raw);
    colon = strstr(raw, ".c:");
    if (colon == NULL) {
        colon = strstr(raw, ".h:");
    }
    if (colon != NULL) {
        char *start = raw;
        char *file;
        while (start < colon && isspace((unsigned char)*start)) {
            start++;
        }
        file = (char *)calloc((size_t)(colon - start) + 4, 1);
        if (file != NULL) {
            size_t ext_len = colon[1] == 'c' ? 2u : 2u;
            memcpy(file, start, (size_t)(colon - start) + ext_len);
            diagnostic->file = file;
            diagnostic->line = (unsigned)strtoul(colon + ext_len + 1u, NULL, 10);
        }
    }
    if (ca_contains(raw, "undeclared identifier")) {
        diagnostic->code = ca_strdup("undeclared_identifier");
    } else if (ca_contains(raw, "No such file or directory") || ca_contains(raw, "file not found")) {
        diagnostic->code = ca_strdup("missing_header");
    } else if (ca_contains(raw, "implicit declaration")) {
        diagnostic->code = ca_strdup("implicit_declaration");
    }
    free(raw);
    if (diagnostic->raw == NULL || diagnostic->severity == NULL || diagnostic->message == NULL) {
        ca_diagnostic_free(diagnostic);
        return CA_NO_MEMORY;
    }
    return CA_OK;
}

void ca_diagnostic_free(ca_diagnostic *diagnostic) {
    if (diagnostic == NULL) {
        return;
    }
    free(diagnostic->severity);
    free(diagnostic->code);
    free(diagnostic->file);
    free(diagnostic->message);
    free(diagnostic->raw);
    memset(diagnostic, 0, sizeof(*diagnostic));
}

int ca_auto_fix_should_apply(const char *error_text) {
    if (error_text == NULL) {
        return 0;
    }
    if (ca_contains(error_text, "delete") || ca_contains(error_text, "remove file") ||
        ca_contains(error_text, "drop table") || ca_contains(error_text, "destructive")) {
        return 0;
    }
    return ca_contains(error_text, "No such file or directory") ||
           ca_contains(error_text, "file not found") ||
           ca_contains(error_text, "undeclared identifier") ||
           ca_contains(error_text, "implicit declaration") ||
           ca_contains(error_text, "missing dependency");
}

ca_status ca_fix_agent_suggest(const char *compiler_output, char **out_suggestion) {
    ca_diagnostic diagnostic;
    ca_string_builder sb;
    ca_status status;
    if (out_suggestion == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *out_suggestion = NULL;
    status = ca_diagnostic_parse(compiler_output, &diagnostic);
    if (status != CA_OK) {
        return status;
    }
    ca_sb_init(&sb);
    ca_sb_append(&sb, "FixAgent suggestion:\n");
    ca_sb_appendf(&sb, "  severity: %s\n", diagnostic.severity == NULL ? "unknown" : diagnostic.severity);
    if (diagnostic.code != NULL) {
        ca_sb_appendf(&sb, "  code: %s\n", diagnostic.code);
    }
    if (diagnostic.file != NULL) {
        ca_sb_appendf(&sb, "  file: %s\n", diagnostic.file);
    }
    if (ca_auto_fix_should_apply(compiler_output)) {
        ca_sb_append(&sb, "  action: safe auto-fix candidate; apply only dependency/import/value-scope edits.\n");
    } else {
        ca_sb_append(&sb, "  action: ask user before changing files.\n");
    }
    ca_sb_appendf(&sb, "  diagnostic: %s\n", diagnostic.message == NULL ? "" : diagnostic.message);
    ca_diagnostic_free(&diagnostic);
    *out_suggestion = ca_sb_take(&sb);
    return *out_suggestion == NULL ? CA_NO_MEMORY : CA_OK;
}

ca_status ca_regression_test_generate(const char *directory,
                                      const char *compiler_output,
                                      char **out_path) {
    ca_string_builder path;
    FILE *file;
    time_t now = time(NULL);
    if (directory == NULL || directory[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    if (out_path != NULL) {
        *out_path = NULL;
    }
    if (ensure_dir(directory) != 0) {
        return CA_IO_ERROR;
    }
    ca_sb_init(&path);
    ca_sb_appendf(&path, "%s/codeagent-regression-%ld.md", directory, (long)now);
    file = fopen(path.data, "wb");
    if (file == NULL) {
        ca_sb_free(&path);
        return CA_IO_ERROR;
    }
    fprintf(file,
            "# Regression Test Skeleton\n\n"
            "## Reproduced Diagnostic\n\n```text\n%s\n```\n\n"
            "## Verification\n\n- Re-run the failing command.\n- Confirm the diagnostic no longer appears.\n",
            compiler_output == NULL ? "" : compiler_output);
    fclose(file);
    if (out_path != NULL) {
        *out_path = ca_sb_take(&path);
    } else {
        ca_sb_free(&path);
    }
    return out_path == NULL || *out_path != NULL ? CA_OK : CA_NO_MEMORY;
}

ca_status ca_auto_fix_apply_safe(const char *project_dir,
                                 const char *compiler_output,
                                 char **out_summary) {
    ca_string_builder sb;
    (void)project_dir;
    if (out_summary == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    ca_sb_init(&sb);
    if (ca_auto_fix_should_apply(compiler_output)) {
        ca_sb_append(&sb,
                     "Safe auto-fix boundary matched. No broad rewrite was applied automatically; "
                     "dependency/import edits should be made through explicit tool calls after review.");
    } else {
        ca_sb_append(&sb, "Diagnostic is outside the auto-fix boundary; user approval required.");
    }
    *out_summary = ca_sb_take(&sb);
    return *out_summary == NULL ? CA_NO_MEMORY : CA_OK;
}
