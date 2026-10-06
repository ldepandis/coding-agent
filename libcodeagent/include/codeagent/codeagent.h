/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#ifndef CODEAGENT_CODEAGENT_H
#define CODEAGENT_CODEAGENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CODEAGENT_VERSION "0.1.0"
#define CODEAGENT_VERSION_MAJOR 0u
#define CODEAGENT_VERSION_MINOR 1u
#define CODEAGENT_VERSION_PATCH 0u
#define CODEAGENT_ABI_VERSION 0u
#define CODEAGENT_MAX_RETRIES 3u
#define CODEAGENT_RETRY_DELAY_MS 1000u
#define CODEAGENT_DEFAULT_PROVIDER "anthropic"
#define CODEAGENT_DEFAULT_MODEL "claude-sonnet-4-20250514"

typedef enum {
    CA_OK = 0,
    CA_ERROR = 1,
    CA_NO_MEMORY = 2,
    CA_INVALID_ARGUMENT = 3,
    CA_IO_ERROR = 4,
    CA_HTTP_ERROR = 5,
    CA_JSON_ERROR = 6,
    CA_NOT_FOUND = 7,
    CA_PERMISSION_REQUIRED = 8
} ca_status;

typedef enum {
    CA_BLOCK_TEXT,
    CA_BLOCK_TOOL_USE,
    CA_BLOCK_TOOL_RESULT
} ca_content_block_type;

typedef enum {
    CA_STATE_WAITING_FOR_USER_INPUT,
    CA_STATE_CALLING_LLM,
    CA_STATE_PROCESSING_LLM_RESPONSE,
    CA_STATE_EXECUTING_TOOLS,
    CA_STATE_POST_TOOLS_HOOK,
    CA_STATE_ERROR,
    CA_STATE_SHUTTING_DOWN
} ca_agent_state_type;

typedef enum {
    CA_EVENT_USER_INPUT,
    CA_EVENT_LLM_COMPLETED,
    CA_EVENT_LLM_ERROR,
    CA_EVENT_TOOL_COMPLETED,
    CA_EVENT_HOOKS_COMPLETED,
    CA_EVENT_RETRY_TIMEOUT,
    CA_EVENT_SHUTDOWN_REQUESTED
} ca_agent_event_type;

typedef enum {
    CA_ACTION_SEND_LLM_REQUEST,
    CA_ACTION_EXECUTE_TOOLS,
    CA_ACTION_RUN_POST_TOOLS_HOOKS,
    CA_ACTION_DISPLAY_TEXT,
    CA_ACTION_DISPLAY_ERROR,
    CA_ACTION_DISPLAY_WARNING,
    CA_ACTION_PROMPT_FOR_INPUT,
    CA_ACTION_SCHEDULE_RETRY,
    CA_ACTION_WAIT_FOR_EVENT,
    CA_ACTION_SHUTDOWN
} ca_agent_action_type;

typedef enum {
    CA_TOOL_PENDING,
    CA_TOOL_RUNNING,
    CA_TOOL_COMPLETED
} ca_tool_execution_state;

typedef enum {
    CA_TOOL_ERROR_CODE,
    CA_TOOL_ERROR_PERMISSION,
    CA_TOOL_ERROR_NETWORK,
    CA_TOOL_ERROR_RESOURCE,
    CA_TOOL_ERROR_TIMEOUT,
    CA_TOOL_ERROR_UNKNOWN
} ca_tool_error_category;

typedef enum {
    CA_PERMISSION_ALLOW,
    CA_PERMISSION_PROMPT,
    CA_PERMISSION_DENY
} ca_permission_result;

typedef enum {
    CA_PERMISSION_RESPONSE_ALLOW_ONCE,
    CA_PERMISSION_RESPONSE_DENY_ONCE,
    CA_PERMISSION_RESPONSE_ALLOW_ALWAYS,
    CA_PERMISSION_RESPONSE_DENY_ALWAYS,
    CA_PERMISSION_RESPONSE_CANCEL
} ca_permission_response;

