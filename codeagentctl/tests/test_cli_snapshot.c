/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define ASSERT_TRUE(expr)                                                                             \
    do {                                                                                              \
        if (!(expr)) {                                                                                \
            fprintf(stderr, "assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #expr);           \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

static int read_file(const char *path, char *buffer, size_t size) {
    FILE *file = fopen(path, "rb");
    size_t n;
    if (file == NULL) {
        return 1;
    }
    n = fread(buffer, 1, size - 1, file);
    buffer[n] = '\0';
    fclose(file);
    return 0;
}

int main(void) {
    char path[128];
    char buffer[4096];
    FILE *file;
    int saved_stdout;
    snprintf(path, sizeof(path), "/tmp/codeagentctl-snapshot-%ld.txt", (long)getpid());
    fflush(stdout);
    saved_stdout = dup(fileno(stdout));
    ASSERT_TRUE(saved_stdout >= 0);
    file = freopen(path, "wb", stdout);
    ASSERT_TRUE(file != NULL);
    codeagentctl_print_context_bar(50, 100);
    codeagentctl_print_progress("agents", 25);
    fflush(stdout);
    dup2(saved_stdout, fileno(stdout));
    close(saved_stdout);
    ASSERT_TRUE(read_file(path, buffer, sizeof(buffer)) == 0);
    ASSERT_TRUE(strstr(buffer, "50% (50/100 tokens)") != NULL);
    ASSERT_TRUE(strstr(buffer, "agents") != NULL);
    ASSERT_TRUE(strstr(buffer, "25%") != NULL);
    remove(path);
    puts("codeagentctl snapshot tests passed");
    return 0;
}
