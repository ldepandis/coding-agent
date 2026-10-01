/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct ca_provider_option {
    char *provider;
    char *key;
    char *value;
    struct ca_provider_option *next;
} ca_provider_option;

static void provider_options_free(ca_provider_option *option) {
    while (option != NULL) {
        ca_provider_option *next = option->next;
        free(option->provider);
        free(option->key);
        free(option->value);
        free(option);
        option = next;
    }
}

static ca_provider_option *provider_option_find(ca_config *config,
                                                const char *provider,
                                                const char *key) {
    ca_provider_option *option;
    if (config == NULL || provider == NULL || key == NULL) {
        return NULL;
    }
    option = (ca_provider_option *)config->provider_options;
    while (option != NULL) {
        if (strcmp(option->provider, provider) == 0 && strcmp(option->key, key) == 0) {
            return option;
        }
        option = option->next;
    }
    return NULL;
}

static ca_status provider_options_clone(const ca_config *src, ca_config *dst) {
    const ca_provider_option *option;
    option = src == NULL ? NULL : (const ca_provider_option *)src->provider_options;
    while (option != NULL) {
        ca_status status = ca_config_set_provider_option(dst, option->provider, option->key, option->value);
        if (status != CA_OK) {
            return status;
        }
        option = option->next;
    }
    return CA_OK;
}

void ca_config_init_defaults(ca_config *config) {
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->provider = ca_strdup(CODEAGENT_DEFAULT_PROVIDER);
    config->model = ca_strdup(CODEAGENT_DEFAULT_MODEL);
    config->max_tokens = 4096u;
    config->context_window = 200000u;
    config->max_tool_iterations = 50u;
    config->persistence_enabled = 1;
    config->persistence_path = ca_strdup(".specstory/history/");
    config->auto_read = 1;
    config->fun_facts = 1;
    config->fun_fact_delay = 10u;
    config->max_llm_retries = CODEAGENT_MAX_RETRIES;
    config->llm_retry_delay_ms = CODEAGENT_RETRY_DELAY_MS;
    config->max_tool_retries = CODEAGENT_MAX_RETRIES;
    config->tool_retry_delay_ms = CODEAGENT_RETRY_DELAY_MS;
    config->provider_timeout_ms = 120000u;
}

void ca_config_free(ca_config *config) {
    size_t i;
    if (config == NULL) {
        return;
    }
    free(config->provider);
    free(config->model);
    free(config->persistence_path);
    free(config->obsidian_vault_path);
    provider_options_free((ca_provider_option *)config->provider_options);
    if (config->trusted_paths != NULL) {
        for (i = 0; i < config->trusted_path_count; i++) {
            free(config->trusted_paths[i]);
        }
    }
    free(config->trusted_paths);
    memset(config, 0, sizeof(*config));
}

ca_status ca_config_clone(const ca_config *src, ca_config *dst) {
    size_t i;
    ca_config_init_defaults(dst);
    if (src == NULL) {
        return CA_OK;
    }
    free(dst->provider);
    free(dst->model);
    free(dst->persistence_path);
    free(dst->obsidian_vault_path);
    provider_options_free((ca_provider_option *)dst->provider_options);
    dst->provider_options = NULL;
    dst->provider = ca_strdup(src->provider);
    dst->model = ca_strdup(src->model);
    dst->max_tokens = src->max_tokens;
    dst->context_window = src->context_window;
    dst->max_tool_iterations = src->max_tool_iterations;
    dst->persistence_enabled = src->persistence_enabled;
    dst->persistence_path = ca_strdup(src->persistence_path);
    dst->auto_read = src->auto_read;
    dst->fun_facts = src->fun_facts;
    dst->fun_fact_delay = src->fun_fact_delay;
    dst->obsidian_vault_path = ca_strdup(src->obsidian_vault_path == NULL ? "" : src->obsidian_vault_path);
    dst->max_llm_retries = src->max_llm_retries;
    dst->llm_retry_delay_ms = src->llm_retry_delay_ms;
    dst->max_tool_retries = src->max_tool_retries;
    dst->tool_retry_delay_ms = src->tool_retry_delay_ms;
    dst->provider_timeout_ms = src->provider_timeout_ms;
    if (dst->provider == NULL || dst->model == NULL || dst->persistence_path == NULL ||
        dst->obsidian_vault_path == NULL) {
        ca_config_free(dst);
        return CA_NO_MEMORY;
    }
    if (src->trusted_path_count > 0) {
        dst->trusted_paths = (char **)calloc(src->trusted_path_count, sizeof(char *));
        if (dst->trusted_paths == NULL) {
            ca_config_free(dst);
            return CA_NO_MEMORY;
        }
        dst->trusted_path_count = src->trusted_path_count;
        for (i = 0; i < src->trusted_path_count; i++) {
            dst->trusted_paths[i] = ca_strdup(src->trusted_paths[i]);
            if (dst->trusted_paths[i] == NULL) {
                ca_config_free(dst);
                return CA_NO_MEMORY;
            }
        }
    }
    if (provider_options_clone(src, dst) != CA_OK) {
        ca_config_free(dst);
        return CA_NO_MEMORY;
    }
    return CA_OK;
}