typedef enum {
    CA_SANDBOX_DISABLED,
    CA_SANDBOX_READ_ONLY,
    CA_SANDBOX_WORKSPACE_WRITE,
    CA_SANDBOX_FULL_ACCESS
} ca_sandbox_mode;

typedef enum {
    CA_GIT_FILE_MODIFIED,
    CA_GIT_FILE_STAGED,
    CA_GIT_FILE_STAGED_WITH_CHANGES,
    CA_GIT_FILE_UNTRACKED,
    CA_GIT_FILE_DELETED,
    CA_GIT_FILE_RENAMED,
    CA_GIT_FILE_CONFLICTED,
    CA_GIT_FILE_ADDED
} ca_git_file_status_kind;

typedef enum {
    CA_NOTE_GENERAL,
    CA_NOTE_MEETING,
    CA_NOTE_CONCEPT,
    CA_NOTE_REFERENCE
} ca_note_type;

typedef enum {
    CA_MANAGED_AGENT_QUEUED,
    CA_MANAGED_AGENT_RUNNING,
    CA_MANAGED_AGENT_COMPLETE,
    CA_MANAGED_AGENT_FAILED,
    CA_MANAGED_AGENT_CANCELLED
} ca_managed_agent_state;

typedef struct ca_content_block {
    ca_content_block_type type;
    char *text;
    char *id;
    char *name;
    char *input_json;
    char *tool_use_id;
    char *content;
    int is_error;
} ca_content_block;

typedef struct ca_message {
    char *role;
    ca_content_block *blocks;
    size_t block_count;
} ca_message;

typedef struct ca_tool_call {
    char *call_id;
    char *tool_name;
    char *input_json;
} ca_tool_call;

typedef struct ca_tool_execution {
    ca_tool_execution_state state;
    char *call_id;
    char *tool_name;
    char *input_json;
    char *result;
    char *error;
} ca_tool_execution;

typedef struct ca_agent_state {
    ca_agent_state_type type;
    ca_message *conversation;
    size_t conversation_count;
    ca_content_block *response_content;
    size_t response_content_count;
    char *stop_reason;
    ca_tool_execution *executions;
    size_t execution_count;
    ca_message *pending_tool_results;
    size_t pending_tool_result_count;
    char *error_message;
    unsigned retries;
} ca_agent_state;

typedef struct ca_agent_event {
    ca_agent_event_type type;
    char *text;
    ca_content_block *content;
    size_t content_count;
    char *stop_reason;
    char *call_id;
    char *result;
    char *error;
    int proceed;
    char *warning;
    unsigned retry_limit;
    unsigned retry_delay_ms;
} ca_agent_event;

typedef struct ca_agent_action {
    ca_agent_action_type type;
    ca_message *messages;
    size_t message_count;
    ca_tool_call *tool_calls;
    size_t tool_call_count;
    char **tool_names;
    size_t tool_name_count;
    char *text;
    unsigned delay_ms;
} ca_agent_action;

typedef struct ca_state_machine ca_state_machine;
typedef struct ca_agent ca_agent;
typedef struct ca_agent_manager ca_agent_manager;
typedef struct ca_session ca_session;
typedef struct ca_session_manager ca_session_manager;
typedef struct ca_permission_checker ca_permission_checker;
typedef struct ca_tool_registry ca_tool_registry;

typedef ca_status (*ca_tool_fn)(const char *input_json, char **output, void *userdata);
typedef void (*ca_ui_fn)(const char *text, void *userdata);
typedef ca_status (*ca_storage_save_fn)(const char *key,
                                        const char *content,
                                        char **out_ref,
                                        void *userdata);
/* Storage load callbacks allocate out_content with libcodeagent-compatible allocation; libcodeagent frees it. */
typedef ca_status (*ca_storage_load_fn)(const char *ref,
                                        char **out_content,
                                        void *userdata);
