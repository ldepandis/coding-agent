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

static const char *note_type_name(ca_note_type type) {
    switch (type) {
        case CA_NOTE_MEETING: return "Meeting";
        case CA_NOTE_CONCEPT: return "Concept";
        case CA_NOTE_REFERENCE: return "Reference";
        case CA_NOTE_GENERAL: return "General";
    }
    return "General";
}

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

static char *slug_title(const char *title) {
    ca_string_builder sb;
    const char *p;
    int last_dash = 0;
    ca_sb_init(&sb);
    for (p = title == NULL ? "Untitled" : title; *p != '\0'; p++) {
        unsigned char ch = (unsigned char)*p;
        if (isalnum(ch)) {
            char lower[2];
            lower[0] = (char)tolower(ch);
            lower[1] = '\0';
            ca_sb_append(&sb, lower);
            last_dash = 0;
        } else if (!last_dash) {
            ca_sb_append(&sb, "-");
            last_dash = 1;
        }
    }
    while (sb.len > 0 && sb.data[sb.len - 1] == '-') {
        sb.data[--sb.len] = '\0';
    }
    if (sb.len == 0) {
        ca_sb_append(&sb, "untitled");
    }
    return ca_sb_take(&sb);
}

ca_status ca_obsidian_write_note(const char *vault_path,
                                 const char *title,
                                 const char *body,
                                 const ca_note_metadata *metadata,
                                 char **out_path) {
    ca_string_builder path;
    ca_string_builder content;
    char *slug;
    FILE *file;
    size_t i;
    time_t now;
    struct tm *tmv;
    char date[64];
    ca_note_type type = metadata == NULL ? CA_NOTE_GENERAL : metadata->note_type;
    if (vault_path == NULL || vault_path[0] == '\0' || title == NULL || title[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    if (out_path != NULL) {
        *out_path = NULL;
    }
    slug = slug_title(title);
    if (slug == NULL) {
        return CA_NO_MEMORY;
    }
    ca_sb_init(&path);
    ca_sb_append(&path, vault_path);
    if (vault_path[strlen(vault_path) - 1] != '/') {
        ca_sb_append(&path, "/");
    }
    ca_sb_append(&path, "Notes");
    if (ensure_dir(path.data) != 0) {
        ca_sb_free(&path);
        free(slug);
        return CA_IO_ERROR;
    }
    ca_sb_append(&path, "/");
    ca_sb_append(&path, slug);
    ca_sb_append(&path, ".md");
    free(slug);

    now = time(NULL);
    tmv = localtime(&now);
    if (tmv != NULL) {
        strftime(date, sizeof(date), "%Y-%m-%d %H:%M", tmv);
    } else {
        snprintf(date, sizeof(date), "unknown");
    }

    ca_sb_init(&content);
    ca_sb_append(&content, "---\n");
    ca_sb_appendf(&content, "created: %s\n", date);
    ca_sb_appendf(&content, "type: %s\n", note_type_name(type));
    if (metadata != NULL && metadata->tag_count > 0) {
        ca_sb_append(&content, "tags:\n");
        for (i = 0; i < metadata->tag_count; i++) {
            ca_sb_appendf(&content, "  - %s\n", metadata->tags[i]);
        }
    }
    if (metadata != NULL && metadata->related_count > 0) {
        ca_sb_append(&content, "related:\n");
        for (i = 0; i < metadata->related_count; i++) {
            ca_sb_appendf(&content, "  - \"[[%s]]\"\n", metadata->related[i]);
        }
    }
    ca_sb_append(&content, "---\n\n# ");
    ca_sb_append(&content, title);
    ca_sb_append(&content, "\n\n");
    ca_sb_append(&content, body == NULL ? "" : body);
    ca_sb_append(&content, "\n");

    file = fopen(path.data, "wb");
    if (file == NULL) {
        ca_sb_free(&path);
        ca_sb_free(&content);
        return CA_IO_ERROR;
    }
    if (fwrite(content.data, 1, content.len, file) != content.len) {
        fclose(file);
        ca_sb_free(&path);
        ca_sb_free(&content);
        return CA_IO_ERROR;
    }
    fclose(file);
    ca_sb_free(&content);
    if (out_path != NULL) {
        *out_path = ca_sb_take(&path);
    } else {
        ca_sb_free(&path);
    }
    return out_path == NULL || *out_path != NULL ? CA_OK : CA_NO_MEMORY;
}
