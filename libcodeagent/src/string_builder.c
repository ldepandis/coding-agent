/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

char *ca_strdup(const char *value) {
    size_t len;
    char *copy;

    if (value == NULL) {
        value = "";
    }

    len = strlen(value);
    copy = (char *)malloc(len + 1);
    if (copy == NULL) {
        return NULL;
    }
    memcpy(copy, value, len + 1);
    return copy;
}

void ca_free(void *ptr) {
    free(ptr);
}

ca_api_version ca_version(void) {
    ca_api_version version;
    version.major = CODEAGENT_VERSION_MAJOR;
    version.minor = CODEAGENT_VERSION_MINOR;
    version.patch = CODEAGENT_VERSION_PATCH;
    version.abi = CODEAGENT_ABI_VERSION;
    return version;
}

const char *ca_version_string(void) {
    return CODEAGENT_VERSION;
}

unsigned ca_abi_version(void) {
    return CODEAGENT_ABI_VERSION;
}

void ca_sb_init(ca_string_builder *sb) {
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

void ca_sb_free(ca_string_builder *sb) {
    if (sb == NULL) {
        return;
    }
    free(sb->data);
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
}

static ca_status ca_sb_reserve(ca_string_builder *sb, size_t extra) {
    size_t need;
    size_t next;
    char *data;

    if (extra > ((size_t)-1) - sb->len - 1) {
        return CA_NO_MEMORY;
    }
    need = sb->len + extra + 1;
    if (need <= sb->cap) {
        return CA_OK;
    }

    next = sb->cap == 0 ? 128 : sb->cap;
    while (next < need) {
        if (next > ((size_t)-1) / 2) {
            next = need;
            break;
        }
        next *= 2;
    }

    data = (char *)realloc(sb->data, next);
    if (data == NULL) {
        return CA_NO_MEMORY;
    }
    sb->data = data;
    sb->cap = next;
    return CA_OK;
}

ca_status ca_sb_append_n(ca_string_builder *sb, const char *text, size_t len) {
    ca_status status;

    if (sb == NULL || text == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    status = ca_sb_reserve(sb, len);
    if (status != CA_OK) {
        return status;
    }
    memcpy(sb->data + sb->len, text, len);
    sb->len += len;
    sb->data[sb->len] = '\0';
    return CA_OK;
}

ca_status ca_sb_append(ca_string_builder *sb, const char *text) {
    if (text == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    return ca_sb_append_n(sb, text, strlen(text));
}

ca_status ca_sb_appendf(ca_string_builder *sb, const char *fmt, ...) {
    va_list args;
    va_list copy;
    int needed;
    ca_status status;

    va_start(args, fmt);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);

    if (needed < 0) {
        va_end(args);
        return CA_ERROR;
    }

    status = ca_sb_reserve(sb, (size_t)needed);
    if (status != CA_OK) {
        va_end(args);
        return status;
    }

    vsnprintf(sb->data + sb->len, sb->cap - sb->len, fmt, args);
    va_end(args);
    sb->len += (size_t)needed;
    return CA_OK;
}

char *ca_sb_take(ca_string_builder *sb) {
    char *data = sb->data;
    if (data == NULL) {
        data = ca_strdup("");
    }
    sb->data = NULL;
    sb->len = 0;
    sb->cap = 0;
    return data;
}

ca_status ca_set_error(char **out, const char *fmt, ...) {
    va_list args;
    va_list copy;
    int needed;

    if (out == NULL) {
        return CA_ERROR;
    }
    *out = NULL;

    va_start(args, fmt);
    va_copy(copy, args);
    needed = vsnprintf(NULL, 0, fmt, copy);
    va_end(copy);
    if (needed < 0) {
        va_end(args);
        return CA_ERROR;
    }
    if ((size_t)needed == SIZE_MAX) {
        va_end(args);
        return CA_NO_MEMORY;
    }
    *out = (char *)malloc((size_t)needed + 1);
    if (*out == NULL) {
        va_end(args);
        return CA_NO_MEMORY;
    }
    vsnprintf(*out, (size_t)needed + 1, fmt, args);
    va_end(args);
    return CA_ERROR;
}

ca_status ca_read_all(FILE *file, char **out, size_t *out_len, size_t max_bytes) {
    ca_string_builder sb;
    char buffer[4096];
    size_t total = 0;

    if (file == NULL || out == NULL) {
        return CA_INVALID_ARGUMENT;
    }

    ca_sb_init(&sb);
    for (;;) {
        size_t to_read = sizeof(buffer);
        size_t n;
        if (max_bytes > 0 && total > max_bytes) {
            ca_sb_free(&sb);
            return CA_NO_MEMORY;
        }
        if (max_bytes > 0 && to_read > max_bytes - total) {
            to_read = max_bytes - total;
        }
        if (to_read == 0) {
            break;
        }
        n = fread(buffer, 1, to_read, file);
        if (n > 0) {
            ca_status status = ca_sb_append_n(&sb, buffer, n);
            if (status != CA_OK) {
                ca_sb_free(&sb);
                return status;
            }
            total += n;
        }
        if (n < to_read) {
            if (ferror(file)) {
                ca_sb_free(&sb);
                return CA_IO_ERROR;
            }
            break;
        }
    }

    *out = ca_sb_take(&sb);
    if (out_len != NULL) {
        *out_len = total;
    }
    return CA_OK;
}

char *ca_trim_copy(const char *text) {
    const char *start;
    const char *end;

    if (text == NULL) {
        return ca_strdup("");
    }
    start = text;
    while (*start == ' ' || *start == '\t' || *start == '\n' || *start == '\r') {
        start++;
    }
    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\n' || end[-1] == '\r')) {
        end--;
    }
    {
        size_t len = (size_t)(end - start);
        char *copy = (char *)malloc(len + 1);
        if (copy == NULL) {
            return NULL;
        }
        memcpy(copy, start, len);
        copy[len] = '\0';
        return copy;
    }
}

int ca_starts_with(const char *text, const char *prefix) {
    size_t len;
    if (text == NULL || prefix == NULL) {
        return 0;
    }
    len = strlen(prefix);
    return strncmp(text, prefix, len) == 0;
}

int ca_contains(const char *text, const char *needle) {
    if (text == NULL || needle == NULL) {
        return 0;
    }
    return strstr(text, needle) != NULL;
}

void ca_sleep_ms(unsigned delay_ms) {
#ifdef _WIN32
    Sleep(delay_ms);
#else
    struct timespec req;
    req.tv_sec = delay_ms / 1000u;
    req.tv_nsec = (long)(delay_ms % 1000u) * 1000000L;
    while (nanosleep(&req, &req) != 0 && errno == EINTR) {
    }
#endif
}
