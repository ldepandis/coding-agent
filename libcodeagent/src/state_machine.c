/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

struct ca_state_machine {
    ca_agent_state state;
};

const char *ca_status_string(ca_status status) {
    switch (status) {
    case CA_OK:
        return "ok";
    case CA_ERROR:
        return "error";
    case CA_NO_MEMORY:
        return "out of memory";
    case CA_INVALID_ARGUMENT:
        return "invalid argument";
    case CA_IO_ERROR:
        return "I/O error";
    case CA_HTTP_ERROR:
        return "HTTP error";
    case CA_JSON_ERROR:
        return "JSON error";
    case CA_NOT_FOUND:
        return "not found";
    case CA_PERMISSION_REQUIRED:
        return "permission required";
    }
    return "unknown status";
}

const char *ca_state_name(ca_agent_state_type state) {
    switch (state) {
    case CA_STATE_WAITING_FOR_USER_INPUT:
        return "WaitingForUserInput";
    case CA_STATE_CALLING_LLM:
        return "CallingLlm";
    case CA_STATE_PROCESSING_LLM_RESPONSE:
        return "ProcessingLlmResponse";
    case CA_STATE_EXECUTING_TOOLS:
        return "ExecutingTools";
    case CA_STATE_POST_TOOLS_HOOK:
        return "PostToolsHook";
    case CA_STATE_ERROR:
        return "Error";
    case CA_STATE_SHUTTING_DOWN:
        return "ShuttingDown";
    }
    return "Unknown";
}

const char *ca_action_name(ca_agent_action_type action) {
    switch (action) {
    case CA_ACTION_SEND_LLM_REQUEST:
        return "SendLlmRequest";
    case CA_ACTION_EXECUTE_TOOLS:
        return "ExecuteTools";
    case CA_ACTION_RUN_POST_TOOLS_HOOKS:
        return "RunPostToolsHooks";
    case CA_ACTION_DISPLAY_TEXT:
        return "DisplayText";
    case CA_ACTION_DISPLAY_ERROR:
        return "DisplayError";
    case CA_ACTION_DISPLAY_WARNING:
        return "DisplayWarning";
    case CA_ACTION_PROMPT_FOR_INPUT:
        return "PromptForInput";
    case CA_ACTION_SCHEDULE_RETRY:
        return "ScheduleRetry";
    case CA_ACTION_WAIT_FOR_EVENT:
        return "WaitForEvent";
    case CA_ACTION_SHUTDOWN:
        return "Shutdown";
    }
    return "Unknown";
}

static void state_clear(ca_agent_state *state) {
    size_t i;
    if (state == NULL) {
        return;
    }
    for (i = 0; i < state->conversation_count; i++) {
        ca_message_free(&state->conversation[i]);
    }
    free(state->conversation);
    for (i = 0; i < state->response_content_count; i++) {
        ca_content_block_free(&state->response_content[i]);
    }
    free(state->response_content);
    free(state->stop_reason);
    if (state->executions != NULL) {
        for (i = 0; i < state->execution_count; i++) {
            free(state->executions[i].call_id);
            free(state->executions[i].tool_name);
            free(state->executions[i].input_json);
            free(state->executions[i].result);
            free(state->executions[i].error);
        }
    }
    free(state->executions);
    for (i = 0; i < state->pending_tool_result_count; i++) {
        ca_message_free(&state->pending_tool_results[i]);
    }
    free(state->pending_tool_results);
    free(state->error_message);
    memset(state, 0, sizeof(*state));
}

static void free_tool_calls(ca_tool_call *calls, size_t count) {
    size_t i;
    if (calls == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(calls[i].call_id);
        free(calls[i].tool_name);
        free(calls[i].input_json);
    }
    free(calls);
}

