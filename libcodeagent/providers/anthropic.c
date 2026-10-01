/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"
#include "provider.h"

#include <stdlib.h>
#include <string.h>

#ifdef CODEAGENT_HAVE_CURL
#include <curl/curl.h>
#endif

static ca_status content_to_json(ca_string_builder *sb, const ca_content_block *block) {
    char *a = NULL;
    char *b = NULL;
    if (block->type == CA_BLOCK_TEXT) {
        a = ca_json_escape(block->text);
        ca_sb_appendf(sb, "{\"type\":\"text\",\"text\":\"%s\"}", a);
    } else if (block->type == CA_BLOCK_TOOL_USE) {
        a = ca_json_escape(block->id);
        b = ca_json_escape(block->name);
        ca_sb_appendf(sb,
                      "{\"type\":\"tool_use\",\"id\":\"%s\",\"name\":\"%s\",\"input\":%s}",
                      a,
                      b,
                      block->input_json == NULL ? "{}" : block->input_json);
    } else {
        a = ca_json_escape(block->tool_use_id);
        b = ca_json_escape(block->content);
        ca_sb_appendf(sb,
                      "{\"type\":\"tool_result\",\"tool_use_id\":\"%s\",\"content\":\"%s\"%s}",
                      a,
                      b,
                      block->is_error ? ",\"is_error\":true" : "");
    }
    free(a);
    free(b);
    return CA_OK;
}

static ca_status tools_to_json(ca_string_builder *sb,
                               const ca_tool_definition *tools,
                               size_t tool_count) {
    size_t i;
    if (tool_count == 0 || tools == NULL) {
        return CA_OK;
    }
    ca_sb_append(sb, ",\"tools\":[");
    for (i = 0; i < tool_count; i++) {
        char *name = ca_json_escape(tools[i].name);
        char *description = ca_json_escape(tools[i].description);
        ca_sb_appendf(sb,
                      "%s{\"name\":\"%s\",\"description\":\"%s\",\"input_schema\":%s}",
                      i == 0 ? "" : ",",
                      name == NULL ? "" : name,
                      description == NULL ? "" : description,
                      tools[i].input_schema_json == NULL ? "{\"type\":\"object\"}" : tools[i].input_schema_json);
        free(name);
        free(description);
    }
    ca_sb_append(sb, "]");
    return CA_OK;
}

static char *messages_to_json(const ca_message *messages,
                              size_t message_count,
                              const char *system_prompt,
                              const char *model,
                              unsigned max_tokens,
                              const ca_tool_definition *tools,
                              size_t tool_count) {
    ca_string_builder sb;
    size_t i;
    char *system_json = ca_json_escape(system_prompt == NULL ? "" : system_prompt);
    ca_sb_init(&sb);
    ca_sb_appendf(&sb,
                  "{\"model\":\"%s\",\"max_tokens\":%u,\"system\":\"%s\",\"messages\":[",
                  model == NULL ? CODEAGENT_DEFAULT_MODEL : model,
                  max_tokens == 0 ? 4096u : max_tokens,
                  system_json == NULL ? "" : system_json);
    free(system_json);
    for (i = 0; i < message_count; i++) {
        size_t j;
        char *role = ca_json_escape(messages[i].role);
        ca_sb_appendf(&sb, "%s{\"role\":\"%s\",\"content\":[", i == 0 ? "" : ",", role);
        free(role);
        for (j = 0; j < messages[i].block_count; j++) {
            if (j > 0) {
                ca_sb_append(&sb, ",");
            }
            content_to_json(&sb, &messages[i].blocks[j]);
        }
        ca_sb_append(&sb, "]}");
    }
    ca_sb_append(&sb, "]");
    tools_to_json(&sb, tools, tool_count);
    ca_sb_append(&sb, "}");
    return ca_sb_take(&sb);
}

static const char *skip_ws_local(const char *p) {
    while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') {
        p++;
    }
    return p;
}

static const char *skip_json_string(const char *p) {
    if (*p != '"') {
        return p;
    }
    p++;
    while (*p != '\0') {
        if (*p == '\\' && p[1] != '\0') {
            p += 2;
        } else if (*p == '"') {
            return p + 1;
        } else {
            p++;
        }
    }
    return p;
}

static const char *skip_json_value(const char *p) {
    int depth = 0;
    p = skip_ws_local(p);
    if (*p == '"') {
        return skip_json_string(p);
    }
    if (*p == '{' || *p == '[') {
        do {
            if (*p == '"') {
                p = skip_json_string(p);
                continue;
            }
            if (*p == '{' || *p == '[') {
                depth++;
            } else if (*p == '}' || *p == ']') {
                depth--;
            }
            p++;
        } while (*p != '\0' && depth > 0);
        return p;
    }
    while (*p != '\0' && *p != ',' && *p != '}' && *p != ']') {
        p++;
    }
    return p;
}

