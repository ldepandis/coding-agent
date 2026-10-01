/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"
#include "provider.h"

#include <stdlib.h>
#include <string.h>

struct ca_agent {
    ca_config config;
    ca_agent_callbacks callbacks;
    ca_tool_definition *tools;
    size_t tool_count;
    ca_state_machine *machine;
    size_t context_tokens;
};

static ca_status clone_tools(const ca_tool_definition *src,
                             size_t count,
                             ca_tool_definition **dst) {
    size_t i;
    *dst = NULL;
    if (count == 0) {
        return CA_OK;
    }
    *dst = (ca_tool_definition *)calloc(count, sizeof(ca_tool_definition));
    if (*dst == NULL) {
        return CA_NO_MEMORY;
    }
    for (i = 0; i < count; i++) {
        (*dst)[i].name = ca_strdup(src[i].name);
        (*dst)[i].description = ca_strdup(src[i].description);
        (*dst)[i].input_schema_json = ca_strdup(src[i].input_schema_json);
        (*dst)[i].function = src[i].function;
        (*dst)[i].userdata = src[i].userdata;
    }
    return CA_OK;
}

static void update_token_estimate(ca_agent *agent, const char *text) {
    if (text != NULL) {
        agent->context_tokens += (strlen(text) / 4u) + 1u;
    }
}

static void display(ca_ui_fn fn, const char *text, void *userdata) {
    if (fn != NULL) {
        fn(text == NULL ? "" : text, userdata);
    }
}

static void bind_builtin_tool_context(ca_agent *agent) {
    size_t i;
    if (agent == NULL) {
        return;
    }
    for (i = 0; i < agent->tool_count; i++) {
        if ((strcmp(agent->tools[i].name, "write_file") == 0 &&
             agent->tools[i].function == ca_tool_write_file) ||
            (strcmp(agent->tools[i].name, "edit_file") == 0 &&
             agent->tools[i].function == ca_tool_edit_file)) {
            agent->tools[i].userdata = &agent->config;
        }
    }
}

static int tool_status_retryable(ca_status status, const char *output) {
    ca_tool_error_category category;
    if (status == CA_OK) {
        return 0;
    }
    category = ca_tool_categorize_error(output == NULL ? ca_status_string(status) : output);
    return category == CA_TOOL_ERROR_NETWORK ||
           category == CA_TOOL_ERROR_RESOURCE ||
           category == CA_TOOL_ERROR_TIMEOUT;
}

