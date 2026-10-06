/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"
#include "provider.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#ifdef CODEAGENT_HAVE_CURL
#include <curl/curl.h>
#endif

typedef struct ollama_settings {
    const char *name;
    const char *default_base_url;
    const char *base_url_env;
    const char *api_key_env;
    const char *model_env;
    const char *default_model;
    int api_key_required;
} ollama_settings;

static char *join_url(const char *base_url, const char *path) {
    ca_string_builder sb;
    size_t len;
    ca_sb_init(&sb);
    ca_sb_append(&sb, base_url == NULL ? "" : base_url);
    len = sb.len;
    if (len > 0 && sb.data[len - 1] == '/') {
        ca_sb_append(&sb, path[0] == '/' ? path + 1 : path);
    } else {
        ca_sb_append(&sb, path[0] == '/' ? path : "/");
        if (path[0] != '/') {
            ca_sb_append(&sb, path);
        }
    }
    return ca_sb_take(&sb);
}

static int ends_with(const char *text, const char *suffix) {
    size_t text_len;
    size_t suffix_len;
    if (text == NULL || suffix == NULL) {
        return 0;
    }
    text_len = strlen(text);
    suffix_len = strlen(suffix);
    if (suffix_len > text_len) {
        return 0;
    }
    return strcmp(text + text_len - suffix_len, suffix) == 0;
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

static const char *find_array_field(const char *json, const char *key, const char **array_end) {
    ca_string_builder needle;
    char *quoted;
    const char *p;
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

static ca_status append_block(ca_provider_response *response, ca_content_block block) {
    if (response->content_count > SIZE_MAX / sizeof(ca_content_block) - 1) {
        ca_content_block_free(&block);
        return CA_NO_MEMORY;
    }
    ca_content_block *next = (ca_content_block *)realloc(response->content,
                                                          sizeof(ca_content_block) * (response->content_count + 1));
    if (next == NULL) {
        ca_content_block_free(&block);
        return CA_NO_MEMORY;
    }
    response->content = next;
    response->content[response->content_count] = block;
    response->content_count++;
    return CA_OK;
}

static ca_status tools_to_json(ca_string_builder *sb,
                               const ca_tool_definition *tools,
                               size_t tool_count) {
    size_t i;
    if (tools == NULL || tool_count == 0) {
        return CA_OK;
    }
    ca_sb_append(sb, ",\"tools\":[");
    for (i = 0; i < tool_count; i++) {
        char *name = ca_json_escape(tools[i].name);
        char *description = ca_json_escape(tools[i].description);
        ca_sb_appendf(sb,
                      "%s{\"type\":\"function\",\"function\":{\"name\":\"%s\",\"description\":\"%s\",\"parameters\":%s}}",
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

static char *message_content_text(const ca_message *message) {
    ca_string_builder sb;
    size_t i;
    ca_sb_init(&sb);
    for (i = 0; i < message->block_count; i++) {
        const ca_content_block *block = &message->blocks[i];
        if (sb.len > 0) {
            ca_sb_append(&sb, "\n");
        }
        if (block->type == CA_BLOCK_TEXT) {
            ca_sb_append(&sb, block->text == NULL ? "" : block->text);
        } else if (block->type == CA_BLOCK_TOOL_RESULT) {
            ca_sb_append(&sb, block->content == NULL ? "" : block->content);
        } else {
            ca_sb_appendf(&sb,
                          "[tool_use name=%s input=%s]",
                          block->name == NULL ? "" : block->name,
                          block->input_json == NULL ? "{}" : block->input_json);
        }
    }
    return ca_sb_take(&sb);
}

static const char *selected_model(const ca_provider_request *request,
                                  const ollama_settings *settings) {
    const char *configured_model = ca_config_provider_option_value(request->config,
                                                                  settings->name,
                                                                  "model",
                                                                  settings->model_env,
                                                                  NULL);
    if (configured_model != NULL && configured_model[0] != '\0') {
        return configured_model;
    }
    if (request->model != NULL && request->model[0] != '\0' &&
        strcmp(request->model, CODEAGENT_DEFAULT_MODEL) != 0) {
        return request->model;
    }
    return settings->default_model;
}

static char *request_body(const ca_provider_request *request, const ollama_settings *settings) {
    ca_string_builder sb;
    size_t i;
    char *model_json = ca_json_escape(selected_model(request, settings));

    ca_sb_init(&sb);
    ca_sb_appendf(&sb, "{\"model\":\"%s\",\"stream\":false,\"messages\":[", model_json);
    free(model_json);

    if (request->system_prompt != NULL && request->system_prompt[0] != '\0') {
        char *system_json = ca_json_escape(request->system_prompt);
        ca_sb_appendf(&sb, "{\"role\":\"system\",\"content\":\"%s\"}", system_json);
        free(system_json);
    }
    for (i = 0; i < request->message_count; i++) {
        char *role_json = ca_json_escape(request->messages[i].role);
        char *text = message_content_text(&request->messages[i]);
        char *text_json = ca_json_escape(text);
        if (i > 0 || (request->system_prompt != NULL && request->system_prompt[0] != '\0')) {
            ca_sb_append(&sb, ",");
        }
        ca_sb_appendf(&sb,
                      "{\"role\":\"%s\",\"content\":\"%s\"}",
                      role_json == NULL ? "" : role_json,
                      text_json == NULL ? "" : text_json);
        free(role_json);
        free(text);
        free(text_json);
    }
    ca_sb_append(&sb, "]");
    tools_to_json(&sb, request->tools, request->tool_count);
    ca_sb_append(&sb, "}");
    return ca_sb_take(&sb);
}

static ca_status parse_ollama_response(char *json, ca_provider_response *response) {
    char *text = NULL;
    int done = 0;
    const char *p;
    const char *array_end = NULL;
    int has_tool_use = 0;

    p = find_array_field(json, "tool_calls", &array_end);
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
                char *name = NULL;
                char *arguments = NULL;
                ca_status status;
                if (object == NULL) {
                    return CA_NO_MEMORY;
                }
                ca_json_get_string(object, "name", &name);
                arguments = json_raw_field(object, "arguments");
                if (name != NULL) {
                    status = append_block(response,
                                          ca_content_tool_use(name,
                                                              name,
                                                              arguments == NULL ? "{}" : arguments));
                    has_tool_use = 1;
                    free(name);
                    free(arguments);
                    free(object);
                    if (status != CA_OK) {
                        return status;
                    }
                    continue;
                }
                free(name);
                free(arguments);
                free(object);
            }
        }
    }

    if (ca_json_get_string(json, "content", &text) == CA_OK && text != NULL && text[0] != '\0') {
        ca_status status = append_block(response, ca_content_text(text));
        free(text);
        if (status != CA_OK) {
            return status;
        }
    } else if (ca_json_get_string(json, "response", &text) == CA_OK && text != NULL && text[0] != '\0') {
        ca_status status = append_block(response, ca_content_text(text));
        free(text);
        if (status != CA_OK) {
            return status;
        }
    } else if (response->content_count == 0 &&
               ca_json_get_string(json, "content", &text) != CA_OK &&
        ca_json_get_string(json, "response", &text) != CA_OK) {
        text = ca_strdup(json);
        if (append_block(response, ca_content_text(text)) != CA_OK) {
            free(text);
            return CA_NO_MEMORY;
        }
        free(text);
    }
    if (response->content_count == 0) {
        if (append_block(response, ca_content_text("")) != CA_OK) {
            return CA_NO_MEMORY;
        }
    }
    if (response->stop_reason == NULL) {
        if (has_tool_use) {
            response->stop_reason = ca_strdup("tool_use");
        } else if (ca_json_get_bool(json, "done", 1, &done) == CA_OK && done) {
            response->stop_reason = ca_strdup("end_turn");
        } else {
            response->stop_reason = ca_strdup("tool_use");
        }
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

static ca_status ollama_call(const ca_provider_request *request,
                             ca_provider_response *response,
                             void *userdata) {
    const ollama_settings *settings = (const ollama_settings *)userdata;
    const char *base_url;
    const char *api_key;
    char *body;
    char *url;

    if (request == NULL || response == NULL || settings == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    base_url = ca_config_provider_option_value(request->config,
                                              settings->name,
                                              "base_url",
                                              settings->base_url_env,
                                              NULL);
    if ((base_url == NULL || base_url[0] == '\0') && strcmp(settings->base_url_env, "CODEAGENT_OLLAMA_SERVER_BASE_URL") == 0) {
        base_url = getenv("OLLAMA_HOST");
    }
    if (base_url == NULL || base_url[0] == '\0') {
        base_url = settings->default_base_url;
    }
    api_key = ca_config_provider_option_value(request->config,
                                             settings->name,
                                             "api_key",
                                             settings->api_key_env,
                                             NULL);
    if (settings->api_key_required && (api_key == NULL || api_key[0] == '\0')) {
        return CA_HTTP_ERROR;
    }
    body = request_body(request, settings);
    url = join_url(base_url, ends_with(base_url, "/api") ? "/chat" : "/api/chat");
    if (body == NULL || url == NULL) {
        free(body);
        free(url);
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
            char auth_header[1024];

            if (curl == NULL) {
                free(body);
                free(url);
                return CA_HTTP_ERROR;
            }
            headers = curl_slist_append(headers, "content-type: application/json");
            if (api_key != NULL && api_key[0] != '\0') {
                snprintf(auth_header, sizeof(auth_header), "Authorization: Bearer %s", api_key);
                headers = curl_slist_append(headers, auth_header);
            }
            ca_sb_init(&http_response);
            curl_easy_setopt(curl, CURLOPT_URL, url);
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
                ca_status status = parse_ollama_response(json, response);
                free(json);
                free(body);
                free(url);
                return status;
            }
            {
                char *diagnostic = ca_sb_take(&http_response);
                free(response->error_message);
                response->error_message = NULL;
                if (curl_status != CURLE_OK) {
                    ca_set_error(&response->error_message,
                                 "%s request failed: %s",
                                 settings->name,
                                 curl_easy_strerror(curl_status));
                } else {
                    ca_set_error(&response->error_message,
                                 "%s HTTP %ld: %s",
                                 settings->name,
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
        free(url);
        return CA_HTTP_ERROR;
    }
#else
    free(body);
    free(url);
    return CA_HTTP_ERROR;
#endif
}

static const ollama_settings cloud_settings = {
    "ollama-cloud",
    "https://ollama.com/api",
    "CODEAGENT_OLLAMA_CLOUD_BASE_URL",
    "OLLAMA_API_KEY",
    "CODEAGENT_OLLAMA_CLOUD_MODEL",
    "gpt-oss:120b",
    1
};

static const ollama_settings server_settings = {
    "ollama-server",
    "http://localhost:11434/api",
    "CODEAGENT_OLLAMA_SERVER_BASE_URL",
    "OLLAMA_API_KEY",
    "CODEAGENT_OLLAMA_SERVER_MODEL",
    "gpt-oss:20b",
    0
};

const ca_provider *ca_ollama_cloud_provider(void) {
    static const ca_provider provider = {
        "ollama-cloud",
        "Ollama cloud provider",
        ollama_call,
        (void *)&cloud_settings
    };
    return &provider;
}

const ca_provider *ca_ollama_server_provider(void) {
    static const ca_provider provider = {
        "ollama-server",
        "Ollama local or remote server provider",
        ollama_call,
        (void *)&server_settings
    };
    return &provider;
}
