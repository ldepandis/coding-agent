/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "config.h"

#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT_TRUE(expr)                                                                             \
    do {                                                                                              \
        if (!(expr)) {                                                                                \
            fprintf(stderr, "assertion failed at %s:%d: %s\n", __FILE__, __LINE__, #expr);           \
            return 1;                                                                                 \
        }                                                                                             \
    } while (0)

static int write_test_file(const char *path, const char *content) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        return 1;
    }
    if (fwrite(content, 1, strlen(content), file) != strlen(content)) {
        fclose(file);
        return 1;
    }
    return fclose(file) == 0 ? 0 : 1;
}

static int test_config_load_ucl_sections(void) {
    ca_config config;
    const char *path = "/tmp/codeagentctl-config-test.ucl";
    ASSERT_TRUE(write_test_file(path,
                                "provider = \"openai\";\n"
                                "model { default = \"gpt-4.1-mini\"; max_tokens = 1234; context_window = 9999; }\n"
                                "behavior { max_tool_iterations = 7; fun_facts = false; fun_fact_delay = 3; }\n"
                                "persistence { enabled = true; path = \"~/codeagent-history\"; }\n"
                                "permissions { auto_read = false; trusted_paths = [\"/tmp/trusted-a\", \"~/trusted-b\"]; }\n"
                                "sandbox { mode = \"workspace-write\"; network = true; }\n"
                                "providers { openai { base_url = \"http://example.test/v1\"; model = \"o-test\"; } }\n") == 0);
    ASSERT_TRUE(codeagentctl_config_load(&config, path) == CA_OK);
    ASSERT_TRUE(strcmp(config.provider, "openai") == 0);
    ASSERT_TRUE(strcmp(config.model, "gpt-4.1-mini") == 0);
    ASSERT_TRUE(config.max_tokens == 1234u);
    ASSERT_TRUE(config.context_window == 9999u);
    ASSERT_TRUE(config.max_tool_iterations == 7u);
    ASSERT_TRUE(config.fun_facts == 0);
    ASSERT_TRUE(config.fun_fact_delay == 3u);
    ASSERT_TRUE(config.persistence_enabled == 1);
    ASSERT_TRUE(strstr(config.persistence_path, "codeagent-history") != NULL);
    ASSERT_TRUE(config.auto_read == 0);
    ASSERT_TRUE(config.trusted_path_count == 2);
    ASSERT_TRUE(config.sandbox_mode == CA_SANDBOX_WORKSPACE_WRITE);
    ASSERT_TRUE(config.sandbox_network == 1);
    ASSERT_TRUE(strcmp(ca_config_provider_option(&config, "openai", "base_url"), "http://example.test/v1") == 0);
    ASSERT_TRUE(strcmp(ca_config_provider_option(&config, "openai", "model"), "o-test") == 0);
    ca_config_free(&config);
    remove(path);
    return 0;
}

static int test_config_load_creates_default(void) {
    ca_config config;
    char dir[128];
    char path[180];
    snprintf(dir, sizeof(dir), "/tmp/codeagentctl-config-dir-%ld", (long)getpid());
    snprintf(path, sizeof(path), "%s/config.ucl", dir);
    ASSERT_TRUE(codeagentctl_config_load(&config, path) == CA_OK);
    ASSERT_TRUE(access(path, F_OK) == 0);
    ASSERT_TRUE(config.provider != NULL);
    ca_config_free(&config);
    remove(path);
    rmdir(dir);
    return 0;
}

int main(void) {
    ASSERT_TRUE(test_config_load_ucl_sections() == 0);
    ASSERT_TRUE(test_config_load_creates_default() == 0);
    puts("codeagentctl config tests passed");
    return 0;
}
