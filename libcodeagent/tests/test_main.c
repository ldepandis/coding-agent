/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "codeagent/codeagent.h"
#include "internal.h"
#include "provider.h"

#include <stdio.h>
#include <stdlib.h>
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

static int test_state_machine_text_response(void) {
    ca_state_machine *machine = ca_state_machine_new();
    ca_agent_event event;
    ca_agent_action action;
    ca_content_block block;
    ca_status status;

    memset(&event, 0, sizeof(event));
    event.type = CA_EVENT_USER_INPUT;
    event.text = "hello";
    status = ca_state_machine_handle(machine, &event, &action);
    ASSERT_TRUE(status == CA_OK);
    ASSERT_TRUE(action.type == CA_ACTION_SEND_LLM_REQUEST);
    ASSERT_TRUE(ca_state_machine_state(machine)->type == CA_STATE_CALLING_LLM);
    ca_agent_action_free(&action);

    block = ca_content_text("hi");
    memset(&event, 0, sizeof(event));
    event.type = CA_EVENT_LLM_COMPLETED;
    event.content = &block;
    event.content_count = 1;
    event.stop_reason = "end_turn";
    status = ca_state_machine_handle(machine, &event, &action);
    ca_content_block_free(&block);
    ASSERT_TRUE(status == CA_OK);
    ASSERT_TRUE(action.type == CA_ACTION_DISPLAY_TEXT);
    ASSERT_TRUE(strcmp(action.text, "hi") == 0);
    ASSERT_TRUE(ca_state_machine_state(machine)->type == CA_STATE_WAITING_FOR_USER_INPUT);
    ca_agent_action_free(&action);
    ca_state_machine_free(machine);
    return 0;
}

static int test_tool_edit_create(void) {
    char *output = NULL;
    ca_status status = ca_tool_edit_file("{\"path\":\"/tmp/codeagent-test-edit.txt\",\"old_str\":\"\",\"new_str\":\"hello\"}",
                                        &output,
                                        NULL);
    ASSERT_TRUE(status == CA_OK);
    ASSERT_TRUE(output != NULL);
    free(output);
    return 0;
}

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

static int test_builtin_tool_schemas_are_specific(void) {
    ca_tool_definition *tools = NULL;
    size_t tool_count = 0;
    size_t i;
    int found_required_content = 0;
    int found_case_sensitive = 0;

    ASSERT_TRUE(ca_builtin_tools(&tools, &tool_count) == CA_OK);
    ASSERT_TRUE(tool_count == 6);
    for (i = 0; i < tool_count; i++) {
        ASSERT_TRUE(tools[i].input_schema_json != NULL);
        ASSERT_TRUE(strcmp(tools[i].input_schema_json, "{\"type\":\"object\"}") != 0);
        if (strcmp(tools[i].name, "write_file") == 0 &&
            strstr(tools[i].input_schema_json, "\"content\"") != NULL &&
            strstr(tools[i].input_schema_json, "\"required\"") != NULL) {
            found_required_content = 1;
        }
        if (strcmp(tools[i].name, "code_search") == 0 &&
            strstr(tools[i].input_schema_json, "\"case_sensitive\"") != NULL) {
            found_case_sensitive = 1;
        }
    }
    ASSERT_TRUE(found_required_content);
    ASSERT_TRUE(found_case_sensitive);
    ca_tool_definitions_free(tools, tool_count);
    return 0;
}

static int test_read_file_truncation_includes_line_count(void) {
    char path[128];
    FILE *file;
    char *output = NULL;
    ca_string_builder input;
    int i;

    snprintf(path, sizeof(path), "/tmp/codeagent-read-truncate-%ld.txt", (long)getpid());
    file = fopen(path, "wb");
    ASSERT_TRUE(file != NULL);
    for (i = 0; i < 100010; i++) {
        fputc(i % 10 == 0 ? '\n' : 'a', file);
    }
    ASSERT_TRUE(fclose(file) == 0);

    ca_sb_init(&input);
    ca_sb_appendf(&input, "{\"path\":\"%s\"}", path);
    ASSERT_TRUE(ca_tool_read_file(input.data, &output, NULL) == CA_OK);
    ASSERT_TRUE(strstr(output, "showing first 100000 characters") != NULL);
    ASSERT_TRUE(strstr(output, "lines. File is 100010 bytes total") != NULL);
    free(output);
    ca_sb_free(&input);
    remove(path);
    return 0;
}

static int test_bash_stdout_stderr_behavior(void) {
    char *output = NULL;

    ASSERT_TRUE(ca_tool_bash("{\"command\":\"printf err >&2\"}", &output, NULL) == CA_OK);
    ASSERT_TRUE(strcmp(output, "err") == 0);
    free(output);
    output = NULL;

    ASSERT_TRUE(ca_tool_bash("{\"command\":\"printf out; printf err >&2; exit 7\"}", &output, NULL) == CA_ERROR);
    ASSERT_TRUE(strstr(output, "Command failed with exit code: 7") != NULL);
    ASSERT_TRUE(strstr(output, "stdout: out") != NULL);
    ASSERT_TRUE(strstr(output, "stderr: err") != NULL);
    free(output);
    return 0;
}