static ca_status clone_messages(const ca_message *src, size_t count, ca_message **dst) {
    size_t i;
    *dst = NULL;
    if (count == 0) {
        return CA_OK;
    }
    if (count > SIZE_MAX / sizeof(ca_message)) {
        return CA_NO_MEMORY;
    }
    *dst = (ca_message *)calloc(count, sizeof(ca_message));
    if (*dst == NULL) {
        return CA_NO_MEMORY;
    }
    for (i = 0; i < count; i++) {
        ca_status status = ca_message_clone(&src[i], &(*dst)[i]);
        if (status != CA_OK) {
            while (i > 0) {
                i--;
                ca_message_free(&(*dst)[i]);
            }
            free(*dst);
            *dst = NULL;
            return status;
        }
    }
    return CA_OK;
}

static ca_status clone_content_blocks(const ca_content_block *src, size_t count, ca_content_block **dst) {
    size_t i;
    *dst = NULL;
    if (count == 0) {
        return CA_OK;
    }
    if (count > SIZE_MAX / sizeof(ca_content_block)) {
        return CA_NO_MEMORY;
    }
    *dst = (ca_content_block *)calloc(count, sizeof(ca_content_block));
    if (*dst == NULL) {
        return CA_NO_MEMORY;
    }
    for (i = 0; i < count; i++) {
        ca_status status = ca_content_block_clone(&src[i], &(*dst)[i]);
        if (status != CA_OK) {
            while (i > 0) {
                i--;
                ca_content_block_free(&(*dst)[i]);
            }
            free(*dst);
            *dst = NULL;
            return status;
        }
    }
    return CA_OK;
}

static ca_status append_message(ca_message **messages, size_t *count, const ca_message *message) {
    ca_message *next;
    ca_status status;

    if (*count > SIZE_MAX / sizeof(ca_message) - 1) {
        return CA_NO_MEMORY;
    }
    next = (ca_message *)realloc(*messages, sizeof(ca_message) * (*count + 1));
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    *messages = next;
    memset(&(*messages)[*count], 0, sizeof(ca_message));
    status = ca_message_clone(message, &(*messages)[*count]);
    if (status != CA_OK) {
        return status;
    }
    *count += 1;
    return CA_OK;
}

static ca_status set_state(ca_state_machine *machine, ca_agent_state *next) {
    state_clear(&machine->state);
    machine->state = *next;
    memset(next, 0, sizeof(*next));
    return CA_OK;
}

static ca_status action_wait(ca_agent_action *action) {
    memset(action, 0, sizeof(*action));
    action->type = CA_ACTION_WAIT_FOR_EVENT;
    return CA_OK;
}

static ca_status action_with_messages(ca_agent_action *action,
                                      ca_agent_action_type type,
                                      const ca_message *messages,
                                      size_t count) {
    ca_status status;
    memset(action, 0, sizeof(*action));
    action->type = type;
    status = clone_messages(messages, count, &action->messages);
    if (status != CA_OK) {
        return status;
    }
    action->message_count = count;
    return CA_OK;
}

static int stop_reason_continues(const char *stop_reason) {
    return stop_reason != NULL && strcmp(stop_reason, "pause_turn") == 0;
}