/* Storage list callbacks allocate out_refs like ca_session_manager_list(); callers release with ca_string_list_free(). */
typedef ca_status (*ca_storage_list_fn)(char ***out_refs,
                                        size_t *out_count,
                                        void *userdata);
typedef ca_status (*ca_post_tools_hook_fn)(const ca_message *conversation,
                                           size_t conversation_count,
                                           const char *const *tool_names,
                                           size_t tool_name_count,
                                           char **warning,
                                           int *proceed,
                                           void *userdata);

typedef struct ca_tool_definition {
    char *name;
    char *description;
    char *input_schema_json;
    ca_tool_fn function;
    void *userdata;
} ca_tool_definition;

typedef struct ca_provider_info {
    const char *name;
    const char *description;
    int available;
} ca_provider_info;

typedef struct ca_command_info {
    const char *name;
    const char *description;
} ca_command_info;

typedef struct ca_api_version {
    unsigned major;
    unsigned minor;
    unsigned patch;
    unsigned abi;
} ca_api_version;

typedef struct ca_git_file_status {
    char *path;
    ca_git_file_status_kind status;
} ca_git_file_status;

typedef struct ca_git_status {
    char *branch;
    int detached;
    int has_conflicts;
    ca_git_file_status *files;
    size_t file_count;
} ca_git_status;

typedef struct ca_cost_tracker {
    char *model;
    size_t input_tokens;
    size_t output_tokens;
    size_t messages;
} ca_cost_tracker;

typedef struct ca_note_metadata {
    ca_note_type note_type;
    const char *const *tags;
    size_t tag_count;
    const char *const *related;
    size_t related_count;
} ca_note_metadata;

typedef struct ca_agent_task_snapshot {
    unsigned id;
    char *name;
    char *description;
    ca_managed_agent_state state;
    unsigned progress;
    char *message;
} ca_agent_task_snapshot;

typedef struct ca_diagnostic {
    char *severity;
    char *code;
    char *file;
    unsigned line;
    unsigned column;
    char *message;
    char *raw;
} ca_diagnostic;

typedef struct ca_config {
    char *provider;
    char *model;
    unsigned max_tokens;
    unsigned context_window;
    size_t max_tool_iterations;
    int persistence_enabled;
    char *persistence_path;
    int auto_read;
    char **trusted_paths;
    size_t trusted_path_count;
    int fun_facts;
    unsigned fun_fact_delay;
    char *obsidian_vault_path;
    unsigned max_llm_retries;
    unsigned llm_retry_delay_ms;
    unsigned max_tool_retries;
    unsigned tool_retry_delay_ms;
    unsigned provider_timeout_ms;
    ca_sandbox_mode sandbox_mode;
    int sandbox_network;
    /* Opaque provider option storage; access only with ca_config_*provider_option* APIs. */
    void *provider_options;
} ca_config;

typedef struct ca_agent_callbacks {
    ca_ui_fn display_text;
    ca_ui_fn display_error;
    ca_ui_fn display_warning;
    ca_ui_fn tool_started;
    ca_ui_fn tool_finished;
    ca_post_tools_hook_fn post_tools_hook;
    void *userdata;
} ca_agent_callbacks;

typedef struct ca_storage_adapter {
    ca_storage_save_fn save;
    ca_storage_load_fn load;
    ca_storage_list_fn list;
    void *userdata;
} ca_storage_adapter;

/* Returns a static string for a status code. The caller must not free it. */
const char *ca_status_string(ca_status status);

/* Returns the compile-time API version used by this libcodeagent build. */
ca_api_version ca_version(void);

/* Returns the semantic version string. The caller must not free it. */
const char *ca_version_string(void);

/* Returns the ABI version number used for dynamic linking compatibility checks. */
unsigned ca_abi_version(void);

/* Returns a static string for a state value. The caller must not free it. */
const char *ca_state_name(ca_agent_state_type state);

