/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "codeagent/codeagent.h"

#include <string.h>

static const ca_command_info commands[] = {
    { "help", "Show available commands" },
    { "cancel", "Cancel the current operation" },
    { "clear", "Reset conversation context" },
    { "commit", "Create a purpose-focused git commit" },
    { "config", "Show or edit configuration" },
    { "context", "Show context usage" },
    { "cost", "Show token and cost totals" },
    { "diff", "Show git diff" },
    { "document", "Write project notes" },
    { "exit", "Exit the session" },
    { "quit", "Exit the session" },
    { "q", "Exit the session" },
    { "history", "List saved sessions" },
    { "land", "Run checks and prepare a summary" },
    { "model", "Switch model" },
    { "results", "Show stored tool results" },
    { "spec", "Enter planning/spec mode" },
    { "status", "Show active agent status" },
    { "undo", "Undo a git operation" }
};

size_t ca_command_count(void) {
    return sizeof(commands) / sizeof(commands[0]);
}

ca_status ca_command_info_at(size_t index, ca_command_info *info) {
    if (info == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    if (index >= ca_command_count()) {
        return CA_NOT_FOUND;
    }
    *info = commands[index];
    return CA_OK;
}

ca_status ca_command_find(const char *name, ca_command_info *info) {
    size_t i;
    if (name == NULL || info == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    for (i = 0; i < ca_command_count(); i++) {
        if (strcmp(commands[i].name, name) == 0) {
            *info = commands[i];
            return CA_OK;
        }
    }
    return CA_NOT_FOUND;
}
