/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#ifndef CODEAGENT_INTERNAL_H
#define CODEAGENT_INTERNAL_H

#include "codeagent/codeagent.h"

#include <stdarg.h>
#include <stdio.h>

typedef struct ca_string_builder {
    char *data;
    size_t len;
    size_t cap;
} ca_string_builder;

void ca_sb_init(ca_string_builder *sb);
void ca_sb_free(ca_string_builder *sb);
ca_status ca_sb_append(ca_string_builder *sb, const char *text);
ca_status ca_sb_append_n(ca_string_builder *sb, const char *text, size_t len);
ca_status ca_sb_appendf(ca_string_builder *sb, const char *fmt, ...);
char *ca_sb_take(ca_string_builder *sb);

ca_status ca_set_error(char **out, const char *fmt, ...);
ca_status ca_read_all(FILE *file, char **out, size_t *out_len, size_t max_bytes);
char *ca_trim_copy(const char *text);
int ca_starts_with(const char *text, const char *prefix);
int ca_contains(const char *text, const char *needle);
void ca_sleep_ms(unsigned delay_ms);

ca_status ca_json_get_string(const char *json, const char *key, char **value);
ca_status ca_json_get_bool(const char *json, const char *key, int default_value, int *value);
char *ca_json_escape(const char *text);

const char *ca_config_provider_option_value(const ca_config *config,
                                            const char *provider,
                                            const char *key,
                                            const char *env_name,
                                            const char *fallback);

#endif