static ca_status execute_tool_with_retry(ca_agent *agent,
                                         const ca_tool_call *call,
                                         char **output) {
    unsigned attempt = 0;
    ca_status status = CA_ERROR;
    if (output == NULL || call == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *output = NULL;
    for (;;) {
        free(*output);
        *output = NULL;
        display(agent->callbacks.tool_started, call->tool_name, agent->callbacks.userdata);
        status = ca_tool_execute(agent->tools,
                                 agent->tool_count,
                                 call->tool_name,
                                 call->input_json,
                                 output);
        display(agent->callbacks.tool_finished,
                *output == NULL ? ca_status_string(status) : *output,
                agent->callbacks.userdata);
        if (!tool_status_retryable(status, *output) || attempt >= agent->config.max_tool_retries) {
            return status;
        }
        attempt++;
        display(agent->callbacks.display_warning,
                "Retrying tool after retryable failure",
                agent->callbacks.userdata);
        ca_sleep_ms(agent->config.tool_retry_delay_ms * attempt);
    }
}

static void run_post_tools_hooks(ca_agent *agent,
                                 const char *const *tool_names,
                                 size_t tool_name_count,
                                 int *proceed) {
    char *warning = NULL;
    unsigned percent;
    *proceed = 1;
    if (agent->callbacks.post_tools_hook != NULL) {
        int callback_proceed = 1;
        size_t conversation_count = 0;
        const ca_message *conversation = ca_agent_conversation(agent, &conversation_count);
        if (agent->callbacks.post_tools_hook(conversation,
                                             conversation_count,
                                             tool_names,
                                             tool_name_count,
                                             &warning,
                                             &callback_proceed,
                                             agent->callbacks.userdata) == CA_OK) {
            *proceed = callback_proceed;
        }
    }
    if (warning != NULL) {
        display(agent->callbacks.display_warning, warning, agent->callbacks.userdata);
        free(warning);
    }
    percent = agent->config.context_window == 0
                  ? 0
                  : (unsigned)((agent->context_tokens * 100u) / agent->config.context_window);
    if (percent >= 70u) {
        display(agent->callbacks.display_warning,
                "Context at 70% or more - consider /clear or /land soon",
                agent->callbacks.userdata);
    } else if (percent >= 60u) {
        display(agent->callbacks.display_warning,
                "Context at 60% or more - approaching limit",
                agent->callbacks.userdata);
    }
}

ca_agent *ca_agent_new(const ca_config *config,
                       const ca_agent_callbacks *callbacks,
                       ca_tool_definition *tools,
                       size_t tool_count) {
    ca_agent *agent = (ca_agent *)calloc(1, sizeof(ca_agent));
    if (agent == NULL) {
        return NULL;
    }
    if (ca_config_clone(config, &agent->config) != CA_OK) {
        free(agent);
        return NULL;
    }
    if (callbacks != NULL) {
        agent->callbacks = *callbacks;
    }
    if (tools != NULL && tool_count > 0) {
        if (clone_tools(tools, tool_count, &agent->tools) != CA_OK) {
            ca_agent_free(agent);
            return NULL;
        }
        agent->tool_count = tool_count;
    } else if (ca_builtin_tools(&agent->tools, &agent->tool_count) != CA_OK) {
        ca_agent_free(agent);
        return NULL;
    }
    bind_builtin_tool_context(agent);
    agent->machine = ca_state_machine_new();
    if (agent->machine == NULL) {
        ca_agent_free(agent);
        return NULL;
    }
    return agent;
}

void ca_agent_free(ca_agent *agent) {
    if (agent == NULL) {
        return;
    }
    ca_config_free(&agent->config);
    ca_tool_definitions_free(agent->tools, agent->tool_count);
    ca_state_machine_free(agent->machine);
    free(agent);
}

ca_status ca_agent_submit(ca_agent *agent, const char *user_text) {
    ca_agent_event event;
    ca_agent_action action;
    size_t iteration = 0;
    ca_status last_provider_status = CA_OK;

    if (agent == NULL || user_text == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    memset(&event, 0, sizeof(event));
    memset(&action, 0, sizeof(action));
    event.type = CA_EVENT_USER_INPUT;
    event.text = (char *)user_text;
    if (ca_state_machine_handle(agent->machine, &event, &action) != CA_OK) {
        return CA_ERROR;
    }
    update_token_estimate(agent, user_text);

    for (;;) {
        ca_status status;

        switch (action.type) {
        case CA_ACTION_SEND_LLM_REQUEST: {
            ca_provider_request request;
            ca_provider_response response;
            char *display_text = action.text;
            action.text = NULL;
            ca_agent_action_free(&action);
            if (display_text != NULL && display_text[0] != '\0') {
                display(agent->callbacks.display_text, display_text, agent->callbacks.userdata);
            }
            free(display_text);
            iteration++;
            if (iteration > agent->config.max_tool_iterations) {
                display(agent->callbacks.display_error,
                        "Maximum tool iterations reached. Stopping to prevent infinite loop.",
                        agent->callbacks.userdata);
                return CA_ERROR;
            }
            if (iteration == ((agent->config.max_tool_iterations * 80u) / 100u)) {
                display(agent->callbacks.display_warning,
                        "Approaching tool iteration limit",
                        agent->callbacks.userdata);
            }
            memset(&request, 0, sizeof(request));
            memset(&response, 0, sizeof(response));
            request.messages = ca_state_machine_state(agent->machine)->conversation;
            request.message_count = ca_state_machine_state(agent->machine)->conversation_count;
            request.system_prompt = "You are a helpful coding agent.";
            request.model = agent->config.model;
            request.config = &agent->config;
            request.tools = agent->tools;
            request.tool_count = agent->tool_count;
            status = ca_provider_call(agent->config.provider, &request, &response);
            memset(&event, 0, sizeof(event));
            if (status == CA_OK) {
                size_t i;
                event.type = CA_EVENT_LLM_COMPLETED;
                event.content = response.content;
                event.content_count = response.content_count;
                event.stop_reason = response.stop_reason;
                for (i = 0; i < response.content_count; i++) {
                    if (response.content[i].type == CA_BLOCK_TEXT) {
                        update_token_estimate(agent, response.content[i].text);
                    }
                }
                if (ca_state_machine_handle(agent->machine, &event, &action) != CA_OK) {
                    for (i = 0; i < response.content_count; i++) {
                        ca_content_block_free(&response.content[i]);
                    }
                    free(response.content);
                    free(response.stop_reason);
                    return CA_ERROR;
                }
                for (i = 0; i < response.content_count; i++) {
                    ca_content_block_free(&response.content[i]);
                }
                free(response.content);
                free(response.stop_reason);
            } else {
                last_provider_status = status;
                event.type = CA_EVENT_LLM_ERROR;
                event.error = response.error_message == NULL
                                  ? (char *)ca_status_string(status)
                                  : response.error_message;
                event.retry_limit = agent->config.max_llm_retries;
                event.retry_delay_ms = agent->config.llm_retry_delay_ms;
                if (ca_state_machine_handle(agent->machine, &event, &action) != CA_OK) {
                    free(response.error_message);
                    return status;
                }
                free(response.error_message);
            }
            break;
        }
        case CA_ACTION_EXECUTE_TOOLS: {
            ca_agent_action next_action;
            size_t i;
            if (action.text != NULL && action.text[0] != '\0') {
                display(agent->callbacks.display_text, action.text, agent->callbacks.userdata);
            }
            memset(&next_action, 0, sizeof(next_action));
            for (i = 0; i < action.tool_call_count; i++) {
                char *output = NULL;
                ca_status tool_status = execute_tool_with_retry(agent, &action.tool_calls[i], &output);
                ca_agent_action_free(&next_action);
                memset(&event, 0, sizeof(event));
                event.type = CA_EVENT_TOOL_COMPLETED;
                event.call_id = action.tool_calls[i].call_id;
                event.result = output == NULL ? (char *)ca_status_string(tool_status) : output;
                event.error = tool_status == CA_OK ? NULL : event.result;
                if (ca_state_machine_handle(agent->machine, &event, &next_action) != CA_OK) {
                    free(output);
                    ca_agent_action_free(&action);
                    return CA_ERROR;
                }
                update_token_estimate(agent, event.result);
                free(output);
            }
            ca_agent_action_free(&action);
            action = next_action;
            break;
        }
        case CA_ACTION_RUN_POST_TOOLS_HOOKS: {
            int proceed = 1;
            run_post_tools_hooks(agent,
                                 (const char *const *)action.tool_names,
                                 action.tool_name_count,
                                 &proceed);
            ca_agent_action_free(&action);
            memset(&event, 0, sizeof(event));
            event.type = CA_EVENT_HOOKS_COMPLETED;
            event.proceed = proceed;
            if (ca_state_machine_handle(agent->machine, &event, &action) != CA_OK) {
                return CA_ERROR;
            }
            break;
        }
        case CA_ACTION_DISPLAY_TEXT:
            display(agent->callbacks.display_text, action.text, agent->callbacks.userdata);
            ca_agent_action_free(&action);
            return CA_OK;
        case CA_ACTION_DISPLAY_WARNING:
            display(agent->callbacks.display_warning, action.text, agent->callbacks.userdata);
            ca_agent_action_free(&action);
            break;
        case CA_ACTION_DISPLAY_ERROR:
            display(agent->callbacks.display_error,
                    action.text == NULL ? "LLM request failed" : action.text,
                    agent->callbacks.userdata);
            ca_agent_action_free(&action);
            return last_provider_status == CA_OK ? CA_ERROR : last_provider_status;
        case CA_ACTION_SCHEDULE_RETRY:
            ca_sleep_ms(action.delay_ms);
            ca_agent_action_free(&action);
            memset(&event, 0, sizeof(event));
            event.type = CA_EVENT_RETRY_TIMEOUT;
            if (ca_state_machine_handle(agent->machine, &event, &action) != CA_OK) {
                return CA_ERROR;
            }
            break;
        case CA_ACTION_PROMPT_FOR_INPUT:
        case CA_ACTION_WAIT_FOR_EVENT:
            ca_agent_action_free(&action);
            return CA_OK;
        case CA_ACTION_SHUTDOWN:
            ca_agent_action_free(&action);
            return CA_OK;
        }
    }
}

ca_status ca_agent_reset(ca_agent *agent) {
    if (agent == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    ca_state_machine_free(agent->machine);
    agent->machine = ca_state_machine_new();
    if (agent->machine == NULL) {
        return CA_NO_MEMORY;
    }
    agent->context_tokens = 0;
    return CA_OK;
}

ca_status ca_agent_set_provider(ca_agent *agent, const char *provider_name) {
    char *next;
    if (agent == NULL || provider_name == NULL || provider_name[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    next = ca_strdup(provider_name);
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    free(agent->config.provider);
    agent->config.provider = next;
    return CA_OK;
}

const char *ca_agent_provider(const ca_agent *agent) {
    if (agent == NULL || agent->config.provider == NULL) {
        return CODEAGENT_DEFAULT_PROVIDER;
    }
    return agent->config.provider;
}

ca_status ca_agent_set_model(ca_agent *agent, const char *model) {
    char *next;
    if (agent == NULL || model == NULL || model[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    next = ca_strdup(model);
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    free(agent->config.model);
    agent->config.model = next;
    return CA_OK;
}

const char *ca_agent_model(const ca_agent *agent) {
    if (agent == NULL || agent->config.model == NULL) {
        return CODEAGENT_DEFAULT_MODEL;
    }
    return agent->config.model;
}

const ca_message *ca_agent_conversation(const ca_agent *agent, size_t *message_count) {
    if (message_count != NULL) {
        const ca_agent_state *state = agent == NULL ? NULL : ca_state_machine_state(agent->machine);
        *message_count = state == NULL ? 0 : state->conversation_count;
    }
    return agent == NULL ? NULL : ca_state_machine_state(agent->machine)->conversation;
}

size_t ca_agent_context_tokens(const ca_agent *agent) {
    return agent == NULL ? 0 : agent->context_tokens;
}