static char *json_raw_field(const char *json, const char *key) {
    ca_string_builder needle;
    char *quoted;
    const char *p;
    const char *end;
    char *raw;
    size_t len;
    ca_sb_init(&needle);
    ca_sb_append(&needle, "\"");
    ca_sb_append(&needle, key);
    ca_sb_append(&needle, "\"");
    quoted = ca_sb_take(&needle);
    p = quoted == NULL ? NULL : strstr(json, quoted);
    free(quoted);
    if (p == NULL) {
        return NULL;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return NULL;
    }
    p = skip_ws_local(p + 1);
    end = skip_json_value(p);
    len = (size_t)(end - p);
    raw = (char *)malloc(len + 1);
    if (raw == NULL) {
        return NULL;
    }
    memcpy(raw, p, len);
    raw[len] = '\0';
    return raw;
}

static const char *find_content_array(const char *json, const char **array_end) {
    const char *p = strstr(json, "\"content\"");
    if (p == NULL) {
        return NULL;
    }
    p = strchr(p, '[');
    if (p == NULL) {
        return NULL;
    }
    *array_end = skip_json_value(p);
    return p + 1;
}

static char *copy_json_object(const char *start, const char **after) {
    const char *end = skip_json_value(start);
    size_t len = (size_t)(end - start);
    char *object = (char *)malloc(len + 1);
    if (object == NULL) {
        return NULL;
    }
    memcpy(object, start, len);
    object[len] = '\0';
    *after = end;
    return object;
}

static ca_status append_block(ca_content_block **content,
                              size_t *content_count,
                              ca_content_block block) {
    ca_content_block *next = (ca_content_block *)realloc(*content,
                                                          sizeof(ca_content_block) * (*content_count + 1));
    if (next == NULL) {
        ca_content_block_free(&block);
        return CA_NO_MEMORY;
    }
    *content = next;
    (*content)[*content_count] = block;
    *content_count += 1;
    return CA_OK;
}

static ca_status parse_anthropic_response(char *json,
                                          ca_content_block **content,
                                          size_t *content_count,
                                          char **stop_reason) {
    const char *p;
    const char *array_end = NULL;

    *content = NULL;
    *content_count = 0;
    if (ca_json_get_string(json, "stop_reason", stop_reason) != CA_OK) {
        *stop_reason = ca_strdup("end_turn");
    }

    p = find_content_array(json, &array_end);
    if (p != NULL) {
        while (p < array_end && *p != '\0') {
            p = skip_ws_local(p);
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p != '{') {
                p++;
                continue;
            }
            {
                char *object = copy_json_object(p, &p);
                char *type = NULL;
                if (object == NULL) {
                    return CA_NO_MEMORY;
                }
                ca_json_get_string(object, "type", &type);
                if (type != NULL && strcmp(type, "text") == 0) {
                    char *text = NULL;
                    if (ca_json_get_string(object, "text", &text) == CA_OK) {
                        ca_status status = append_block(content,
                                                        content_count,
                                                        ca_content_text(text));
                        free(text);
                        free(type);
                        free(object);
                        if (status != CA_OK) {
                            return status;
                        }
                        continue;
                    }
                } else if (type != NULL && strcmp(type, "tool_use") == 0) {
                    char *id = NULL;
                    char *name = NULL;
                    char *input = NULL;
                    ca_status status;
                    ca_json_get_string(object, "id", &id);
                    ca_json_get_string(object, "name", &name);
                    input = json_raw_field(object, "input");
                    status = append_block(content,
                                          content_count,
                                          ca_content_tool_use(id == NULL ? "tool_1" : id,
                                                              name == NULL ? "unknown" : name,
                                                              input == NULL ? "{}" : input));
                    free(id);
                    free(name);
                    free(input);
                    free(type);
                    free(object);
                    if (status != CA_OK) {
                        return status;
                    }
                    continue;
                } else if (type != NULL) {
                    ca_status status;
                    char *label = NULL;
                    ca_set_error(&label, "[unsupported content block: %s]", type);
                    status = append_block(content, content_count, ca_content_text(label));
                    free(label);
                    free(type);
                    free(object);
                    if (status != CA_OK) {
                        return status;
                    }
                    continue;
                }
                free(type);
                free(object);
            }
        }
    }

    if (*content_count == 0) {
        char *text = NULL;
        if (ca_json_get_string(json, "text", &text) != CA_OK) {
            text = ca_strdup(json);
        }
        if (append_block(content, content_count, ca_content_text(text)) != CA_OK) {
            free(text);
            return CA_NO_MEMORY;
        }
        free(text);
    }
    return CA_OK;
}