ca_status ca_config_set_provider(ca_config *config, const char *provider_name) {
    char *next;
    if (config == NULL || provider_name == NULL || provider_name[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    next = ca_strdup(provider_name);
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    free(config->provider);
    config->provider = next;
    return CA_OK;
}

ca_status ca_config_set_model(ca_config *config, const char *model) {
    char *next;
    if (config == NULL || model == NULL || model[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    next = ca_strdup(model);
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    free(config->model);
    config->model = next;
    return CA_OK;
}

ca_status ca_config_set_persistence_path(ca_config *config, const char *path) {
    char *next;
    if (config == NULL || path == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    next = ca_strdup(path);
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    free(config->persistence_path);
    config->persistence_path = next;
    return CA_OK;
}

ca_status ca_config_set_obsidian_vault_path(ca_config *config, const char *path) {
    char *next;
    if (config == NULL || path == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    next = ca_strdup(path);
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    free(config->obsidian_vault_path);
    config->obsidian_vault_path = next;
    return CA_OK;
}

ca_status ca_config_set_provider_option(ca_config *config,
                                        const char *provider,
                                        const char *key,
                                        const char *value) {
    ca_provider_option *option;
    char *next_value;
    if (config == NULL || provider == NULL || provider[0] == '\0' ||
        key == NULL || key[0] == '\0' || value == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    option = provider_option_find(config, provider, key);
    next_value = ca_strdup(value);
    if (next_value == NULL) {
        return CA_NO_MEMORY;
    }
    if (option != NULL) {
        free(option->value);
        option->value = next_value;
        return CA_OK;
    }
    option = (ca_provider_option *)calloc(1, sizeof(ca_provider_option));
    if (option == NULL) {
        free(next_value);
        return CA_NO_MEMORY;
    }
    option->provider = ca_strdup(provider);
    option->key = ca_strdup(key);
    option->value = next_value;
    if (option->provider == NULL || option->key == NULL) {
        free(option->provider);
        free(option->key);
        free(option->value);
        free(option);
        return CA_NO_MEMORY;
    }
    option->next = (ca_provider_option *)config->provider_options;
    config->provider_options = option;
    return CA_OK;
}

const char *ca_config_provider_option(const ca_config *config,
                                      const char *provider,
                                      const char *key) {
    ca_provider_option *option;
    if (config == NULL || provider == NULL || key == NULL) {
        return NULL;
    }
    option = (ca_provider_option *)config->provider_options;
    while (option != NULL) {
        if (strcmp(option->provider, provider) == 0 && strcmp(option->key, key) == 0) {
            return option->value;
        }
        option = option->next;
    }
    return NULL;
}

const char *ca_config_provider_option_value(const ca_config *config,
                                            const char *provider,
                                            const char *key,
                                            const char *env_name,
                                            const char *fallback) {
    const char *value = ca_config_provider_option(config, provider, key);
    if (value != NULL && value[0] != '\0') {
        return value;
    }
    if (env_name != NULL && env_name[0] != '\0') {
        value = getenv(env_name);
        if (value != NULL && value[0] != '\0') {
            return value;
        }
    }
    return fallback;
}