static int test_code_search_uses_argv_and_limits_matches(void) {
    char dir[128];
    char file_path[160];
    ca_string_builder content;
    ca_string_builder input;
    char *output = NULL;
    int i;

    snprintf(dir, sizeof(dir), "/tmp/codeagent-rg-%ld", (long)getpid());
    ASSERT_TRUE(mkdir(dir, 0777) == 0 || access(dir, F_OK) == 0);
    snprintf(file_path, sizeof(file_path), "%s/search.txt", dir);
    ca_sb_init(&content);
    for (i = 0; i < 60; i++) {
        ca_sb_append(&content, "needle'quote\n");
    }
    ASSERT_TRUE(write_test_file(file_path, content.data) == 0);
    ca_sb_free(&content);

    ca_sb_init(&input);
    ca_sb_appendf(&input, "{\"pattern\":\"needle'quote\",\"path\":\"%s\"}", dir);
    ASSERT_TRUE(ca_tool_code_search(input.data, &output, NULL) == CA_OK);
    ASSERT_TRUE(strstr(output, "needle'quote") != NULL);
    ASSERT_TRUE(strstr(output, "... (showing first 50 of 60 matches)") != NULL);
    free(output);
    ca_sb_free(&input);
    remove(file_path);
    rmdir(dir);
    return 0;
}

static int test_list_files_pretty_json(void) {
    char dir[128];
    char child_dir[160];
    char file_path[180];
    ca_string_builder input;
    char *output = NULL;

    snprintf(dir, sizeof(dir), "/tmp/codeagent-list-%ld", (long)getpid());
    snprintf(child_dir, sizeof(child_dir), "%s/dir", dir);
    snprintf(file_path, sizeof(file_path), "%s/file.txt", child_dir);
    ASSERT_TRUE(mkdir(dir, 0777) == 0 || access(dir, F_OK) == 0);
    ASSERT_TRUE(mkdir(child_dir, 0777) == 0 || access(child_dir, F_OK) == 0);
    ASSERT_TRUE(write_test_file(file_path, "x") == 0);
    ca_sb_init(&input);
    ca_sb_appendf(&input, "{\"path\":\"%s\"}", dir);
    ASSERT_TRUE(ca_tool_list_files(input.data, &output, NULL) == CA_OK);
    ASSERT_TRUE(strstr(output, "[\n") == output);
    ASSERT_TRUE(strstr(output, "\"dir/\"") != NULL);
    ASSERT_TRUE(strstr(output, "\"dir/file.txt\"") != NULL);
    free(output);
    ca_sb_free(&input);
    remove(file_path);
    rmdir(child_dir);
    rmdir(dir);
    return 0;
}

static int test_write_edit_permission_checks(void) {
    ca_config config;
    char path[160];
    ca_string_builder input;
    char *output = NULL;

    ca_config_init_defaults(&config);
    snprintf(path, sizeof(path), "/tmp/codeagent-permission-%ld.txt", (long)getpid());
    ca_sb_init(&input);
    ca_sb_appendf(&input, "{\"path\":\"%s\",\"content\":\"x\"}", path);
    ASSERT_TRUE(ca_tool_write_file(input.data, &output, &config) == CA_PERMISSION_REQUIRED);
    ASSERT_TRUE(strstr(output, "ErrorCategory::Permission|Writing") != NULL);
    free(output);
    output = NULL;
    ca_sb_free(&input);

    ASSERT_TRUE(ca_config_add_trusted_path(&config, "/tmp/codeagent-permission-") == CA_OK);
    ca_sb_init(&input);
    ca_sb_appendf(&input, "{\"path\":\"%s\",\"content\":\"x\"}", path);
    ASSERT_TRUE(ca_tool_write_file(input.data, &output, &config) == CA_OK);
    ASSERT_TRUE(strstr(output, "Successfully wrote 1 bytes") != NULL);
    free(output);
    output = NULL;
    ca_sb_free(&input);

    ca_sb_init(&input);
    ca_sb_appendf(&input, "{\"path\":\"%s\",\"old_str\":\"x\",\"new_str\":\"y\"}", path);
    ASSERT_TRUE(ca_tool_edit_file(input.data, &output, &config) == CA_OK);
    ASSERT_TRUE(strcmp(output, "OK") == 0);
    free(output);
    ca_sb_free(&input);
    remove(path);
    ca_config_free(&config);
    return 0;
}

static ca_status mock_provider_call(const ca_provider_request *request,
                                    ca_provider_response *response,
                                    void *userdata) {
    int *calls = (int *)userdata;
    (void)request;

    response->content = (ca_content_block *)calloc(1, sizeof(ca_content_block));
    if (response->content == NULL) {
        return CA_NO_MEMORY;
    }
    response->content[0] = ca_content_text("hello from mock");
    response->content_count = 1;
    response->stop_reason = ca_strdup("end_turn");
    (*calls)++;
    return CA_OK;
}

typedef struct multi_tool_ctx {
    int provider_calls;
    int retry_tool_calls;
    int hook_calls;
} multi_tool_ctx;

static ca_status multi_tool_provider_call(const ca_provider_request *request,
                                          ca_provider_response *response,
                                          void *userdata) {
    multi_tool_ctx *ctx = (multi_tool_ctx *)userdata;
    (void)request;
    if (ctx->provider_calls == 0) {
        response->content = (ca_content_block *)calloc(3, sizeof(ca_content_block));
        if (response->content == NULL) {
            return CA_NO_MEMORY;
        }
        response->content[0] = ca_content_text("I will use tools.");
        response->content[1] = ca_content_tool_use("call_a", "ok_tool", "{\"value\":\"a\"}");
        response->content[2] = ca_content_tool_use("call_b", "retry_tool", "{\"value\":\"b\"}");
        response->content_count = 3;
        response->stop_reason = ca_strdup("tool_use");
    } else {
        response->content = (ca_content_block *)calloc(1, sizeof(ca_content_block));
        if (response->content == NULL) {
            return CA_NO_MEMORY;
        }
        response->content[0] = ca_content_text("done");
        response->content_count = 1;
        response->stop_reason = ca_strdup("end_turn");
    }
    ctx->provider_calls++;
    return CA_OK;
}