#ifdef CODEAGENT_HAVE_CURL
static size_t curl_write_cb(char *ptr, size_t size, size_t nmemb, void *userdata) {
    ca_string_builder *sb = (ca_string_builder *)userdata;
    ca_sb_append_n(sb, ptr, size * nmemb);
    return size * nmemb;
}
#endif

static ca_status anthropic_call(const ca_provider_request *request,
                                ca_provider_response *response,
                                void *userdata) {
    const char *api_key;
    char *body;
    (void)userdata;

    if (request == NULL || response == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    api_key = ca_config_provider_option_value(request->config,
                                             "anthropic",
                                             "api_key",
                                             "ANTHROPIC_API_KEY",
                                             NULL);
    if (api_key == NULL || api_key[0] == '\0') {
        return CA_HTTP_ERROR;
    }
    body = messages_to_json(request->messages,
                            request->message_count,
                            request->system_prompt,
                            request->model,
                            request->config == NULL ? 4096u : request->config->max_tokens,
                            request->tools,
                            request->tool_count);
    if (body == NULL) {
        return CA_NO_MEMORY;
    }

#ifdef CODEAGENT_HAVE_CURL
    {
        CURLcode curl_status = CURLE_OK;
        long status_code = 0;
        unsigned attempt;
        const unsigned retry_limit = request->config == NULL ? CODEAGENT_MAX_RETRIES : request->config->max_llm_retries;
        const unsigned retry_delay = request->config == NULL ? CODEAGENT_RETRY_DELAY_MS : request->config->llm_retry_delay_ms;

        for (attempt = 0; attempt <= retry_limit; attempt++) {
            CURL *curl = curl_easy_init();
            struct curl_slist *headers = NULL;
            ca_string_builder http_response;
            char api_header[1024];

            if (curl == NULL) {
                free(body);
                return CA_HTTP_ERROR;
            }
            snprintf(api_header, sizeof(api_header), "x-api-key: %s", api_key);
            headers = curl_slist_append(headers, api_header);
            headers = curl_slist_append(headers, "anthropic-version: 2023-06-01");
            headers = curl_slist_append(headers, "content-type: application/json");
            ca_sb_init(&http_response);
            curl_easy_setopt(curl, CURLOPT_URL, "https://api.anthropic.com/v1/messages");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body);
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &http_response);
            if (request->config != NULL && request->config->provider_timeout_ms > 0) {
                curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)request->config->provider_timeout_ms);
            }
            curl_status = curl_easy_perform(curl);
            if (curl_status == CURLE_OK) {
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
            }
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            if (curl_status == CURLE_OK && status_code >= 200 && status_code < 300) {
                char *json = ca_sb_take(&http_response);
                ca_status parse_status = parse_anthropic_response(json,
                                                                  &response->content,
                                                                  &response->content_count,
                                                                  &response->stop_reason);
                free(json);
                free(body);
                return parse_status;
            }
            {
                char *diagnostic = ca_sb_take(&http_response);
                free(response->error_message);
                response->error_message = NULL;
                if (curl_status != CURLE_OK) {
                    ca_set_error(&response->error_message,
                                 "Anthropic request failed: %s",
                                 curl_easy_strerror(curl_status));
                } else {
                    ca_set_error(&response->error_message,
                                 "Anthropic HTTP %ld: %s",
                                 status_code,
                                 diagnostic == NULL ? "" : diagnostic);
                }
                free(diagnostic);
            }
            if (attempt < retry_limit &&
                (curl_status != CURLE_OK || status_code == 429 || status_code >= 500)) {
                ca_sleep_ms(retry_delay * (attempt + 1u));
                continue;
            }
            break;
        }
        free(body);
        return CA_HTTP_ERROR;
    }
#else
    {
        free(body);
        return CA_HTTP_ERROR;
    }
#endif
}

#ifdef CODEAGENT_TESTING
ca_status ca_anthropic_parse_response_for_test(char *json,
                                               ca_content_block **content,
                                               size_t *content_count,
                                               char **stop_reason) {
    return parse_anthropic_response(json, content, content_count, stop_reason);
}

char *ca_anthropic_request_for_test(const ca_provider_request *request) {
    if (request == NULL) {
        return NULL;
    }
    return messages_to_json(request->messages,
                            request->message_count,
                            request->system_prompt,
                            request->model,
                            request->config == NULL ? 4096u : request->config->max_tokens,
                            request->tools,
                            request->tool_count);
}
#endif

const ca_provider *ca_anthropic_provider(void) {
    static const ca_provider provider = {
        "anthropic",
        "Anthropic Messages API provider",
        anthropic_call,
        NULL
    };
    return &provider;
}