/* Returns a static string for an action value. The caller must not free it. */
const char *ca_action_name(ca_agent_action_type action);

/* Returns a static string for a sandbox mode value. The caller must not free it. */
const char *ca_sandbox_mode_name(ca_sandbox_mode mode);

/* Duplicates a string with libcodeagent allocation. Free with ca_free(). */
char *ca_strdup(const char *value);

/* Frees memory returned by libcodeagent allocation helpers and APIs. */
void ca_free(void *ptr);

/* Creates an owned text content block. Release nested memory with ca_content_block_free(). */
ca_content_block ca_content_text(const char *text);

/* Creates an owned tool-use content block. Release nested memory with ca_content_block_free(). */
ca_content_block ca_content_tool_use(const char *id, const char *name, const char *input_json);

/* Creates an owned tool-result content block. Release nested memory with ca_content_block_free(). */
ca_content_block ca_content_tool_result(const char *tool_use_id, const char *content, int is_error);

/* Releases all memory owned by a content block and resets it to zero. */
void ca_content_block_free(ca_content_block *block);

/* Deep-copies a content block into dst. Release dst with ca_content_block_free(). */
ca_status ca_content_block_clone(const ca_content_block *src, ca_content_block *dst);

/* Creates a user message with one text block. Release with ca_message_free(). */
ca_message ca_message_user(const char *text);

/* Creates an assistant message by deep-copying blocks. Release with ca_message_free(). */
ca_message ca_message_assistant(ca_content_block *blocks, size_t block_count);

/* Creates a user message containing one tool-result block. Release with ca_message_free(). */
ca_message ca_message_tool_result(const char *tool_use_id, const char *content, int is_error);

/* Releases all memory owned by a message and resets it to zero. */
void ca_message_free(ca_message *message);

/* Deep-copies a message into dst. Release dst with ca_message_free(). */
ca_status ca_message_clone(const ca_message *src, ca_message *dst);

/* Allocates a new state machine. Release with ca_state_machine_free(). */
ca_state_machine *ca_state_machine_new(void);

/* Releases a state machine allocated by ca_state_machine_new(). */
void ca_state_machine_free(ca_state_machine *machine);

/* Returns a borrowed state pointer valid until the machine is mutated or freed. */
const ca_agent_state *ca_state_machine_state(const ca_state_machine *machine);

/* Handles one event and writes the next action to action. Release action with ca_agent_action_free(). */
ca_status ca_state_machine_handle(ca_state_machine *machine,
                                  const ca_agent_event *event,
                                  ca_agent_action *action);

/* Releases all memory owned by an action and resets it to zero. */
void ca_agent_action_free(ca_agent_action *action);

/* Initializes config with defaults. Release owned fields with ca_config_free(). */
void ca_config_init_defaults(ca_config *config);

/* Releases all memory owned by config and resets it to zero. */
void ca_config_free(ca_config *config);

/* Deep-copies src into dst. Release dst with ca_config_free(). */
ca_status ca_config_clone(const ca_config *src, ca_config *dst);

/* Sets the selected provider name in config. The value is copied. */
ca_status ca_config_set_provider(ca_config *config, const char *provider_name);

/* Sets the default model in config. The value is copied. */
ca_status ca_config_set_model(ca_config *config, const char *model);

/* Sets the session persistence path in config. The value is copied. */
ca_status ca_config_set_persistence_path(ca_config *config, const char *path);

/* Sets the Obsidian vault path in config. The value is copied. */
ca_status ca_config_set_obsidian_vault_path(ca_config *config, const char *path);

/* Sets the sandbox mode used by built-in tools and child process execution. */
ca_status ca_config_set_sandbox_mode(ca_config *config, ca_sandbox_mode mode);

/* Parses disabled, read_only/read-only, workspace_write/workspace-write, or full_access/full-access. */
ca_status ca_sandbox_mode_parse(const char *text, ca_sandbox_mode *mode);

