/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "codeagent/codeagent.h"
#include "config.h"
#include "ui.h"

#include <dirent.h>
#include <errno.h>
#include <stdint.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

typedef struct cli_state {
    char **tool_results;
    size_t tool_result_count;
    char session_path[PATH_MAX];
    ca_cost_tracker cost_tracker;
    int spec_mode;
} cli_state;

static volatile sig_atomic_t interrupted;

static void handle_sigint(int signo) {
    (void)signo;
    interrupted = 1;
}

static const char *skip_spaces(const char *text) {
    while (text != NULL && (*text == ' ' || *text == '\t')) {
        text++;
    }
    return text == NULL ? "" : text;
}

static int ensure_dir(const char *path) {
    char tmp[PATH_MAX];
    char *p;
    size_t len;
    if (path == NULL || path[0] == '\0') {
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
    return mkdir(tmp, 0777) == 0 || errno == EEXIST ? 0 : -1;
}

static const char *history_directory(const ca_config *config) {
    if (config == NULL || config->persistence_path == NULL || config->persistence_path[0] == '\0') {
        return ".specstory/history/";
    }
    return config->persistence_path;
}

static void default_session_path(const char *directory, char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm value;
    struct tm *ptr = localtime(&now);
    const char *root = directory == NULL || directory[0] == '\0' ? ".specstory/history/" : directory;
    size_t len = strlen(root);
    ensure_dir(root);
    if (ptr != NULL) {
        value = *ptr;
    } else {
        memset(&value, 0, sizeof(value));
    }
    snprintf(buffer,
             size,
             "%s%scodeagentctl-%04d%02d%02d-%02d%02d%02d-%ld.md",
             root,
             len > 0 && root[len - 1] == '/' ? "" : "/",
             value.tm_year + 1900,
             value.tm_mon + 1,
             value.tm_mday,
             value.tm_hour,
             value.tm_min,
             value.tm_sec,
             (long)getpid());
}

static int latest_session_path(const char *directory, char *buffer, size_t size) {
    const char *root = directory == NULL || directory[0] == '\0' ? ".specstory/history/" : directory;
    size_t root_len = strlen(root);
    DIR *dir = opendir(root);
    struct dirent *entry;
    time_t best = 0;
    int found = 0;
    if (dir == NULL) {
        return 0;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        struct stat st;
        size_t name_len = strlen(entry->d_name);
        if (name_len < 4 || strcmp(entry->d_name + name_len - 3, ".md") != 0) {
            continue;
        }
        snprintf(path, sizeof(path), "%s%s%s", root, root_len > 0 && root[root_len - 1] == '/' ? "" : "/", entry->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        if (!found || st.st_mtime > best) {
            snprintf(buffer, size, "%s", path);
            best = st.st_mtime;
            found = 1;
        }
    }
    closedir(dir);
    return found;
}

static void cli_state_free(cli_state *state) {
    size_t i;
    if (state == NULL) {
        return;
    }
    for (i = 0; i < state->tool_result_count; i++) {
        free(state->tool_results[i]);
    }
    free(state->tool_results);
    ca_cost_tracker_free(&state->cost_tracker);
}

static void remember_tool_result(cli_state *state, const char *summary) {
    char **next;
    if (state == NULL || summary == NULL) {
        return;
    }
    if (state->tool_result_count > SIZE_MAX / sizeof(char *) - 1) {
        return;
    }
    next = (char **)realloc(state->tool_results, sizeof(char *) * (state->tool_result_count + 1));
    if (next == NULL) {
        return;
    }
    state->tool_results = next;
    state->tool_results[state->tool_result_count] = ca_strdup(summary);
    if (state->tool_results[state->tool_result_count] != NULL) {
        state->tool_result_count++;
    }
}

static void save_agent_session(ca_agent *agent, const char *path) {
    ca_session *session;
    const ca_message *messages;
    size_t count = 0;
    size_t i;
    if (agent == NULL || path == NULL || path[0] == '\0') {
        return;
    }
    session = ca_session_new("codeagentctl");
    if (session == NULL) {
        return;
    }
    messages = ca_agent_conversation(agent, &count);
    for (i = 0; i < count; i++) {
        ca_session_append(session, &messages[i]);
    }
    ca_session_save_markdown(session, path);
    ca_session_free(session);
}

static void on_text(const char *text, void *userdata) {
    (void)userdata;
    codeagentctl_print_text(text);
}

static void on_error(const char *text, void *userdata) {
    (void)userdata;
    codeagentctl_print_error(text);
}

static void on_warning(const char *text, void *userdata) {
    (void)userdata;
    codeagentctl_print_warning(text);
}

static void on_tool_started(const char *text, void *userdata) {
    (void)userdata;
    codeagentctl_print_tool_started(text);
}

static void on_tool_finished(const char *text, void *userdata) {
    remember_tool_result((cli_state *)userdata, text);
    codeagentctl_print_tool_finished(text);
}

static void print_help(void) {
    size_t i;
    puts("\nCommands:");
    for (i = 0; i < ca_command_count(); i++) {
        ca_command_info info;
        if (ca_command_info_at(i, &info) == CA_OK) {
            printf("  /%-10s %s\n", info.name, info.description == NULL ? "" : info.description);
        }
    }
    puts("  /commit --pick  Show changed files before committing");
    puts("  /quit, /q       Exit aliases");
}

static void print_providers(void) {
    size_t i;
    size_t count = ca_provider_count();
    for (i = 0; i < count; i++) {
        ca_provider_info info;
        if (ca_provider_info_at(i, &info) == CA_OK) {
            printf("%s\t%s\n", info.name, info.description == NULL ? "" : info.description);
        }
    }
}

static void print_history(const char *directory) {
    const char *root = directory == NULL || directory[0] == '\0' ? ".specstory/history/" : directory;
    size_t root_len = strlen(root);
    DIR *dir = opendir(root);
    struct dirent *entry;
    int found = 0;
    if (dir == NULL) {
        puts("No saved sessions.");
        return;
    }
    while ((entry = readdir(dir)) != NULL) {
        size_t len = strlen(entry->d_name);
        if (len >= 4 && strcmp(entry->d_name + len - 3, ".md") == 0) {
            printf("%s%s%s\n", root, root_len > 0 && root[root_len - 1] == '/' ? "" : "/", entry->d_name);
            found = 1;
        }
    }
    closedir(dir);
    if (!found) {
        puts("No saved sessions.");
    }
}

static void print_config(const ca_config *config, const ca_agent *agent) {
    printf("provider: %s\n", ca_agent_provider(agent));
    printf("model: %s\n", ca_agent_model(agent));
    printf("max_tokens: %u\n", config->max_tokens);
    printf("context_window: %u\n", config->context_window);
    printf("max_tool_iterations: %lu\n", (unsigned long)config->max_tool_iterations);
    printf("persistence: %s\n", config->persistence_enabled ? "enabled" : "disabled");
    printf("persistence_path: %s\n", config->persistence_path == NULL ? ".specstory/history/" : config->persistence_path);
    printf("sandbox: %s\n", ca_sandbox_mode_name(config->sandbox_mode));
    printf("sandbox_network: %s\n", config->sandbox_network ? "enabled" : "disabled");
    printf("obsidian_vault_path: %s\n", config->obsidian_vault_path == NULL ? "" : config->obsidian_vault_path);
}

static void print_results(const cli_state *state, int expanded) {
    size_t i;
    if (state == NULL || state->tool_result_count == 0) {
        puts("No tool results yet.");
        return;
    }
    for (i = 0; i < state->tool_result_count; i++) {
        printf("[%lu] ", (unsigned long)(i + 1));
        codeagentctl_print_tool_result(state->tool_results[i], expanded);
    }
}

static int run_slash_command(char *input, ca_agent *agent, ca_config *config, cli_state *state) {
    char *space = strchr(input, ' ');
    const char *args = "";
    if (space != NULL) {
        *space = '\0';
        args = skip_spaces(space + 1);
    }
    if (strcmp(input, "/exit") == 0 || strcmp(input, "/quit") == 0 || strcmp(input, "/q") == 0) {
        return 1;
    }
    if (strcmp(input, "/help") == 0) {
        print_help();
    } else if (strcmp(input, "/cancel") == 0) {
        puts("No active operation to cancel.");
    } else if (strcmp(input, "/clear") == 0) {
        ca_agent_reset(agent);
        ca_cost_tracker_free(&state->cost_tracker);
        ca_cost_tracker_init(&state->cost_tracker, ca_agent_model(agent));
        puts("Context cleared.");
    } else if (strcmp(input, "/config") == 0) {
        print_config(config, agent);
    } else if (strcmp(input, "/context") == 0) {
        codeagentctl_print_context_bar(ca_agent_context_tokens(agent), config->context_window);
    } else if (strcmp(input, "/cost") == 0) {
        char *out = NULL;
        if (ca_cost_tracker_render(&state->cost_tracker, &out) == CA_OK) {
            codeagentctl_print_text(out);
        }
        free(out);
    } else if (strcmp(input, "/diff") == 0) {
        char *out = NULL;
        if (ca_git_diff(NULL, strcmp(args, "--staged") == 0 || strcmp(args, "--cached") == 0, &out) == CA_OK) {
            codeagentctl_print_text(out[0] == '\0' ? "No diff." : out);
        } else {
            codeagentctl_print_error("failed to read git diff");
        }
        free(out);
    } else if (strcmp(input, "/history") == 0) {
        print_history(history_directory(config));
    } else if (strcmp(input, "/model") == 0) {
        if (args[0] == '\0') {
            printf("Current model: %s\n", ca_agent_model(agent));
        } else if (ca_agent_set_model(agent, args) == CA_OK && ca_config_set_model(config, args) == CA_OK) {
            ca_cost_tracker_free(&state->cost_tracker);
            ca_cost_tracker_init(&state->cost_tracker, ca_agent_model(agent));
            printf("Model set to %s\n", ca_agent_model(agent));
        } else {
            codeagentctl_print_error("failed to set model");
        }
    } else if (strcmp(input, "/results") == 0) {
        print_results(state, strcmp(args, "--all") == 0 || strcmp(args, "--expanded") == 0);
    } else if (strcmp(input, "/status") == 0) {
        printf("Provider: %s\nModel: %s\n", ca_agent_provider(agent), ca_agent_model(agent));
        codeagentctl_print_context_bar(ca_agent_context_tokens(agent), config->context_window);
        codeagentctl_print_progress("agents", 100);
        puts("No background agents active.");
    } else if (strcmp(input, "/document") == 0) {
        char *path = NULL;
        const char *title = args[0] == '\0' ? "Coding Agent Session" : args;
        if (config->obsidian_vault_path != NULL && config->obsidian_vault_path[0] != '\0') {
            ca_note_metadata metadata;
            const char *tags[] = { "coding-agent" };
            memset(&metadata, 0, sizeof(metadata));
            metadata.note_type = CA_NOTE_REFERENCE;
            metadata.tags = tags;
            metadata.tag_count = 1;
            if (ca_obsidian_write_note(config->obsidian_vault_path,
                                       title,
                                       "Session notes captured from codeagentctl.",
                                       &metadata,
                                       &path) == CA_OK) {
                printf("Saved Obsidian note to %s\n", path);
            } else {
                codeagentctl_print_error("failed to write Obsidian note");
            }
            free(path);
        } else {
            const char *session_path = args[0] == '\0' ? state->session_path : args;
            save_agent_session(agent, session_path);
            printf("Saved conversation to %s\n", session_path);
        }
    } else if (strcmp(input, "/spec") == 0) {
        state->spec_mode = !state->spec_mode;
        printf("Spec mode %s.\n", state->spec_mode ? "enabled" : "disabled");
    } else if (strcmp(input, "/commit") == 0) {
        if (strcmp(args, "--pick") == 0) {
            ca_git_status status;
            char *out = NULL;
            char *groups = NULL;
            if (ca_git_status_load(NULL, &status) == CA_OK) {
                ca_git_status_render(&status, &out);
                codeagentctl_print_text(status.file_count == 0 ? "No changed files to pick." : out);
                if (status.file_count > 0 && ca_git_group_summary(&status, &groups) == CA_OK) {
                    codeagentctl_print_text(groups);
                }
                ca_git_status_free(&status);
            } else {
                codeagentctl_print_error("not a git repository");
            }
            free(groups);
            free(out);
        } else if (args[0] == '\0') {
            char *message = NULL;
            ca_status status = ca_git_commit_all(NULL, NULL, &message);
            if (status == CA_OK) {
                printf("Committed: %s\n", message);
            } else if (status == CA_NOT_FOUND) {
                puts("No changes to commit.");
            } else {
                codeagentctl_print_error("git commit failed");
            }
            free(message);
        } else {
            char *message = NULL;
            if (ca_git_commit_all(NULL, args, &message) == CA_OK) {
                printf("Committed: %s\n", message);
            } else {
                codeagentctl_print_error("git commit failed");
            }
            free(message);
        }
    } else if (strcmp(input, "/land") == 0) {
        char *out = NULL;
        if (ca_git_land_summary(NULL, &out) == CA_OK) {
            codeagentctl_print_text(out);
        }
        free(out);
    } else if (strcmp(input, "/undo") == 0) {
        int hard = strcmp(args, "--hard") == 0;
        if (args[0] == '\0' || strcmp(args, "--soft") == 0 || hard) {
            if (ca_git_undo_last_commit(NULL, hard) == CA_OK) {
                printf("Undid last commit with %s reset.\n", hard ? "hard" : "soft");
            } else {
                codeagentctl_print_error("git undo failed");
            }
        } else {
            puts("Usage: /undo [--soft|--hard]");
        }
    } else {
        printf("Unknown command: %s\n", input);
        print_help();
    }
    return 0;
}

int main(int argc, char **argv) {
    ca_config config;
    ca_agent_callbacks callbacks;
    ca_agent *agent;
    cli_state state;
    const char *provider_name = NULL;
    int verbose = 0;
    int i;

    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--verbose") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--provider") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "Error: --provider requires a provider name\n");
                return 2;
            }
            provider_name = argv[++i];
        } else if (strcmp(argv[i], "--providers") == 0) {
            print_providers();
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: codeagentctl [OPTIONS]\n\n");
            printf("Options:\n");
            printf("  --provider NAME  Select provider backend (default: %s)\n", CODEAGENT_DEFAULT_PROVIDER);
            printf("  --providers      List provider backends\n");
            printf("  -v, --verbose    Enable verbose output\n");
            printf("  -h, --help       Show this help message\n");
            return 0;
        }
    }

    if (codeagentctl_config_load(&config, NULL) != CA_OK) {
        ca_config_init_defaults(&config);
        fprintf(stderr, "Warning: could not load config.ucl; using built-in defaults\n");
    }
    memset(&state, 0, sizeof(state));
    if (provider_name != NULL) {
        ca_config_set_provider(&config, provider_name);
    }
    ca_cost_tracker_init(&state.cost_tracker, config.model);
    default_session_path(history_directory(&config), state.session_path, sizeof(state.session_path));
    memset(&callbacks, 0, sizeof(callbacks));
    callbacks.display_text = on_text;
    callbacks.display_error = on_error;
    callbacks.display_warning = on_warning;
    callbacks.tool_started = on_tool_started;
    callbacks.tool_finished = on_tool_finished;
    callbacks.userdata = &state;

    agent = ca_agent_new(&config, &callbacks, NULL, 0);
    if (agent == NULL) {
        fprintf(stderr, "Error: failed to initialize agent\n");
        ca_config_free(&config);
        return 1;
    }

    signal(SIGINT, handle_sigint);
    codeagentctl_ui_init();
    codeagentctl_print_banner();
    {
        char latest[PATH_MAX];
        int has_history = latest_session_path(history_directory(&config), latest, sizeof(latest));
        codeagentctl_print_startup_menu(has_history);
    }
    {
        int choice = codeagentctl_startup_choice();
        if (choice == 'q' || choice == 'Q') {
            codeagentctl_ui_shutdown();
            ca_agent_free(agent);
            ca_config_free(&config);
            cli_state_free(&state);
            return 0;
        }
        if (choice == 'h' || choice == 'H') {
            print_help();
        } else if (choice == 'c' || choice == 'C') {
            print_config(&config, agent);
        } else if (choice == 'r' || choice == 'R') {
            char latest[PATH_MAX];
            if (latest_session_path(history_directory(&config), latest, sizeof(latest))) {
                ca_session *session = NULL;
                snprintf(state.session_path, sizeof(state.session_path), "%s", latest);
                if (ca_session_load_markdown(&session, latest) == CA_OK) {
                    printf("Resumed session display from %s\n", latest);
                    ca_session_free(session);
                } else {
                    codeagentctl_print_warning("could not load latest session");
                }
            } else {
                codeagentctl_print_warning("no saved session to resume");
            }
        }
    }
    if (verbose) {
        fprintf(stderr,
                "[verbose] initialized libcodeagent %s with provider %s\n",
                CODEAGENT_VERSION,
                ca_agent_provider(agent));
    }

    for (;;) {
        char *input = codeagentctl_read_input();
        if (interrupted) {
            interrupted = 0;
            codeagentctl_print_warning("operation cancelled");
            free(input);
            continue;
        }
        if (input == NULL) {
            break;
        }
        if (input[0] == '\0') {
            free(input);
            continue;
        }
        if (input[0] == '/') {
            int should_exit = run_slash_command(input, agent, &config, &state);
            free(input);
            if (should_exit) {
                break;
            }
            continue;
        }
        {
            size_t before_tokens = ca_agent_context_tokens(agent);
            size_t input_tokens = ca_estimate_tokens_text(input);
            if (ca_agent_submit(agent, input) != CA_OK) {
                codeagentctl_print_warning("conversation ended with an error");
            }
            {
                size_t after_tokens = ca_agent_context_tokens(agent);
                size_t output_tokens = after_tokens > before_tokens + input_tokens ?
                    after_tokens - before_tokens - input_tokens : 0;
                ca_cost_tracker_add(&state.cost_tracker, input_tokens, output_tokens);
            }
        }
        codeagentctl_print_context_bar(ca_agent_context_tokens(agent), config.context_window);
        if (config.persistence_enabled) {
            save_agent_session(agent, state.session_path);
        }
        free(input);
    }

    codeagentctl_ui_shutdown();
    ca_agent_free(agent);
    ca_config_free(&config);
    cli_state_free(&state);
    return 0;
}