static ca_status llm_retry_provider_call(const ca_provider_request *request,
                                         ca_provider_response *response,
                                         void *userdata) {
    int *calls = (int *)userdata;
    (void)request;
    if (*calls == 0) {
        (*calls)++;
        return CA_HTTP_ERROR;
    }
    response->content = (ca_content_block *)calloc(1, sizeof(ca_content_block));
    if (response->content == NULL) {
        return CA_NO_MEMORY;
    }
    response->content[0] = ca_content_text("retried");
    response->content_count = 1;
    response->stop_reason = ca_strdup("end_turn");
    (*calls)++;
    return CA_OK;
}

static ca_status ok_tool(const char *input_json, char **output, void *userdata) {
    (void)input_json;
    (void)userdata;
    *output = ca_strdup("ok");
    return CA_OK;
}

static ca_status external_echo_tool(const char *input_json, char **output, void *userdata) {
    const char *prefix = (const char *)userdata;
    ca_string_builder sb;
    ca_sb_init(&sb);
    ca_sb_append(&sb, prefix == NULL ? "" : prefix);
    ca_sb_append(&sb, input_json == NULL ? "" : input_json);
    *output = ca_sb_take(&sb);
    return *output == NULL ? CA_NO_MEMORY : CA_OK;
}

static ca_status retry_tool(const char *input_json, char **output, void *userdata) {
    multi_tool_ctx *ctx = (multi_tool_ctx *)userdata;
    (void)input_json;
    ctx->retry_tool_calls++;
    if (ctx->retry_tool_calls == 1) {
        *output = ca_strdup("network timeout");
        return CA_ERROR;
    }
    *output = ca_strdup("retry ok");
    return CA_OK;
}

static ca_status post_tools_hook(const ca_message *conversation,
                                 size_t conversation_count,
                                 const char *const *tool_names,
                                 size_t tool_name_count,
                                 char **warning,
                                 int *proceed,
                                 void *userdata) {
    multi_tool_ctx *ctx = (multi_tool_ctx *)userdata;
    ASSERT_TRUE(conversation != NULL);
    ASSERT_TRUE(conversation_count == 3);
    ASSERT_TRUE(conversation[2].block_count == 2);
    ASSERT_TRUE(tool_name_count == 2);
    ASSERT_TRUE(strcmp(tool_names[0], "ok_tool") == 0);
    ASSERT_TRUE(strcmp(tool_names[1], "retry_tool") == 0);
    ctx->hook_calls++;
    *warning = ca_strdup("hook warning");
    *proceed = 1;
    return CA_OK;
}

static int test_agent_conversation_loop_in_library(void) {
    ca_config config;
    ca_agent_callbacks callbacks;
    ca_agent *agent;
    size_t count = 0;
    int calls = 0;
    ca_provider provider;

    ca_config_init_defaults(&config);
    free(config.provider);
    config.provider = ca_strdup("mock");
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.userdata = &calls;
    provider.name = "mock";
    provider.description = "Test provider";
    provider.call = mock_provider_call;
    provider.userdata = &calls;
    ASSERT_TRUE(ca_provider_register(&provider) == CA_OK);

    agent = ca_agent_new(&config, &callbacks, NULL, 0);
    ASSERT_TRUE(agent != NULL);
    ASSERT_TRUE(ca_agent_submit(agent, "hello") == CA_OK);
    ASSERT_TRUE(calls == 1);
    ASSERT_TRUE(ca_agent_conversation(agent, &count) != NULL);
    ASSERT_TRUE(count == 2);

    ca_agent_free(agent);
    ca_config_free(&config);
    return 0;
}

static int test_agent_state_machine_tool_loop(void) {
    ca_config config;
    ca_agent_callbacks callbacks;
    ca_agent *agent;
    ca_provider provider;
    ca_tool_definition *tools;
    const ca_message *conversation;
    size_t count = 0;
    multi_tool_ctx ctx;

    memset(&ctx, 0, sizeof(ctx));
    tools = (ca_tool_definition *)calloc(2, sizeof(ca_tool_definition));
    ASSERT_TRUE(tools != NULL);
    ca_config_init_defaults(&config);
    ca_config_set_provider(&config, "mock-tools");
    config.max_tool_iterations = 5;
    config.max_tool_retries = 1;
    config.tool_retry_delay_ms = 0;
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.post_tools_hook = post_tools_hook;
    callbacks.userdata = &ctx;
    provider.name = "mock-tools";
    provider.description = "Tool loop provider";
    provider.call = multi_tool_provider_call;
    provider.userdata = &ctx;
    ASSERT_TRUE(ca_provider_register(&provider) == CA_OK);
    tools[0].name = ca_strdup("ok_tool");
    tools[0].description = ca_strdup("ok");
    tools[0].input_schema_json = ca_strdup("{\"type\":\"object\"}");
    tools[0].function = ok_tool;
    tools[1].name = ca_strdup("retry_tool");
    tools[1].description = ca_strdup("retry");
    tools[1].input_schema_json = ca_strdup("{\"type\":\"object\"}");
    tools[1].function = retry_tool;
    tools[1].userdata = &ctx;

    agent = ca_agent_new(&config, &callbacks, tools, 2);
    ASSERT_TRUE(agent != NULL);
    ASSERT_TRUE(ca_agent_submit(agent, "use tools") == CA_OK);
    ASSERT_TRUE(ctx.provider_calls == 2);
    ASSERT_TRUE(ctx.retry_tool_calls == 2);
    ASSERT_TRUE(ctx.hook_calls == 1);
    conversation = ca_agent_conversation(agent, &count);
    ASSERT_TRUE(conversation != NULL);
    ASSERT_TRUE(count == 4);
    ASSERT_TRUE(conversation[2].block_count == 2);

    ca_agent_free(agent);
    ca_tool_definitions_free(tools, 2);
    ca_config_free(&config);
    return 0;
}