/* Sets a provider-specific option by provider name and key. The value is copied. */
ca_status ca_config_set_provider_option(ca_config *config,
                                        const char *provider,
                                        const char *key,
                                        const char *value);

/* Returns a borrowed provider option value, or NULL if unset. Do not free it. */
const char *ca_config_provider_option(const ca_config *config,
                                      const char *provider,
                                      const char *key);

/* Adds a trusted path prefix to config. The value is copied. */
ca_status ca_config_add_trusted_path(ca_config *config, const char *path);

/* Checks whether a path is allowed, denied, or requires a prompt. */
ca_permission_result ca_permission_check_path(const ca_config *config,
                                              const char *path,
                                              int write_access);

/* Allocates a permission checker from config trusted paths and auto_read. Release with ca_permission_checker_free(). */
ca_permission_checker *ca_permission_checker_new(const ca_config *config);

/* Releases a permission checker. */
void ca_permission_checker_free(ca_permission_checker *checker);

/* Checks a path using trusted paths and the session decision cache. */
ca_permission_result ca_permission_checker_check(ca_permission_checker *checker,
                                                 const char *path,
                                                 int write_access);

/* Records a prompt response for path. ALLOW_ALWAYS also adds a trusted path. */
ca_status ca_permission_checker_respond(ca_permission_checker *checker,
                                        ca_config *config,
                                        const char *path,
                                        int write_access,
                                        ca_permission_response response);

/* Parses a prompt answer such as y, n, a, always, never, or q. */
ca_status ca_permission_response_parse(const char *text, ca_permission_response *response);

/* Executes the built-in read_file tool. output is allocated and must be freed with ca_free(). */
ca_status ca_tool_read_file(const char *input_json, char **output, void *userdata);

/* Executes the built-in write_file tool. output is allocated and must be freed with ca_free().
   Pass a ca_config* as userdata to enforce trusted-path write permissions. */
ca_status ca_tool_write_file(const char *input_json, char **output, void *userdata);

/* Executes the built-in edit_file tool. output is allocated and must be freed with ca_free().
   Pass a ca_config* as userdata to enforce trusted-path write permissions. */
ca_status ca_tool_edit_file(const char *input_json, char **output, void *userdata);

/* Executes the built-in list_files tool. output is allocated and must be freed with ca_free(). */
ca_status ca_tool_list_files(const char *input_json, char **output, void *userdata);

/* Executes the built-in bash tool. output is allocated and must be freed with ca_free(). */
ca_status ca_tool_bash(const char *input_json, char **output, void *userdata);

/* Executes the built-in code_search tool. output is allocated and must be freed with ca_free(). */
ca_status ca_tool_code_search(const char *input_json, char **output, void *userdata);

/* Categorizes a tool error string. The returned enum does not own memory. */
ca_tool_error_category ca_tool_categorize_error(const char *error);

/* Allocates the built-in tool table with provider-ready JSON schemas. Release with ca_tool_definitions_free(). */
ca_status ca_builtin_tools(ca_tool_definition **tools, size_t *tool_count);

/* Releases a tool table allocated by ca_builtin_tools() or cloned by libcodeagent. */
void ca_tool_definitions_free(ca_tool_definition *tools, size_t tool_count);

/* Allocates an empty mutable tool registry. Release with ca_tool_registry_free(). */
ca_tool_registry *ca_tool_registry_new(void);

/* Releases a tool registry and all cloned tool definitions it owns. */
void ca_tool_registry_free(ca_tool_registry *registry);

/* Adds all built-in tools to registry. Existing tools with the same name are replaced. */
ca_status ca_tool_registry_add_builtin(ca_tool_registry *registry);

/* Registers or replaces one external tool definition. Strings are copied; function/userdata are borrowed. */
ca_status ca_tool_registry_register(ca_tool_registry *registry,
                                    const char *name,
                                    const char *description,
                                    const char *input_schema_json,
                                    ca_tool_fn function,
                                    void *userdata);