static ca_status process_llm_response(ca_state_machine *machine, ca_agent_action *action) {
    ca_agent_state next;
    ca_tool_call *calls = NULL;
    size_t call_count = 0;
    size_t i;
    ca_string_builder text;
    ca_message assistant;
    ca_status status;

    ca_sb_init(&text);
    memset(&assistant, 0, sizeof(assistant));
    memset(&next, 0, sizeof(next));

    for (i = 0; i < machine->state.response_content_count; i++) {
        ca_content_block *block = &machine->state.response_content[i];
        if (block->type == CA_BLOCK_TEXT) {
            status = ca_sb_append(&text, block->text);
            if (status != CA_OK) {
                free_tool_calls(calls, call_count);
                ca_sb_free(&text);
                return status;
            }
        } else if (block->type == CA_BLOCK_TOOL_USE) {
            if (call_count > SIZE_MAX / sizeof(ca_tool_call) - 1) {
                free_tool_calls(calls, call_count);
                ca_sb_free(&text);
                return CA_NO_MEMORY;
            }
            ca_tool_call *more = (ca_tool_call *)realloc(calls, sizeof(ca_tool_call) * (call_count + 1));
            if (more == NULL) {
                free_tool_calls(calls, call_count);
                ca_sb_free(&text);
                return CA_NO_MEMORY;
            }
            calls = more;
            calls[call_count].call_id = ca_strdup(block->id);
            calls[call_count].tool_name = ca_strdup(block->name);
            calls[call_count].input_json = ca_strdup(block->input_json);
            if (calls[call_count].call_id == NULL || calls[call_count].tool_name == NULL ||
                calls[call_count].input_json == NULL) {
                free_tool_calls(calls, call_count + 1);
                ca_sb_free(&text);
                return CA_NO_MEMORY;
            }
            call_count++;
        }
    }

    assistant = ca_message_assistant(machine->state.response_content, machine->state.response_content_count);
    status = clone_messages(machine->state.conversation, machine->state.conversation_count, &next.conversation);
    if (status != CA_OK) {
        free_tool_calls(calls, call_count);
        ca_sb_free(&text);
        return status;
    }
    next.conversation_count = machine->state.conversation_count;
    status = append_message(&next.conversation, &next.conversation_count, &assistant);
    ca_message_free(&assistant);
    if (status != CA_OK) {
        state_clear(&next);
        free_tool_calls(calls, call_count);
        ca_sb_free(&text);
        return status;
    }

    memset(action, 0, sizeof(*action));
    if (call_count > 0) {
        char *display = ca_sb_take(&text);
        next.type = CA_STATE_EXECUTING_TOOLS;
        if (call_count > SIZE_MAX / sizeof(ca_tool_execution)) {
            state_clear(&next);
            free_tool_calls(calls, call_count);
            free(display);
            return CA_NO_MEMORY;
        }
        next.executions = (ca_tool_execution *)calloc(call_count, sizeof(ca_tool_execution));
        if (next.executions == NULL) {
            state_clear(&next);
            free_tool_calls(calls, call_count);
            free(display);
            return CA_NO_MEMORY;
        }
        next.execution_count = call_count;
        for (i = 0; i < call_count; i++) {
            next.executions[i].state = CA_TOOL_PENDING;
            next.executions[i].call_id = ca_strdup(calls[i].call_id);
            next.executions[i].tool_name = ca_strdup(calls[i].tool_name);
            next.executions[i].input_json = ca_strdup(calls[i].input_json);
            if (next.executions[i].call_id == NULL || next.executions[i].tool_name == NULL ||
                next.executions[i].input_json == NULL) {
                state_clear(&next);
                free_tool_calls(calls, call_count);
                free(display);
                return CA_NO_MEMORY;
            }
        }
        action->type = CA_ACTION_EXECUTE_TOOLS;
        action->tool_calls = calls;
        action->tool_call_count = call_count;
        if (display != NULL && display[0] != '\0') {
            action->text = display;
        } else {
            free(display);
        }
        return set_state(machine, &next);
    }

    {
        char *display = ca_sb_take(&text);
        if (stop_reason_continues(machine->state.stop_reason)) {
            next.type = CA_STATE_CALLING_LLM;
            status = set_state(machine, &next);
            if (status != CA_OK) {
                free(display);
                return status;
            }
            status = action_with_messages(action,
                                          CA_ACTION_SEND_LLM_REQUEST,
                                          machine->state.conversation,
                                          machine->state.conversation_count);
            if (status != CA_OK) {
                free(display);
                return status;
            }
            if (display != NULL && display[0] != '\0') {
                action->text = display;
            } else {
                free(display);
            }
            return CA_OK;
        }
        next.type = CA_STATE_WAITING_FOR_USER_INPUT;
        if (display != NULL && display[0] != '\0') {
            action->type = CA_ACTION_DISPLAY_TEXT;
            action->text = display;
        } else {
            free(display);
            action->type = CA_ACTION_PROMPT_FOR_INPUT;
        }
    }
    return set_state(machine, &next);
}