static int test_agent_llm_retry(void) {
    ca_config config;
    ca_agent_callbacks callbacks;
    ca_agent *agent;
    ca_provider provider;
    int calls = 0;

    ca_config_init_defaults(&config);
    ca_config_set_provider(&config, "mock-llm-retry");
    config.max_llm_retries = 1;
    config.llm_retry_delay_ms = 0;
    memset(&callbacks, 0, sizeof(callbacks));
    provider.name = "mock-llm-retry";
    provider.description = "LLM retry provider";
    provider.call = llm_retry_provider_call;
    provider.userdata = &calls;
    ASSERT_TRUE(ca_provider_register(&provider) == CA_OK);

    agent = ca_agent_new(&config, &callbacks, NULL, 0);
    ASSERT_TRUE(agent != NULL);
    ASSERT_TRUE(ca_agent_submit(agent, "retry") == CA_OK);
    ASSERT_TRUE(calls == 2);

    ca_agent_free(agent);
    ca_config_free(&config);
    return 0;
}

static int test_provider_discovery(void) {
    ca_provider_info info;
    size_t count = ca_provider_count();
    size_t i;
    int found_anthropic = 0;
    int found_openai = 0;
    int found_ollama_cloud = 0;
    int found_ollama_server = 0;
    int found_openai_compatible = 0;

    ASSERT_TRUE(count >= 5);
    for (i = 0; i < count; i++) {
        ASSERT_TRUE(ca_provider_info_at(i, &info) == CA_OK);
        ASSERT_TRUE(info.name != NULL);
        if (strcmp(info.name, "anthropic") == 0) {
            found_anthropic = 1;
        } else if (strcmp(info.name, "openai") == 0) {
            found_openai = 1;
        } else if (strcmp(info.name, "ollama-cloud") == 0) {
            found_ollama_cloud = 1;
        } else if (strcmp(info.name, "ollama-server") == 0) {
            found_ollama_server = 1;
        } else if (strcmp(info.name, "openai-compatible") == 0) {
            found_openai_compatible = 1;
        }
    }
    ASSERT_TRUE(found_anthropic);
    ASSERT_TRUE(found_openai);
    ASSERT_TRUE(found_ollama_cloud);
    ASSERT_TRUE(found_ollama_server);
    ASSERT_TRUE(found_openai_compatible);
    return 0;
}

static int test_config_provider_options(void) {
    ca_config config;
    ca_config clone;
    ca_config_init_defaults(&config);
    ASSERT_TRUE(ca_config_set_provider_option(&config, "openai", "base_url", "http://example.test/v1") == CA_OK);
    ASSERT_TRUE(strcmp(ca_config_provider_option(&config, "openai", "base_url"), "http://example.test/v1") == 0);
    ASSERT_TRUE(ca_config_clone(&config, &clone) == CA_OK);
    ASSERT_TRUE(strcmp(ca_config_provider_option(&clone, "openai", "base_url"), "http://example.test/v1") == 0);
    ca_config_free(&clone);
    ca_config_free(&config);
    return 0;
}

static int test_version_and_tool_registry_api(void) {
    ca_api_version version = ca_version();
    ca_tool_registry *registry;
    ca_tool_definition *tools = NULL;
    size_t tool_count = 0;
    char *output = NULL;

    ASSERT_TRUE(version.major == CODEAGENT_VERSION_MAJOR);
    ASSERT_TRUE(version.minor == CODEAGENT_VERSION_MINOR);
    ASSERT_TRUE(version.patch == CODEAGENT_VERSION_PATCH);
    ASSERT_TRUE(version.abi == ca_abi_version());
    ASSERT_TRUE(strcmp(ca_version_string(), CODEAGENT_VERSION) == 0);

    registry = ca_tool_registry_new();
    ASSERT_TRUE(registry != NULL);
    ASSERT_TRUE(ca_tool_registry_add_builtin(registry) == CA_OK);
    ASSERT_TRUE(ca_tool_registry_count(registry) == 6);
    ASSERT_TRUE(ca_tool_registry_register(registry,
                                          "external_echo",
                                          "Echo input with prefix",
                                          "{\"type\":\"object\"}",
                                          external_echo_tool,
                                          "prefix:") == CA_OK);
    ASSERT_TRUE(ca_tool_registry_count(registry) == 7);
    ASSERT_TRUE(ca_tool_registry_execute(registry, "external_echo", "{\"x\":1}", &output) == CA_OK);
    ASSERT_TRUE(strcmp(output, "prefix:{\"x\":1}") == 0);
    free(output);
    output = NULL;
    ASSERT_TRUE(ca_tool_registry_definitions(registry, &tools, &tool_count) == CA_OK);
    ASSERT_TRUE(tool_count == 7);
    ASSERT_TRUE(ca_tool_execute(tools, tool_count, "external_echo", "{}", &output) == CA_OK);
    ASSERT_TRUE(strcmp(output, "prefix:{}") == 0);
    free(output);
    ca_tool_definitions_free(tools, tool_count);
    ca_tool_registry_free(registry);
    return 0;
}