/* Returns the number of tools currently registered. */
size_t ca_tool_registry_count(const ca_tool_registry *registry);

/* Allocates a cloned tool table from registry. Release with ca_tool_definitions_free(). */
ca_status ca_tool_registry_definitions(const ca_tool_registry *registry,
                                       ca_tool_definition **tools,
                                       size_t *tool_count);

/* Executes a named tool from registry. output is allocated and must be freed with ca_free(). */
ca_status ca_tool_registry_execute(ca_tool_registry *registry,
                                   const char *name,
                                   const char *input_json,
                                   char **output);

/* Executes a named tool from a caller-supplied table. output is allocated and must be freed with ca_free(). */
ca_status ca_tool_execute(ca_tool_definition *tools,
                          size_t tool_count,
                          const char *name,
                          const char *input_json,
                          char **output);

/* Returns the number of available built-in and registered providers. */
size_t ca_provider_count(void);

/* Writes borrowed provider metadata for index into info. Strings must not be freed. */
ca_status ca_provider_info_at(size_t index, ca_provider_info *info);

/* Returns the number of known slash commands. */
size_t ca_command_count(void);

/* Writes borrowed command metadata for index into info. Strings must not be freed. */
ca_status ca_command_info_at(size_t index, ca_command_info *info);

/* Finds a slash command by name. Returned strings in info are borrowed. */
ca_status ca_command_find(const char *name, ca_command_info *info);

/* Estimates tokens with the current simple 4-character heuristic. */
size_t ca_estimate_tokens_text(const char *text);

/* Estimates tokens across messages with the current simple heuristic. */
size_t ca_estimate_tokens_messages(const ca_message *messages, size_t message_count);

/* Estimates USD cost for known model families. Writes the result to cost_usd. */
ca_status ca_estimate_cost_usd(const char *model,
                               size_t input_tokens,
                               size_t output_tokens,
                               double *cost_usd);

/* Initializes a cost tracker for model. Release with ca_cost_tracker_free(). */
void ca_cost_tracker_init(ca_cost_tracker *tracker, const char *model);

/* Releases memory owned by tracker and resets it to zero. */
void ca_cost_tracker_free(ca_cost_tracker *tracker);

/* Adds one usage record to tracker. */
ca_status ca_cost_tracker_add(ca_cost_tracker *tracker, size_t input_tokens, size_t output_tokens);

/* Writes the estimated total cost to cost_usd. */
ca_status ca_cost_tracker_total_usd(const ca_cost_tracker *tracker, double *cost_usd);

/* Allocates a human-readable cost breakdown. Free with ca_free(). */
ca_status ca_cost_tracker_render(const ca_cost_tracker *tracker, char **out);

/* Returns a static git status indicator. The caller must not free it. */
const char *ca_git_file_status_indicator(ca_git_file_status_kind status);

/* Returns a static git status description. The caller must not free it. */
const char *ca_git_file_status_description(ca_git_file_status_kind status);

/* Loads git status for repository_path or the current directory when NULL. Release with ca_git_status_free(). */
ca_status ca_git_status_load(const char *repository_path, ca_git_status *status);

/* Releases memory owned by status and resets it to zero. */
void ca_git_status_free(ca_git_status *status);

/* Allocates a formatted git status summary. Free with ca_free(). */
ca_status ca_git_status_render(const ca_git_status *status, char **out);

/* Allocates a simple logical grouping summary for changed files. Free with ca_free(). */
ca_status ca_git_group_summary(const ca_git_status *status, char **out);

/* Allocates git diff output for repository_path or current directory when NULL. Free with ca_free(). */
ca_status ca_git_diff(const char *repository_path, int staged, char **out);

/* Allocates a purpose-focused commit message from a status. Free with ca_free(). */
ca_status ca_git_generate_commit_message(const ca_git_status *status, char **out);

