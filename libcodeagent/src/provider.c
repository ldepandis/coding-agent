/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "provider.h"

#include <string.h>

#define CA_MAX_REGISTERED_PROVIDERS 16u

static ca_provider registered_providers[CA_MAX_REGISTERED_PROVIDERS];
static size_t registered_provider_count;

static const ca_provider *builtin_provider_at(size_t index) {
    switch (index) {
    case 0:
        return ca_anthropic_provider();
    case 1:
        return ca_openai_provider();
    case 2:
        return ca_ollama_cloud_provider();
    case 3:
        return ca_ollama_server_provider();
    case 4:
        return ca_openai_compatible_provider();
    default:
        return NULL;
    }
}

static const ca_provider *find_registered_provider(const char *name) {
    size_t i;
    for (i = 0; i < registered_provider_count; i++) {
        if (registered_providers[i].name != NULL && strcmp(registered_providers[i].name, name) == 0) {
            return &registered_providers[i];
        }
    }
    return NULL;
}

static const ca_provider *find_builtin_provider(const char *name) {
    size_t i;
    for (i = 0;; i++) {
        const ca_provider *provider = builtin_provider_at(i);
        if (provider == NULL) {
            break;
        }
        if (provider->name != NULL && strcmp(provider->name, name) == 0) {
            return provider;
        }
    }
    return NULL;
}

ca_status ca_provider_register(const ca_provider *provider) {
    if (provider == NULL || provider->name == NULL || provider->name[0] == '\0' || provider->call == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    if (registered_provider_count >= CA_MAX_REGISTERED_PROVIDERS) {
        return CA_ERROR;
    }
    registered_providers[registered_provider_count] = *provider;
    registered_provider_count++;
    return CA_OK;
}

size_t ca_provider_count(void) {
    size_t builtins = 0;
    while (builtin_provider_at(builtins) != NULL) {
        builtins++;
    }
    return builtins + registered_provider_count;
}

ca_status ca_provider_info_at(size_t index, ca_provider_info *info) {
    const ca_provider *provider;
    size_t builtins = 0;

    if (info == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    while (builtin_provider_at(builtins) != NULL) {
        builtins++;
    }
    if (index < builtins) {
        provider = builtin_provider_at(index);
    } else {
        index -= builtins;
        if (index >= registered_provider_count) {
            return CA_NOT_FOUND;
        }
        provider = &registered_providers[index];
    }

    info->name = provider->name;
    info->description = provider->description == NULL ? "" : provider->description;
    info->available = provider->call != NULL;
    return CA_OK;
}

ca_status ca_provider_call(const char *name,
                           const ca_provider_request *request,
                           ca_provider_response *response) {
    const char *resolved_name = (name == NULL || name[0] == '\0') ? CODEAGENT_DEFAULT_PROVIDER : name;
    const ca_provider *provider;

    if (request == NULL || response == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    response->content = NULL;
    response->content_count = 0;
    response->stop_reason = NULL;
    response->error_message = NULL;

    provider = find_registered_provider(resolved_name);
    if (provider == NULL) {
        provider = find_builtin_provider(resolved_name);
    }
    if (provider == NULL) {
        return CA_NOT_FOUND;
    }
    return provider->call(request, response, provider->userdata);
}