typedef struct memory_storage {
    char *ref;
    char *content;
} memory_storage;

static ca_status memory_storage_save(const char *key,
                                     const char *content,
                                     char **out_ref,
                                     void *userdata) {
    memory_storage *storage = (memory_storage *)userdata;
    free(storage->ref);
    free(storage->content);
    storage->ref = ca_strdup(key == NULL ? "session" : key);
    storage->content = ca_strdup(content == NULL ? "" : content);
    if (storage->ref == NULL || storage->content == NULL) {
        return CA_NO_MEMORY;
    }
    if (out_ref != NULL) {
        *out_ref = ca_strdup(storage->ref);
        return *out_ref == NULL ? CA_NO_MEMORY : CA_OK;
    }
    return CA_OK;
}

static ca_status memory_storage_load(const char *ref, char **out_content, void *userdata) {
    memory_storage *storage = (memory_storage *)userdata;
    if (storage->ref == NULL || strcmp(storage->ref, ref) != 0) {
        return CA_NOT_FOUND;
    }
    *out_content = ca_strdup(storage->content);
    return *out_content == NULL ? CA_NO_MEMORY : CA_OK;
}

static ca_status memory_storage_list(char ***out_refs, size_t *out_count, void *userdata) {
    memory_storage *storage = (memory_storage *)userdata;
    *out_refs = NULL;
    *out_count = 0;
    if (storage->ref == NULL) {
        return CA_OK;
    }
    *out_refs = (char **)calloc(1, sizeof(char *));
    if (*out_refs == NULL) {
        return CA_NO_MEMORY;
    }
    (*out_refs)[0] = ca_strdup(storage->ref);
    if ((*out_refs)[0] == NULL) {
        free(*out_refs);
        *out_refs = NULL;
        return CA_NO_MEMORY;
    }
    *out_count = 1;
    return CA_OK;
}

static int test_storage_adapter_api(void) {
    memory_storage storage;
    ca_storage_adapter adapter;
    ca_session *session;
    ca_session *loaded = NULL;
    ca_message message;
    char *ref = NULL;
    char **refs = NULL;
    size_t ref_count = 0;
    size_t message_count = 0;

    memset(&storage, 0, sizeof(storage));
    memset(&adapter, 0, sizeof(adapter));
    adapter.save = memory_storage_save;
    adapter.load = memory_storage_load;
    adapter.list = memory_storage_list;
    adapter.userdata = &storage;

    session = ca_session_new("memory-session");
    ASSERT_TRUE(session != NULL);
    message = ca_message_user("hello storage");
    ASSERT_TRUE(ca_session_append(session, &message) == CA_OK);
    ca_message_free(&message);

    ASSERT_TRUE(ca_storage_save_session(&adapter, "ref-1", session, &ref) == CA_OK);
    ASSERT_TRUE(strcmp(ref, "ref-1") == 0);
    ASSERT_TRUE(ca_storage_list_sessions(&adapter, &refs, &ref_count) == CA_OK);
    ASSERT_TRUE(ref_count == 1);
    ASSERT_TRUE(strcmp(refs[0], "ref-1") == 0);
    ASSERT_TRUE(ca_storage_load_session(&adapter, ref, &loaded) == CA_OK);
    ASSERT_TRUE(ca_session_messages(loaded, &message_count) != NULL);
    ASSERT_TRUE(message_count == 1);

    ca_string_list_free(refs, ref_count);
    free(ref);
    ca_session_free(loaded);
    ca_session_free(session);
    free(storage.ref);
    free(storage.content);
    return 0;
}

static int test_anthropic_request_serializes_tools(void) {
    ca_config config;
    ca_message message;
    ca_tool_definition tool;
    ca_provider_request request;
    char *body;

    ca_config_init_defaults(&config);
    message = ca_message_user("read it");
    memset(&tool, 0, sizeof(tool));
    tool.name = ca_strdup("read_file");
    tool.description = ca_strdup("Read a file");
    tool.input_schema_json = ca_strdup("{\"type\":\"object\",\"properties\":{\"path\":{\"type\":\"string\"}},\"required\":[\"path\"]}");

    memset(&request, 0, sizeof(request));
    request.messages = &message;
    request.message_count = 1;
    request.system_prompt = "system";
    request.model = "claude-test";
    request.config = &config;
    request.tools = &tool;
    request.tool_count = 1;

    body = ca_anthropic_request_for_test(&request);
    ASSERT_TRUE(body != NULL);
    ASSERT_TRUE(strstr(body, "\"tools\"") != NULL);
    ASSERT_TRUE(strstr(body, "\"input_schema\"") != NULL);
    ASSERT_TRUE(strstr(body, "\"read_file\"") != NULL);
    ASSERT_TRUE(strstr(body, "\"path\"") != NULL);

    free(body);
    free(tool.name);
    free(tool.description);
    free(tool.input_schema_json);
    ca_message_free(&message);
    ca_config_free(&config);
    return 0;
}