/* Creates a git commit using either message or a generated message. Writes the final message to out_message when non-NULL. */
ca_status ca_git_commit_all(const char *repository_path, const char *message, char **out_message);

/* Runs a safe git undo operation: hard=0 reverts the last commit keeping changes, hard=1 discards it. */
ca_status ca_git_undo_last_commit(const char *repository_path, int hard);

/* Runs formatter/test landing commands and writes a summary. Free with ca_free(). */
ca_status ca_git_land_summary(const char *repository_path, char **out);

/* Creates or updates an Obsidian-style markdown note. out_path is allocated when non-NULL; free with ca_free(). */
ca_status ca_obsidian_write_note(const char *vault_path,
                                 const char *title,
                                 const char *body,
                                 const ca_note_metadata *metadata,
                                 char **out_path);

/* Allocates an in-memory manager for foreground/background agent progress. */
ca_agent_manager *ca_agent_manager_new(void);

/* Releases a manager allocated by ca_agent_manager_new(). */
void ca_agent_manager_free(ca_agent_manager *manager);

/* Adds an agent task and writes its id. name/description are copied. */
ca_status ca_agent_manager_spawn(ca_agent_manager *manager,
                                 const char *name,
                                 const char *description,
                                 unsigned *out_id);

/* Updates state/progress/message for an agent task. message is copied when non-NULL. */
ca_status ca_agent_manager_update(ca_agent_manager *manager,
                                  unsigned id,
                                  ca_managed_agent_state state,
                                  unsigned progress,
                                  const char *message);

/* Marks an agent task cancelled. */
ca_status ca_agent_manager_cancel(ca_agent_manager *manager, unsigned id);

/* Allocates snapshots for all known agent tasks. Release with ca_agent_task_snapshots_free(). */
ca_status ca_agent_manager_list(const ca_agent_manager *manager,
                                ca_agent_task_snapshot **out_tasks,
                                size_t *out_count);

/* Releases snapshots allocated by ca_agent_manager_list(). */
void ca_agent_task_snapshots_free(ca_agent_task_snapshot *tasks, size_t count);

/* Allocates a human-readable status report for active agents. Free with ca_free(). */
ca_status ca_agent_manager_render(const ca_agent_manager *manager, char **out);

/* Parses the first compiler-style diagnostic in output. Release with ca_diagnostic_free(). */
ca_status ca_diagnostic_parse(const char *output, ca_diagnostic *diagnostic);

/* Releases memory owned by a diagnostic and resets it to zero. */
void ca_diagnostic_free(ca_diagnostic *diagnostic);

/* Returns non-zero when a diagnostic/error is within the safe auto-fix boundary. */
int ca_auto_fix_should_apply(const char *error_text);

/* Allocates a conservative fix suggestion for output. Free with ca_free(). */
ca_status ca_fix_agent_suggest(const char *compiler_output, char **out_suggestion);

/* Writes a regression-test skeleton for compiler_output under directory. out_path is allocated when non-NULL. */
ca_status ca_regression_test_generate(const char *directory,
                                      const char *compiler_output,
                                      char **out_path);

/* Applies safe auto-fixes for narrow dependency/import cases; writes a summary. Free out_summary with ca_free(). */
ca_status ca_auto_fix_apply_safe(const char *project_dir,
                                 const char *compiler_output,
                                 char **out_summary);

/* Allocates a new in-memory session. Release with ca_session_free(). */
ca_session *ca_session_new(const char *id);

/* Releases a session allocated by ca_session_new() or ca_session_load_markdown(). */
void ca_session_free(ca_session *session);

/* Returns a borrowed session id valid until the session is freed. */
const char *ca_session_id(const ca_session *session);

/* Appends a deep copy of message to session. */
ca_status ca_session_append(ca_session *session, const ca_message *message);

