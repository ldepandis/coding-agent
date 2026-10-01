/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#ifndef CODEAGENT_PROVIDER_H
#define CODEAGENT_PROVIDER_H

#include "codeagent/codeagent.h"

typedef struct ca_provider_request {
    const ca_message *messages;
    size_t message_count;
    const char *system_prompt;
    const char *model;
    const ca_config *config;
    const ca_tool_definition *tools;
    size_t tool_count;
} ca_provider_request;

typedef struct ca_provider_response {
    ca_content_block *content;
    size_t content_count;
    char *stop_reason;
    char *error_message;
} ca_provider_response;

typedef ca_status (*ca_provider_call_fn)(const ca_provider_request *request,
                                         ca_provider_response *response,
                                         void *userdata);

typedef struct ca_provider {
    const char *name;
    const char *description;
    ca_provider_call_fn call;
    void *userdata;
} ca_provider;

ca_status ca_provider_register(const ca_provider *provider);
ca_status ca_provider_call(const char *name,
                           const ca_provider_request *request,
                           ca_provider_response *response);

const ca_provider *ca_anthropic_provider(void);
const ca_provider *ca_openai_provider(void);
const ca_provider *ca_ollama_cloud_provider(void);
const ca_provider *ca_ollama_server_provider(void);
const ca_provider *ca_openai_compatible_provider(void);

#ifdef CODEAGENT_TESTING
ca_status ca_anthropic_parse_response_for_test(char *json,
                                               ca_content_block **content,
                                               size_t *content_count,
                                               char **stop_reason);
char *ca_anthropic_request_for_test(const ca_provider_request *request);
#endif

#endif