static int test_anthropic_parse_content_blocks(void) {
    char json[] = "{\"stop_reason\":\"tool_use\",\"content\":["
                  "{\"type\":\"text\",\"text\":\"hello\"},"
                  "{\"type\":\"tool_use\",\"id\":\"toolu_1\",\"name\":\"read_file\",\"input\":{\"path\":\"src/main.c\",\"nested\":{\"x\":1}}},"
                  "{\"type\":\"thinking\",\"thinking\":\"hidden\"}"
                  "]}";
    ca_content_block *content = NULL;
    size_t content_count = 0;
    char *stop_reason = NULL;
    ca_status status;
    size_t i;

    status = ca_anthropic_parse_response_for_test(json, &content, &content_count, &stop_reason);
    ASSERT_TRUE(status == CA_OK);
    ASSERT_TRUE(stop_reason != NULL);
    ASSERT_TRUE(strcmp(stop_reason, "tool_use") == 0);
    ASSERT_TRUE(content_count == 3);
    ASSERT_TRUE(content[0].type == CA_BLOCK_TEXT);
    ASSERT_TRUE(strcmp(content[0].text, "hello") == 0);
    ASSERT_TRUE(content[1].type == CA_BLOCK_TOOL_USE);
    ASSERT_TRUE(strcmp(content[1].id, "toolu_1") == 0);
    ASSERT_TRUE(strcmp(content[1].name, "read_file") == 0);
    ASSERT_TRUE(strstr(content[1].input_json, "\"nested\":{\"x\":1}") != NULL);
    ASSERT_TRUE(content[2].type == CA_BLOCK_TEXT);
    ASSERT_TRUE(strstr(content[2].text, "unsupported content block") != NULL);

    for (i = 0; i < content_count; i++) {
        ca_content_block_free(&content[i]);
    }
    free(content);
    free(stop_reason);
    return 0;
}

static int test_command_discovery(void) {
    ca_command_info info;
    ASSERT_TRUE(ca_command_count() >= 19);
    ASSERT_TRUE(ca_command_find("help", &info) == CA_OK);
    ASSERT_TRUE(strcmp(info.name, "help") == 0);
    ASSERT_TRUE(ca_command_find("quit", &info) == CA_OK);
    ASSERT_TRUE(ca_command_find("q", &info) == CA_OK);
    ASSERT_TRUE(ca_command_info_at(0, &info) == CA_OK);
    ASSERT_TRUE(info.name != NULL);
    return 0;
}

static int test_session_metrics_and_permissions(void) {
    ca_config config;
    ca_session *session;
    ca_message message;
    const ca_message *messages;
    size_t message_count = 0;
    double cost = 0.0;

    ca_config_init_defaults(&config);
    ASSERT_TRUE(ca_config_add_trusted_path(&config, "/tmp/codeagent") == CA_OK);
    ASSERT_TRUE(ca_permission_check_path(&config, "/tmp/codeagent/file.txt", 1) == CA_PERMISSION_ALLOW);
    ASSERT_TRUE(ca_permission_check_path(&config, "/private/file.txt", 1) == CA_PERMISSION_PROMPT);
    ASSERT_TRUE(ca_permission_check_path(&config, "/private/file.txt", 0) == CA_PERMISSION_ALLOW);

    session = ca_session_new("test");
    ASSERT_TRUE(session != NULL);
    ASSERT_TRUE(strcmp(ca_session_id(session), "test") == 0);
    message = ca_message_user("hello there");
    ASSERT_TRUE(ca_session_append(session, &message) == CA_OK);
    ca_message_free(&message);
    messages = ca_session_messages(session, &message_count);
    ASSERT_TRUE(messages != NULL);
    ASSERT_TRUE(message_count == 1);
    ASSERT_TRUE(ca_estimate_tokens_messages(messages, message_count) > 0);
    ASSERT_TRUE(ca_estimate_cost_usd("gpt-4.1-mini", 1000, 1000, &cost) == CA_OK);
    ASSERT_TRUE(cost > 0.0);
    ASSERT_TRUE(ca_session_save_markdown(session, "/tmp/codeagent-session-test.md") == CA_OK);
    ca_session_free(session);
    session = NULL;
    ASSERT_TRUE(ca_session_load_markdown(&session, "/tmp/codeagent-session-test.md") == CA_OK);
    ASSERT_TRUE(session != NULL);

    ca_session_free(session);
    ca_config_free(&config);
    return 0;
}

static int test_session_manager_roundtrip_latest_list(void) {
    char dir[128];
    ca_session_manager *manager;
    ca_session *session;
    ca_session *loaded = NULL;
    ca_message message;
    char *saved_path = NULL;
    char **paths = NULL;
    size_t path_count = 0;
    const ca_message *messages;
    size_t message_count = 0;

    snprintf(dir, sizeof(dir), "/tmp/codeagent-sessions-%ld", (long)getpid());
    manager = ca_session_manager_new(dir);
    ASSERT_TRUE(manager != NULL);
    session = ca_session_new("roundtrip");
    ASSERT_TRUE(session != NULL);
    message = ca_message_user("hello persisted");
    ASSERT_TRUE(ca_session_append(session, &message) == CA_OK);
    ca_message_free(&message);

    ASSERT_TRUE(ca_session_manager_save(manager, session, &saved_path) == CA_OK);
    ASSERT_TRUE(saved_path != NULL);
    ASSERT_TRUE(access(saved_path, F_OK) == 0);
    ASSERT_TRUE(ca_session_manager_list(manager, &paths, &path_count) == CA_OK);
    ASSERT_TRUE(path_count >= 1);
    ASSERT_TRUE(ca_session_manager_load_latest(manager, &loaded) == CA_OK);
    ASSERT_TRUE(loaded != NULL);
    messages = ca_session_messages(loaded, &message_count);
    ASSERT_TRUE(messages != NULL);
    ASSERT_TRUE(message_count >= 1);
    ASSERT_TRUE(strstr(messages[0].blocks[0].text, "hello persisted") != NULL);

    ca_string_list_free(paths, path_count);
    ca_session_free(loaded);
    ca_session_free(session);
    remove(saved_path);
    free(saved_path);
    ca_session_manager_free(manager);
    rmdir(dir);
    return 0;
}

