/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct ca_permission_decision {
    char *path;
    int write_access;
    ca_permission_result result;
    struct ca_permission_decision *next;
} ca_permission_decision;

struct ca_permission_checker {
    ca_config config;
    ca_permission_decision *decisions;
};

static void decisions_free(ca_permission_decision *decision) {
    while (decision != NULL) {
        ca_permission_decision *next = decision->next;
        free(decision->path);
        free(decision);
        decision = next;
    }
}

static ca_permission_decision *decision_find(ca_permission_checker *checker,
                                             const char *path,
                                             int write_access) {
    ca_permission_decision *decision;
    if (checker == NULL || path == NULL) {
        return NULL;
    }
    decision = checker->decisions;
    while (decision != NULL) {
        if (decision->write_access == write_access && strcmp(decision->path, path) == 0) {
            return decision;
        }
        decision = decision->next;
    }
    return NULL;
}

static ca_status decision_set(ca_permission_checker *checker,
                              const char *path,
                              int write_access,
                              ca_permission_result result) {
    ca_permission_decision *decision;
    if (checker == NULL || path == NULL || path[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    decision = decision_find(checker, path, write_access);
    if (decision != NULL) {
        decision->result = result;
        return CA_OK;
    }
    decision = (ca_permission_decision *)calloc(1, sizeof(ca_permission_decision));
    if (decision == NULL) {
        return CA_NO_MEMORY;
    }
    decision->path = ca_strdup(path);
    if (decision->path == NULL) {
        free(decision);
        return CA_NO_MEMORY;
    }
    decision->write_access = write_access;
    decision->result = result;
    decision->next = checker->decisions;
    checker->decisions = decision;
    return CA_OK;
}

ca_status ca_config_add_trusted_path(ca_config *config, const char *path) {
    char **next;
    if (config == NULL || path == NULL || path[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    next = (char **)realloc(config->trusted_paths,
                            sizeof(char *) * (config->trusted_path_count + 1));
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    config->trusted_paths = next;
    config->trusted_paths[config->trusted_path_count] = ca_strdup(path);
    if (config->trusted_paths[config->trusted_path_count] == NULL) {
        return CA_NO_MEMORY;
    }
    config->trusted_path_count++;
    return CA_OK;
}

ca_permission_result ca_permission_check_path(const ca_config *config,
                                              const char *path,
                                              int write_access) {
    size_t i;
    if (path == NULL || path[0] == '\0') {
        return CA_PERMISSION_DENY;
    }
    if (config == NULL) {
        return CA_PERMISSION_PROMPT;
    }
    for (i = 0; i < config->trusted_path_count; i++) {
        const char *trusted = config->trusted_paths[i];
        if (trusted != NULL && ca_starts_with(path, trusted)) {
            return CA_PERMISSION_ALLOW;
        }
    }
    if (!write_access && config->auto_read) {
        return CA_PERMISSION_ALLOW;
    }
    return CA_PERMISSION_PROMPT;
}

ca_permission_checker *ca_permission_checker_new(const ca_config *config) {
    ca_permission_checker *checker = (ca_permission_checker *)calloc(1, sizeof(ca_permission_checker));
    if (checker == NULL) {
        return NULL;
    }
    if (ca_config_clone(config, &checker->config) != CA_OK) {
        free(checker);
        return NULL;
    }
    return checker;
}

void ca_permission_checker_free(ca_permission_checker *checker) {
    if (checker == NULL) {
        return;
    }
    ca_config_free(&checker->config);
    decisions_free(checker->decisions);
    free(checker);
}

ca_permission_result ca_permission_checker_check(ca_permission_checker *checker,
                                                 const char *path,
                                                 int write_access) {
    ca_permission_decision *decision;
    if (checker == NULL) {
        return ca_permission_check_path(NULL, path, write_access);
    }
    decision = decision_find(checker, path, write_access);
    if (decision != NULL) {
        return decision->result;
    }
    return ca_permission_check_path(&checker->config, path, write_access);
}

ca_status ca_permission_checker_respond(ca_permission_checker *checker,
                                        ca_config *config,
                                        const char *path,
                                        int write_access,
                                        ca_permission_response response) {
    if (checker == NULL || path == NULL || path[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    switch (response) {
    case CA_PERMISSION_RESPONSE_ALLOW_ONCE:
        return decision_set(checker, path, write_access, CA_PERMISSION_ALLOW);
    case CA_PERMISSION_RESPONSE_DENY_ONCE:
        return decision_set(checker, path, write_access, CA_PERMISSION_DENY);
    case CA_PERMISSION_RESPONSE_ALLOW_ALWAYS:
        if (ca_config_add_trusted_path(&checker->config, path) != CA_OK) {
            return CA_NO_MEMORY;
        }
        if (config != NULL && ca_config_add_trusted_path(config, path) != CA_OK) {
            return CA_NO_MEMORY;
        }
        return decision_set(checker, path, write_access, CA_PERMISSION_ALLOW);
    case CA_PERMISSION_RESPONSE_DENY_ALWAYS:
        return decision_set(checker, path, write_access, CA_PERMISSION_DENY);
    case CA_PERMISSION_RESPONSE_CANCEL:
        return decision_set(checker, path, write_access, CA_PERMISSION_DENY);
    default:
        return CA_INVALID_ARGUMENT;
    }
}

ca_status ca_permission_response_parse(const char *text, ca_permission_response *response) {
    if (text == NULL || response == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }
    if (*text == '\0' || strcmp(text, "y") == 0 || strcmp(text, "Y") == 0 ||
        strcmp(text, "yes") == 0 || strcmp(text, "Yes") == 0) {
        *response = CA_PERMISSION_RESPONSE_ALLOW_ONCE;
        return CA_OK;
    }
    if (strcmp(text, "n") == 0 || strcmp(text, "N") == 0 ||
        strcmp(text, "no") == 0 || strcmp(text, "No") == 0) {
        *response = CA_PERMISSION_RESPONSE_DENY_ONCE;
        return CA_OK;
    }
    if (strcmp(text, "a") == 0 || strcmp(text, "A") == 0 ||
        strcmp(text, "always") == 0 || strcmp(text, "Always") == 0) {
        *response = CA_PERMISSION_RESPONSE_ALLOW_ALWAYS;
        return CA_OK;
    }
    if (strcmp(text, "never") == 0 || strcmp(text, "Never") == 0) {
        *response = CA_PERMISSION_RESPONSE_DENY_ALWAYS;
        return CA_OK;
    }
    if (strcmp(text, "q") == 0 || strcmp(text, "Q") == 0 ||
        strcmp(text, "cancel") == 0 || strcmp(text, "Cancel") == 0) {
        *response = CA_PERMISSION_RESPONSE_CANCEL;
        return CA_OK;
    }
    return CA_INVALID_ARGUMENT;
}