ca_state_machine *ca_state_machine_new(void) {
    ca_state_machine *machine = (ca_state_machine *)calloc(1, sizeof(ca_state_machine));
    if (machine == NULL) {
        return NULL;
    }
    machine->state.type = CA_STATE_WAITING_FOR_USER_INPUT;
    return machine;
}

void ca_state_machine_free(ca_state_machine *machine) {
    if (machine == NULL) {
        return;
    }
    state_clear(&machine->state);
    free(machine);
}

const ca_agent_state *ca_state_machine_state(const ca_state_machine *machine) {
    return machine == NULL ? NULL : &machine->state;
}

ca_status ca_state_machine_handle(ca_state_machine *machine,
                                  const ca_agent_event *event,
                                  ca_agent_action *action) {
    ca_agent_state next;
    ca_status status;

    if (machine == NULL || event == NULL || action == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    memset(action, 0, sizeof(*action));

    if (event->type == CA_EVENT_SHUTDOWN_REQUESTED) {
        state_clear(&machine->state);
        machine->state.type = CA_STATE_SHUTTING_DOWN;
        action->type = CA_ACTION_SHUTDOWN;
        return CA_OK;
    }

    memset(&next, 0, sizeof(next));
    switch (machine->state.type) {
    case CA_STATE_WAITING_FOR_USER_INPUT:
        if (event->type == CA_EVENT_USER_INPUT) {
            ca_message user = ca_message_user(event->text);
            status = clone_messages(machine->state.conversation,
                                    machine->state.conversation_count,
                                    &next.conversation);
            if (status != CA_OK) {
                ca_message_free(&user);
                return status;
            }
            next.conversation_count = machine->state.conversation_count;
            status = append_message(&next.conversation, &next.conversation_count, &user);
            ca_message_free(&user);
            if (status != CA_OK) {
                state_clear(&next);
                return status;
            }
            next.type = CA_STATE_CALLING_LLM;
            next.retries = 0;
            set_state(machine, &next);
            return action_with_messages(action,
                                        CA_ACTION_SEND_LLM_REQUEST,
                                        machine->state.conversation,
                                        machine->state.conversation_count);
        }
        return action_wait(action);

    case CA_STATE_CALLING_LLM:
        if (event->type == CA_EVENT_LLM_COMPLETED) {
            next.type = CA_STATE_PROCESSING_LLM_RESPONSE;
            status = clone_messages(machine->state.conversation,
                                    machine->state.conversation_count,
                                    &next.conversation);
            if (status != CA_OK) {
                return status;
            }
            next.conversation_count = machine->state.conversation_count;
            status = clone_content_blocks(event->content, event->content_count, &next.response_content);
            if (status != CA_OK) {
                state_clear(&next);
                return status;
            }
            next.response_content_count = event->content_count;
            next.stop_reason = ca_strdup(event->stop_reason);
            set_state(machine, &next);
            return process_llm_response(machine, action);
        }
        if (event->type == CA_EVENT_LLM_ERROR) {
            unsigned retry_limit = event->retry_limit;
            unsigned retry_delay_ms = event->retry_delay_ms;
            if (machine->state.retries < retry_limit) {
                next.type = CA_STATE_ERROR;
                status = clone_messages(machine->state.conversation,
                                        machine->state.conversation_count,
                                        &next.conversation);
                if (status != CA_OK) {
                    return status;
                }
                next.conversation_count = machine->state.conversation_count;
                next.error_message = ca_strdup(event->error);
                next.retries = machine->state.retries;
                set_state(machine, &next);
                action->type = CA_ACTION_SCHEDULE_RETRY;
                action->delay_ms = retry_delay_ms * (next.retries + 1u);
                return CA_OK;
            }
            next.type = CA_STATE_WAITING_FOR_USER_INPUT;
            status = clone_messages(machine->state.conversation,
                                    machine->state.conversation_count,
                                    &next.conversation);
            if (status != CA_OK) {
                return status;
            }
            next.conversation_count = machine->state.conversation_count;
            set_state(machine, &next);
            action->type = CA_ACTION_DISPLAY_ERROR;
            ca_set_error(&action->text,
                         "LLM request failed after %u retries: %s",
                         retry_limit,
                         event->error == NULL ? "" : event->error);
            return CA_OK;
        }
        return action_wait(action);

    case CA_STATE_ERROR:
        if (event->type == CA_EVENT_RETRY_TIMEOUT) {
            next.type = CA_STATE_CALLING_LLM;
            status = clone_messages(machine->state.conversation,
                                    machine->state.conversation_count,
                                    &next.conversation);
            if (status != CA_OK) {
                return status;
            }
            next.conversation_count = machine->state.conversation_count;
            next.retries = machine->state.retries + 1u;
            set_state(machine, &next);
            return action_with_messages(action,
                                        CA_ACTION_SEND_LLM_REQUEST,
                                        machine->state.conversation,
                                        machine->state.conversation_count);
        }
        return action_wait(action);

    case CA_STATE_EXECUTING_TOOLS:
        if (event->type == CA_EVENT_TOOL_COMPLETED) {
            size_t completed = 0;
            size_t tool_name_count = 0;
            size_t i;
            ca_message *results = NULL;
            ca_content_block *result_blocks = NULL;
            char **tool_names = NULL;
            status = clone_messages(machine->state.conversation,
                                    machine->state.conversation_count,
                                    &next.conversation);
            if (status != CA_OK) {
                return status;
            }
            next.type = CA_STATE_EXECUTING_TOOLS;
            next.conversation_count = machine->state.conversation_count;
            next.execution_count = machine->state.execution_count;
            if (next.execution_count > SIZE_MAX / sizeof(ca_tool_execution)) {
                state_clear(&next);
                return CA_NO_MEMORY;
            }
            next.executions = (ca_tool_execution *)calloc(next.execution_count, sizeof(ca_tool_execution));
            if (next.executions == NULL) {
                state_clear(&next);
                return CA_NO_MEMORY;
            }
            for (i = 0; i < next.execution_count; i++) {
                ca_tool_execution *dst = &next.executions[i];
                ca_tool_execution *src = &machine->state.executions[i];
                dst->state = src->state;
                dst->call_id = ca_strdup(src->call_id);
                dst->tool_name = ca_strdup(src->tool_name);
                dst->input_json = ca_strdup(src->input_json);
                dst->result = ca_strdup(src->result);
                dst->error = ca_strdup(src->error);
                if (strcmp(dst->call_id, event->call_id == NULL ? "" : event->call_id) == 0) {
                    dst->state = CA_TOOL_COMPLETED;
                    free(dst->result);
                    free(dst->error);
                    dst->result = event->error == NULL ? ca_strdup(event->result) : NULL;
                    dst->error = event->error != NULL ? ca_strdup(event->error) : NULL;
                }
                if (dst->state == CA_TOOL_COMPLETED) {
                    completed++;
                }
            }
            if (completed != next.execution_count) {
                set_state(machine, &next);
                return action_wait(action);
            }
            tool_name_count = next.execution_count;

            if (tool_name_count > SIZE_MAX / sizeof(ca_content_block) ||
                tool_name_count > SIZE_MAX / sizeof(char *)) {
                state_clear(&next);
                return CA_NO_MEMORY;
            }
            results = (ca_message *)calloc(1, sizeof(ca_message));
            result_blocks = (ca_content_block *)calloc(tool_name_count, sizeof(ca_content_block));
            tool_names = (char **)calloc(tool_name_count, sizeof(char *));
            if (results == NULL || result_blocks == NULL || tool_names == NULL) {
                free(results);
                free(result_blocks);
                free(tool_names);
                state_clear(&next);
                return CA_NO_MEMORY;
            }
            results[0].blocks = result_blocks;
            results[0].block_count = tool_name_count;
            for (i = 0; i < tool_name_count; i++) {
                ca_tool_execution *exec = &next.executions[i];
                result_blocks[i] = ca_content_tool_result(exec->call_id,
                                                          exec->error == NULL ? exec->result : exec->error,
                                                          exec->error != NULL);
                tool_names[i] = ca_strdup(exec->tool_name);
                if (tool_names[i] == NULL) {
                    size_t j;
                    for (j = 0; j < i; j++) {
                        free(tool_names[j]);
                    }
                    free(tool_names);
                    ca_message_free(&results[0]);
                    free(results);
                    state_clear(&next);
                    return CA_NO_MEMORY;
                }
            }
            results[0].role = ca_strdup("user");
            if (results[0].role == NULL) {
                for (i = 0; i < tool_name_count; i++) {
                    free(tool_names[i]);
                }
                free(tool_names);
                ca_message_free(&results[0]);
                free(results);
                state_clear(&next);
                return CA_NO_MEMORY;
            }
            state_clear(&next);
            next.type = CA_STATE_POST_TOOLS_HOOK;
            status = clone_messages(machine->state.conversation,
                                    machine->state.conversation_count,
                                    &next.conversation);
            if (status != CA_OK) {
                for (i = 0; i < tool_name_count; i++) {
                    free(tool_names[i]);
                }
                free(tool_names);
                ca_message_free(&results[0]);
                free(results);
                return status;
            }
            next.conversation_count = machine->state.conversation_count;
            status = append_message(&next.conversation,
                                    &next.conversation_count,
                                    &results[0]);
            ca_message_free(&results[0]);
            free(results);
            if (status != CA_OK) {
                for (i = 0; i < tool_name_count; i++) {
                    free(tool_names[i]);
                }
                free(tool_names);
                state_clear(&next);
                return status;
            }
            set_state(machine, &next);
            action->type = CA_ACTION_RUN_POST_TOOLS_HOOKS;
            action->tool_names = tool_names;
            action->tool_name_count = tool_name_count;
            return CA_OK;
        }
        return action_wait(action);

    case CA_STATE_POST_TOOLS_HOOK:
        if (event->type == CA_EVENT_HOOKS_COMPLETED) {
            status = clone_messages(machine->state.conversation,
                                    machine->state.conversation_count,
                                    &next.conversation);
            if (status != CA_OK) {
                return status;
            }
            next.conversation_count = machine->state.conversation_count;
            if (event->proceed) {
                next.type = CA_STATE_CALLING_LLM;
                set_state(machine, &next);
                if (event->warning != NULL) {
                    action->type = CA_ACTION_DISPLAY_WARNING;
                    action->text = ca_strdup(event->warning);
                    return action->text == NULL ? CA_NO_MEMORY : CA_OK;
                }
                return action_with_messages(action,
                                            CA_ACTION_SEND_LLM_REQUEST,
                                            machine->state.conversation,
                                            machine->state.conversation_count);
            }
            next.type = CA_STATE_WAITING_FOR_USER_INPUT;
            set_state(machine, &next);
            if (event->warning != NULL) {
                action->type = CA_ACTION_DISPLAY_WARNING;
                action->text = ca_strdup(event->warning);
            } else {
                action->type = CA_ACTION_PROMPT_FOR_INPUT;
            }
            return CA_OK;
        }
        return action_wait(action);

    default:
        return action_wait(action);
    }
}

void ca_agent_action_free(ca_agent_action *action) {
    size_t i;
    if (action == NULL) {
        return;
    }
    for (i = 0; i < action->message_count; i++) {
        ca_message_free(&action->messages[i]);
    }
    free(action->messages);
    free_tool_calls(action->tool_calls, action->tool_call_count);
    for (i = 0; i < action->tool_name_count; i++) {
        free(action->tool_names[i]);
    }
    free(action->tool_names);
    free(action->text);
    memset(action, 0, sizeof(*action));
}