static int test_permission_checker_cache_and_prompt_parse(void) {
    ca_config config;
    ca_permission_checker *checker;
    ca_permission_response response;
    const char *path = "/tmp/codeagent-permission-cache/file.txt";

    ca_config_init_defaults(&config);
    config.auto_read = 0;
    checker = ca_permission_checker_new(&config);
    ASSERT_TRUE(checker != NULL);
    ASSERT_TRUE(ca_permission_checker_check(checker, path, 0) == CA_PERMISSION_PROMPT);
    ASSERT_TRUE(ca_permission_response_parse("y", &response) == CA_OK);
    ASSERT_TRUE(response == CA_PERMISSION_RESPONSE_ALLOW_ONCE);
    ASSERT_TRUE(ca_permission_checker_respond(checker, &config, path, 0, response) == CA_OK);
    ASSERT_TRUE(ca_permission_checker_check(checker, path, 0) == CA_PERMISSION_ALLOW);
    ASSERT_TRUE(ca_permission_response_parse("always", &response) == CA_OK);
    ASSERT_TRUE(ca_permission_checker_respond(checker, &config, path, 1, response) == CA_OK);
    ASSERT_TRUE(ca_permission_checker_check(checker, path, 1) == CA_PERMISSION_ALLOW);
    ASSERT_TRUE(config.trusted_path_count == 1);
    ASSERT_TRUE(ca_permission_response_parse("never", &response) == CA_OK);
    ASSERT_TRUE(response == CA_PERMISSION_RESPONSE_DENY_ALWAYS);

    ca_permission_checker_free(checker);
    ca_config_free(&config);
    return 0;
}

static int test_cost_tracker_render(void) {
    ca_cost_tracker tracker;
    char *out = NULL;
    double cost = 0.0;
    ca_cost_tracker_init(&tracker, "gpt-4.1-mini");
    ASSERT_TRUE(ca_cost_tracker_add(&tracker, 1000, 500) == CA_OK);
    ASSERT_TRUE(ca_cost_tracker_total_usd(&tracker, &cost) == CA_OK);
    ASSERT_TRUE(cost > 0.0);
    ASSERT_TRUE(ca_cost_tracker_render(&tracker, &out) == CA_OK);
    ASSERT_TRUE(strstr(out, "Input tokens: 1000") != NULL);
    ASSERT_TRUE(strstr(out, "Output tokens: 500") != NULL);
    free(out);
    ca_cost_tracker_free(&tracker);
    return 0;
}

static int test_obsidian_write_note(void) {
    char dir[128];
    char *path = NULL;
    ca_note_metadata metadata;
    const char *tags[] = { "codeagent", "test" };
    snprintf(dir, sizeof(dir), "/tmp/codeagent-vault-%ld", (long)getpid());
    ASSERT_TRUE(mkdir(dir, 0777) == 0 || access(dir, F_OK) == 0);
    memset(&metadata, 0, sizeof(metadata));
    metadata.note_type = CA_NOTE_REFERENCE;
    metadata.tags = tags;
    metadata.tag_count = 2;
    ASSERT_TRUE(ca_obsidian_write_note(dir, "My Test Note", "body text", &metadata, &path) == CA_OK);
    ASSERT_TRUE(path != NULL);
    ASSERT_TRUE(access(path, F_OK) == 0);
    remove(path);
    free(path);
    {
        char notes_dir[160];
        snprintf(notes_dir, sizeof(notes_dir), "%s/Notes", dir);
        rmdir(notes_dir);
    }
    rmdir(dir);
    return 0;
}

static int test_git_status_and_message(void) {
    char dir[128];
    char cmd[512];
    char file_path[160];
    ca_git_status status;
    char *rendered = NULL;
    char *message = NULL;
    char *groups = NULL;
    snprintf(dir, sizeof(dir), "/tmp/codeagent-git-%ld", (long)getpid());
    ASSERT_TRUE(mkdir(dir, 0777) == 0 || access(dir, F_OK) == 0);
    snprintf(cmd, sizeof(cmd), "git -C %s init >/dev/null 2>&1", dir);
    ASSERT_TRUE(system(cmd) == 0);
    snprintf(file_path, sizeof(file_path), "%s/src.c", dir);
    ASSERT_TRUE(write_test_file(file_path, "int main(void){return 0;}") == 0);
    ASSERT_TRUE(ca_git_status_load(dir, &status) == CA_OK);
    ASSERT_TRUE(status.file_count == 1);
    ASSERT_TRUE(status.files[0].status == CA_GIT_FILE_UNTRACKED);
    ASSERT_TRUE(ca_git_status_render(&status, &rendered) == CA_OK);
    ASSERT_TRUE(strstr(rendered, "src.c") != NULL);
    ASSERT_TRUE(ca_git_generate_commit_message(&status, &message) == CA_OK);
    ASSERT_TRUE(strstr(message, "implementation") != NULL);
    ASSERT_TRUE(ca_git_group_summary(&status, &groups) == CA_OK);
    ASSERT_TRUE(strstr(groups, "implementation") != NULL);
    free(groups);
    free(message);
    free(rendered);
    ca_git_status_free(&status);
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    ASSERT_TRUE(system(cmd) == 0);
    return 0;
}

