/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

static const char *skip_ws(const char *p) {
    while (*p != '\0' && isspace((unsigned char)*p)) {
        p++;
    }
    return p;
}

static const char *json_string_end(const char *p) {
    p++;
    while (*p != '\0') {
        if (*p == '\\') {
            p++;
            if (*p == '\0') {
                return NULL;
            }
            p++;
            continue;
        }
        if (*p == '"') {
            return p;
        }
        p++;
    }
    return NULL;
}

static int json_key_matches(const char *start, const char *end, const char *key) {
    size_t key_len;
    if (start == NULL || end == NULL || key == NULL) {
        return 0;
    }
    key_len = strlen(key);
    return (size_t)(end - start) == key_len && memcmp(start, key, key_len) == 0;
}

static int is_json_token_delimiter(char c) {
    return c == '\0' || c == ',' || c == '}' || c == ']' || isspace((unsigned char)c);
}

static const char *find_json_key(const char *json, const char *key) {
    const char *p;

    if (json == NULL || key == NULL) {
        return NULL;
    }

    p = json;
    while ((p = strchr(p, '"')) != NULL) {
        const char *start = p + 1;
        const char *end = json_string_end(p);
        const char *after;

        if (end == NULL) {
            return NULL;
        }

        after = skip_ws(end + 1);
        if (*after == ':' && json_key_matches(start, end, key)) {
            return skip_ws(after + 1);
        }
        p = end + 1;
    }
    return NULL;
}

char *ca_json_escape(const char *text) {
    ca_string_builder sb;
    const unsigned char *p;

    if (text == NULL || text[0] == '\0') {
        return ca_strdup("");
    }

    ca_sb_init(&sb);
    for (p = (const unsigned char *)text; *p != '\0'; p++) {
        char buf[8];
        switch (*p) {
        case '"':
            ca_sb_append(&sb, "\\\"");
            break;
        case '\\':
            ca_sb_append(&sb, "\\\\");
            break;
        case '\b':
            ca_sb_append(&sb, "\\b");
            break;
        case '\f':
            ca_sb_append(&sb, "\\f");
            break;
        case '\n':
            ca_sb_append(&sb, "\\n");
            break;
        case '\r':
            ca_sb_append(&sb, "\\r");
            break;
        case '\t':
            ca_sb_append(&sb, "\\t");
            break;
        default:
            if (*p < 0x20) {
                snprintf(buf, sizeof(buf), "\\u%04x", *p);
                ca_sb_append(&sb, buf);
            } else {
                ca_sb_append_n(&sb, (const char *)p, 1);
            }
            break;
        }
    }
    return ca_sb_take(&sb);
}

ca_status ca_json_get_string(const char *json, const char *key, char **value) {
    const char *p;
    ca_string_builder sb;

    if (json == NULL || key == NULL || value == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *value = NULL;
    p = find_json_key(json, key);
    if (p == NULL) {
        return CA_NOT_FOUND;
    }
    if (*p != '"') {
        return CA_JSON_ERROR;
    }
    p++;
    ca_sb_init(&sb);
    while (*p != '\0' && *p != '"') {
        if (*p == '\\') {
            p++;
            switch (*p) {
            case 'n':
                ca_sb_append_n(&sb, "\n", 1);
                break;
            case 'r':
                ca_sb_append_n(&sb, "\r", 1);
                break;
            case 't':
                ca_sb_append_n(&sb, "\t", 1);
                break;
            case '"':
            case '\\':
            case '/':
                ca_sb_append_n(&sb, p, 1);
                break;
            default:
                ca_sb_append_n(&sb, p, 1);
                break;
            }
            if (*p != '\0') {
                p++;
            }
        } else {
            ca_sb_append_n(&sb, p, 1);
            p++;
        }
    }
    if (*p != '"') {
        ca_sb_free(&sb);
        return CA_JSON_ERROR;
    }
    *value = ca_sb_take(&sb);
    return *value == NULL ? CA_NO_MEMORY : CA_OK;
}

ca_status ca_json_get_bool(const char *json, const char *key, int default_value, int *value) {
    const char *p;
    if (json == NULL || key == NULL || value == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    p = find_json_key(json, key);
    if (p == NULL) {
        *value = default_value;
        return CA_OK;
    }
    if (strncmp(p, "true", 4) == 0 && is_json_token_delimiter(p[4])) {
        *value = 1;
        return CA_OK;
    }
    if (strncmp(p, "false", 5) == 0 && is_json_token_delimiter(p[5])) {
        *value = 0;
        return CA_OK;
    }
    return CA_JSON_ERROR;
}