/* Returns borrowed session messages valid until session mutation or free. */
const ca_message *ca_session_messages(const ca_session *session, size_t *message_count);

/* Saves a session as markdown at path. The file is overwritten. */
ca_status ca_session_save_markdown(const ca_session *session, const char *path);

/* Loads a markdown file into a session object. Release *session with ca_session_free(). */
ca_status ca_session_load_markdown(ca_session **session, const char *path);

/* Allocates markdown text for a session. Free with ca_free(). */
ca_status ca_session_render_markdown(const ca_session *session, char **out_markdown);

/* Parses markdown text into a session object. Release *session with ca_session_free(). */
ca_status ca_session_load_markdown_text(ca_session **session,
                                        const char *session_id,
                                        const char *markdown);

/* Saves a session through a caller-supplied storage adapter. out_ref is allocated when non-NULL. */
ca_status ca_storage_save_session(const ca_storage_adapter *adapter,
                                  const char *key,
                                  const ca_session *session,
                                  char **out_ref);

/* Loads a session through a caller-supplied storage adapter. Release *session with ca_session_free(). */
ca_status ca_storage_load_session(const ca_storage_adapter *adapter,
                                  const char *ref,
                                  ca_session **session);

/* Lists session references through a caller-supplied storage adapter. Release with ca_string_list_free(). */
ca_status ca_storage_list_sessions(const ca_storage_adapter *adapter,
                                   char ***out_refs,
                                   size_t *out_count);

/* Allocates a session manager rooted at directory. Release with ca_session_manager_free(). */
ca_session_manager *ca_session_manager_new(const char *directory);

/* Releases a session manager. */
void ca_session_manager_free(ca_session_manager *manager);

/* Returns a borrowed session manager directory path. */
const char *ca_session_manager_directory(const ca_session_manager *manager);

/* Saves session under manager directory and writes the allocated path to out_path when non-NULL. Free with ca_free(). */
ca_status ca_session_manager_save(ca_session_manager *manager,
                                  const ca_session *session,
                                  char **out_path);

/* Loads the newest markdown session under manager directory. Release *session with ca_session_free(). */
ca_status ca_session_manager_load_latest(ca_session_manager *manager, ca_session **session);

/* Lists markdown session paths. out_paths and each string are allocated; release with ca_string_list_free(). */
ca_status ca_session_manager_list(ca_session_manager *manager,
                                  char ***out_paths,
                                  size_t *out_count);

/* Releases a string list returned by libcodeagent. */
void ca_string_list_free(char **items, size_t count);

/* Allocates a new agent. The config and tools are copied; callbacks are borrowed by value. */
ca_agent *ca_agent_new(const ca_config *config,
                       const ca_agent_callbacks *callbacks,
                       ca_tool_definition *tools,
                       size_t tool_count);

/* Releases an agent allocated by ca_agent_new(). */
void ca_agent_free(ca_agent *agent);

/* Submits one user message and runs the library-owned conversation loop. */
ca_status ca_agent_submit(ca_agent *agent, const char *user_text);

/* Clears an agent conversation and token estimate. */
ca_status ca_agent_reset(ca_agent *agent);

/* Sets the selected provider on an agent. The value is copied. */
ca_status ca_agent_set_provider(ca_agent *agent, const char *provider_name);

/* Returns the borrowed selected provider name, or the default provider. */
const char *ca_agent_provider(const ca_agent *agent);

/* Sets the selected model on an agent. The value is copied. */
ca_status ca_agent_set_model(ca_agent *agent, const char *model);

/* Returns the borrowed selected model name, or the default model. */
const char *ca_agent_model(const ca_agent *agent);

/* Returns borrowed conversation messages valid until agent mutation or free. */
const ca_message *ca_agent_conversation(const ca_agent *agent, size_t *message_count);

/* Returns the current heuristic context token estimate. */
size_t ca_agent_context_tokens(const ca_agent *agent);

#ifdef __cplusplus
}
#endif

#endif