static int test_agent_manager_progress(void) {
    ca_agent_manager *manager = ca_agent_manager_new();
    ca_agent_task_snapshot *tasks = NULL;
    size_t count = 0;
    char *rendered = NULL;
    unsigned id = 0;
    ASSERT_TRUE(manager != NULL);
    ASSERT_TRUE(ca_agent_manager_spawn(manager, "fix", "repair compiler error", &id) == CA_OK);
    ASSERT_TRUE(id > 0);
    ASSERT_TRUE(ca_agent_manager_update(manager, id, CA_MANAGED_AGENT_RUNNING, 40, "diagnosing") == CA_OK);
    ASSERT_TRUE(ca_agent_manager_list(manager, &tasks, &count) == CA_OK);
    ASSERT_TRUE(count == 1);
    ASSERT_TRUE(tasks[0].state == CA_MANAGED_AGENT_RUNNING);
    ASSERT_TRUE(tasks[0].progress == 40);
    ca_agent_task_snapshots_free(tasks, count);
    ASSERT_TRUE(ca_agent_manager_render(manager, &rendered) == CA_OK);
    ASSERT_TRUE(strstr(rendered, "fix") != NULL);
    free(rendered);
    ASSERT_TRUE(ca_agent_manager_cancel(manager, id) == CA_OK);
    ca_agent_manager_free(manager);
    return 0;
}

static int test_diagnostic_and_autofix(void) {
    const char *output = "src/main.c:1:10: fatal error: 'missing.h' file not found\n";
    ca_diagnostic diagnostic;
    char *suggestion = NULL;
    char *summary = NULL;
    char dir[128];
    char *path = NULL;
    ASSERT_TRUE(ca_diagnostic_parse(output, &diagnostic) == CA_OK);
    ASSERT_TRUE(strcmp(diagnostic.severity, "error") == 0);
    ASSERT_TRUE(diagnostic.code != NULL && strcmp(diagnostic.code, "missing_header") == 0);
    ASSERT_TRUE(ca_auto_fix_should_apply(output));
    ASSERT_TRUE(ca_fix_agent_suggest(output, &suggestion) == CA_OK);
    ASSERT_TRUE(strstr(suggestion, "safe auto-fix") != NULL);
    ASSERT_TRUE(ca_auto_fix_apply_safe(".", output, &summary) == CA_OK);
    ASSERT_TRUE(strstr(summary, "Safe auto-fix boundary") != NULL);
    snprintf(dir, sizeof(dir), "/tmp/codeagent-regression-%ld", (long)getpid());
    ASSERT_TRUE(ca_regression_test_generate(dir, output, &path) == CA_OK);
    ASSERT_TRUE(path != NULL && access(path, F_OK) == 0);
    remove(path);
    rmdir(dir);
    free(path);
    free(summary);
    free(suggestion);
    ca_diagnostic_free(&diagnostic);
    return 0;
}

int main(void) {
    ASSERT_TRUE(test_state_machine_text_response() == 0);
    ASSERT_TRUE(test_tool_edit_create() == 0);
    ASSERT_TRUE(test_builtin_tool_schemas_are_specific() == 0);
    ASSERT_TRUE(test_read_file_truncation_includes_line_count() == 0);
    ASSERT_TRUE(test_bash_stdout_stderr_behavior() == 0);
    ASSERT_TRUE(test_code_search_uses_argv_and_limits_matches() == 0);
    ASSERT_TRUE(test_list_files_pretty_json() == 0);
    ASSERT_TRUE(test_write_edit_permission_checks() == 0);
    ASSERT_TRUE(test_agent_conversation_loop_in_library() == 0);
    ASSERT_TRUE(test_agent_state_machine_tool_loop() == 0);
    ASSERT_TRUE(test_agent_llm_retry() == 0);
    ASSERT_TRUE(test_provider_discovery() == 0);
    ASSERT_TRUE(test_config_provider_options() == 0);
    ASSERT_TRUE(test_version_and_tool_registry_api() == 0);
    ASSERT_TRUE(test_storage_adapter_api() == 0);
    ASSERT_TRUE(test_anthropic_request_serializes_tools() == 0);
    ASSERT_TRUE(test_anthropic_parse_content_blocks() == 0);
    ASSERT_TRUE(test_command_discovery() == 0);
    ASSERT_TRUE(test_session_metrics_and_permissions() == 0);
    ASSERT_TRUE(test_session_manager_roundtrip_latest_list() == 0);
    ASSERT_TRUE(test_permission_checker_cache_and_prompt_parse() == 0);
    ASSERT_TRUE(test_cost_tracker_render() == 0);
    ASSERT_TRUE(test_obsidian_write_note() == 0);
    ASSERT_TRUE(test_git_status_and_message() == 0);
    ASSERT_TRUE(test_agent_manager_progress() == 0);
    ASSERT_TRUE(test_diagnostic_and_autofix() == 0);
    puts("codeagent C tests passed");
    return 0;
}
