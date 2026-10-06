/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "config.h"

#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ucl.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static char *expand_tilde(const char *path) {
    const char *home;
    size_t home_len;
    size_t rest_len;
    char *out;
    const char *rest;
    if (path == NULL) {
        return NULL;
    }
    if (path[0] != '~') {
        return ca_strdup(path);
    }
    home = getenv("HOME");
    if (home == NULL || home[0] == '\0') {
        return ca_strdup(path);
    }
    rest = path + 1;
    home_len = strlen(home);
    rest_len = strlen(rest);
    if (home_len > SIZE_MAX - rest_len - 1) {
        return NULL;
    }
    out = (char *)malloc(home_len + rest_len + 1);
    if (out == NULL) {
        return NULL;
    }
    memcpy(out, home, home_len);
    memcpy(out + home_len, rest, rest_len + 1);
    return out;
}

static int ensure_parent_dirs_for_file(const char *path) {
    char tmp[PATH_MAX];
    char *p;
    size_t len;
    if (path == NULL) {
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
    return 0;
}

static ca_status read_all(FILE *file, char **out) {
    char *data = NULL;
    size_t len = 0;
    size_t cap = 0;
    char buffer[4096];
    if (file == NULL || out == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *out = NULL;
    while (!feof(file)) {
        size_t n = fread(buffer, 1, sizeof(buffer), file);
        if (n > 0) {
            char *next;
            if (len > SIZE_MAX - n - 1) {
                free(data);
                return CA_NO_MEMORY;
            }
            if (len + n + 1 > cap) {
                cap = cap == 0 ? 8192 : cap * 2;
                while (cap < len + n + 1) {
                    if (cap > SIZE_MAX / 2) {
                        free(data);
                        return CA_NO_MEMORY;
                    }
                    cap *= 2;
                }
                next = (char *)realloc(data, cap);
                if (next == NULL) {
                    free(data);
                    return CA_NO_MEMORY;
                }
                data = next;
            }
            memcpy(data + len, buffer, n);
            len += n;
        }
        if (ferror(file)) {
            free(data);
            return CA_IO_ERROR;
        }
    }
    if (data == NULL) {
        data = ca_strdup("");
        if (data == NULL) {
            return CA_NO_MEMORY;
        }
    } else {
        data[len] = '\0';
    }
    *out = data;
    return CA_OK;
}

char *codeagentctl_default_config_path(void) {
    return expand_tilde("~/.config/coding-agent/config.ucl");
}

static ca_status write_default_config_file(const char *path) {
    FILE *file;
    if (path == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    if (ensure_parent_dirs_for_file(path) != 0) {
        return CA_IO_ERROR;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        return CA_IO_ERROR;
    }
    fputs("# codeagentctl UCL configuration\n"
          "provider = \"anthropic\";\n"
          "model {\n"
          "  default = \"claude-sonnet-4-20250514\";\n"
          "  available = [\"claude-sonnet-4-20250514\"];\n"
          "  max_tokens = 4096;\n"
          "  context_window = 200000;\n"
          "}\n"
          "behavior {\n"
          "  max_tool_iterations = 50;\n"
          "  fun_facts = true;\n"
          "  fun_fact_delay = 10;\n"
          "}\n"
          "persistence {\n"
          "  enabled = true;\n"
          "  path = \".specstory/history/\";\n"
          "}\n"
          "permissions {\n"
          "  auto_read = true;\n"
          "  trusted_paths = [];\n"
          "}\n"
          "sandbox {\n"
          "  mode = \"disabled\";\n"
          "  network = false;\n"
          "}\n"
          "integrations {\n"
          "  obsidian {\n"
          "    vault_path = \"\";\n"
          "  }\n"
          "}\n"
          "providers {\n"
          "  anthropic { }\n"
          "  openai { }\n"
          "  ollama-cloud { }\n"
          "  ollama-server { }\n"
          "  openai-compatible { }\n"
          "}\n",
          file);
    return fclose(file) == 0 ? CA_OK : CA_IO_ERROR;
}

static const ucl_object_t *lookup_any(const ucl_object_t *root, const char *a, const char *b) {
    const ucl_object_t *value = NULL;
    if (root == NULL) {
        return NULL;
    }
    if (a != NULL) {
        value = ucl_object_lookup_path(root, a);
    }
    if (value == NULL && b != NULL) {
        value = ucl_object_lookup_path(root, b);
    }
    return value;
}

static void set_string_from_ucl(ca_config *config,
                                const ucl_object_t *root,
                                const char *a,
                                const char *b,
                                ca_status (*setter)(ca_config *, const char *),
                                int expand) {
    const ucl_object_t *obj = lookup_any(root, a, b);
    const char *value;
    char *expanded = NULL;
    if (config == NULL || setter == NULL || obj == NULL) {
        return;
    }
    value = ucl_object_tostring(obj);
    if (value == NULL) {
        return;
    }
    if (expand) {
        expanded = expand_tilde(value);
        value = expanded;
    }
    if (value != NULL) {
        setter(config, value);
    }
    free(expanded);
}

static void set_bool_from_ucl(int *field, const ucl_object_t *root, const char *path) {
    const ucl_object_t *obj = lookup_any(root, path, NULL);
    if (field != NULL && obj != NULL) {
        *field = ucl_object_toboolean(obj) ? 1 : 0;
    }
}

static void set_uint_from_ucl(unsigned *field, const ucl_object_t *root, const char *path) {
    const ucl_object_t *obj = lookup_any(root, path, NULL);
    if (field != NULL && obj != NULL) {
        *field = (unsigned)ucl_object_toint(obj);
    }
}

static void set_size_from_ucl(size_t *field, const ucl_object_t *root, const char *path) {
    const ucl_object_t *obj = lookup_any(root, path, NULL);
    if (field != NULL && obj != NULL) {
        *field = (size_t)ucl_object_toint(obj);
    }
}

static void set_sandbox_mode_from_ucl(ca_config *config, const ucl_object_t *root) {
    const ucl_object_t *obj = lookup_any(root, "sandbox.mode", NULL);
    const char *value = obj == NULL ? NULL : ucl_object_tostring(obj);
    ca_sandbox_mode mode;
    if (config != NULL && value != NULL && ca_sandbox_mode_parse(value, &mode) == CA_OK) {
        ca_config_set_sandbox_mode(config, mode);
    }
}

static void load_trusted_paths(ca_config *config, const ucl_object_t *root) {
    const ucl_object_t *array = lookup_any(root, "permissions.trusted_paths", NULL);
    ucl_object_iter_t iter = NULL;
    const ucl_object_t *item;
    if (config == NULL || array == NULL) {
        return;
    }
    while ((item = ucl_object_iterate(array, &iter, true)) != NULL) {
        const char *path = ucl_object_tostring(item);
        char *expanded;
        if (path == NULL) {
            continue;
        }
        expanded = expand_tilde(path);
        if (expanded != NULL) {
            ca_config_add_trusted_path(config, expanded);
            free(expanded);
        }
    }
}

static void load_provider_option(ca_config *config,
                                 const ucl_object_t *root,
                                 const char *provider,
                                 const char *key) {
    char path[160];
    const ucl_object_t *obj;
    const char *value;
    snprintf(path, sizeof(path), "providers.%s.%s", provider, key);
    obj = ucl_object_lookup_path(root, path);
    if (obj == NULL) {
        snprintf(path, sizeof(path), "%s.%s", provider, key);
        obj = ucl_object_lookup_path(root, path);
    }
    value = obj == NULL ? NULL : ucl_object_tostring(obj);
    if (value != NULL) {
        ca_config_set_provider_option(config, provider, key, value);
    }
}

ca_status codeagentctl_config_load(ca_config *config, const char *path) {
    FILE *file;
    char *content = NULL;
    char *resolved_path;
    ca_status status;
    if (config == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    ca_config_init_defaults(config);
    resolved_path = path == NULL ? codeagentctl_default_config_path() : expand_tilde(path);
    if (resolved_path == NULL) {
        return CA_NO_MEMORY;
    }
    file = fopen(resolved_path, "rb");
    if (file == NULL) {
        status = write_default_config_file(resolved_path);
        free(resolved_path);
        return status;
    }
    status = read_all(file, &content);
    fclose(file);
    if (status != CA_OK) {
        free(resolved_path);
        return status;
    }

    {
        struct ucl_parser *parser = ucl_parser_new(UCL_PARSER_KEY_LOWERCASE);
        ucl_object_t *root;
        if (parser == NULL) {
            free(content);
            free(resolved_path);
            return CA_NO_MEMORY;
        }
        if (!ucl_parser_add_string(parser, content, strlen(content))) {
            ucl_parser_free(parser);
            free(content);
            free(resolved_path);
            return CA_JSON_ERROR;
        }
        root = ucl_parser_get_object(parser);
        if (root == NULL) {
            ucl_parser_free(parser);
            free(content);
            free(resolved_path);
            return CA_JSON_ERROR;
        }
        set_string_from_ucl(config, root, "provider", NULL, ca_config_set_provider, 0);
        set_string_from_ucl(config, root, "model.default", "model", ca_config_set_model, 0);
        set_uint_from_ucl(&config->max_tokens, root, "model.max_tokens");
        set_uint_from_ucl(&config->context_window, root, "model.context_window");
        set_size_from_ucl(&config->max_tool_iterations, root, "behavior.max_tool_iterations");
        set_bool_from_ucl(&config->fun_facts, root, "behavior.fun_facts");
        set_uint_from_ucl(&config->fun_fact_delay, root, "behavior.fun_fact_delay");
        set_bool_from_ucl(&config->persistence_enabled, root, "persistence.enabled");
        set_string_from_ucl(config, root, "persistence.path", "persistence_path", ca_config_set_persistence_path, 1);
        set_string_from_ucl(config, root, "integrations.obsidian.vault_path", "obsidian.vault_path", ca_config_set_obsidian_vault_path, 1);
        set_bool_from_ucl(&config->auto_read, root, "permissions.auto_read");
        set_sandbox_mode_from_ucl(config, root);
        set_bool_from_ucl(&config->sandbox_network, root, "sandbox.network");
        load_trusted_paths(config, root);
        load_provider_option(config, root, "anthropic", "api_key");
        load_provider_option(config, root, "openai", "api_key");
        load_provider_option(config, root, "openai", "base_url");
        load_provider_option(config, root, "openai", "model");
        load_provider_option(config, root, "ollama-cloud", "api_key");
        load_provider_option(config, root, "ollama-cloud", "base_url");
        load_provider_option(config, root, "ollama-cloud", "model");
        load_provider_option(config, root, "ollama-server", "api_key");
        load_provider_option(config, root, "ollama-server", "base_url");
        load_provider_option(config, root, "ollama-server", "model");
        load_provider_option(config, root, "openai-compatible", "api_key");
        load_provider_option(config, root, "openai-compatible", "base_url");
        load_provider_option(config, root, "openai-compatible", "model");
        ucl_object_unref(root);
        ucl_parser_free(parser);
    }
    status = CA_OK;
    free(content);
    free(resolved_path);
    return status;
}
