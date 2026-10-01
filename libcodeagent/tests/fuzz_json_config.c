/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "codeagent/codeagent.h"
#include "internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(void) {
    const char *samples[] = {
        "",
        "{",
        "{\"path\":\"/tmp/x\"}",
        "{\"path\":123}",
        "{\"command\":\"printf hi\"}",
        "{\"pattern\":\"needle\",\"path\":\".\"}",
        "[[[[[",
        "{\"unterminated\":\"value",
        "{\"path\":\"\\xff\\xfe\"}"
    };
    size_t i;
    for (i = 0; i < sizeof(samples) / sizeof(samples[0]); i++) {
        char *value = NULL;
        int bool_value = 0;
        ca_json_get_string(samples[i], "path", &value);
        free(value);
        ca_json_get_bool(samples[i], "flag", 0, &bool_value);
        ca_estimate_tokens_text(samples[i]);
    }
    puts("codeagent fuzz smoke passed");
    return 0;
}
