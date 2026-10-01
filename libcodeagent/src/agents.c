/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

typedef struct ca_agent_task {
    unsigned id;
    char *name;
    char *description;
    ca_managed_agent_state state;
    unsigned progress;
    char *message;
    struct ca_agent_task *next;
} ca_agent_task;

struct ca_agent_manager {
    unsigned next_id;
    ca_agent_task *tasks;
};

static const char *managed_state_name(ca_managed_agent_state state) {
    switch (state) {
        case CA_MANAGED_AGENT_QUEUED: return "queued";
        case CA_MANAGED_AGENT_RUNNING: return "running";
        case CA_MANAGED_AGENT_COMPLETE: return "complete";
        case CA_MANAGED_AGENT_FAILED: return "failed";
        case CA_MANAGED_AGENT_CANCELLED: return "cancelled";
    }
    return "unknown";
}

static ca_agent_task *find_task(ca_agent_manager *manager, unsigned id) {
    ca_agent_task *task = manager == NULL ? NULL : manager->tasks;
    while (task != NULL) {
        if (task->id == id) {
            return task;
        }
        task = task->next;
    }
    return NULL;
}

ca_agent_manager *ca_agent_manager_new(void) {
    ca_agent_manager *manager = (ca_agent_manager *)calloc(1, sizeof(ca_agent_manager));
    if (manager != NULL) {
        manager->next_id = 1;
    }
    return manager;
}

void ca_agent_manager_free(ca_agent_manager *manager) {
    ca_agent_task *task;
    if (manager == NULL) {
        return;
    }
    task = manager->tasks;
    while (task != NULL) {
        ca_agent_task *next = task->next;
        free(task->name);
        free(task->description);
        free(task->message);
        free(task);
        task = next;
    }
    free(manager);
}

ca_status ca_agent_manager_spawn(ca_agent_manager *manager,
                                 const char *name,
                                 const char *description,
                                 unsigned *out_id) {
    ca_agent_task *task;
    if (manager == NULL || name == NULL || name[0] == '\0') {
        return CA_INVALID_ARGUMENT;
    }
    task = (ca_agent_task *)calloc(1, sizeof(ca_agent_task));
    if (task == NULL) {
        return CA_NO_MEMORY;
    }
    task->id = manager->next_id++;
    task->name = ca_strdup(name);
    task->description = ca_strdup(description == NULL ? "" : description);
    task->state = CA_MANAGED_AGENT_QUEUED;
    task->progress = 0;
    task->message = ca_strdup("");
    if (task->name == NULL || task->description == NULL || task->message == NULL) {
        free(task->name);
        free(task->description);
        free(task->message);
        free(task);
        return CA_NO_MEMORY;
    }
    task->next = manager->tasks;
    manager->tasks = task;
    if (out_id != NULL) {
        *out_id = task->id;
    }
    return CA_OK;
}

ca_status ca_agent_manager_update(ca_agent_manager *manager,
                                  unsigned id,
                                  ca_managed_agent_state state,
                                  unsigned progress,
                                  const char *message) {
    ca_agent_task *task = find_task(manager, id);
    char *next_message = NULL;
    if (task == NULL) {
        return CA_NOT_FOUND;
    }
    if (message != NULL) {
        next_message = ca_strdup(message);
        if (next_message == NULL) {
            return CA_NO_MEMORY;
        }
        free(task->message);
        task->message = next_message;
    }
    task->state = state;
    task->progress = progress > 100u ? 100u : progress;
    return CA_OK;
}

ca_status ca_agent_manager_cancel(ca_agent_manager *manager, unsigned id) {
    return ca_agent_manager_update(manager, id, CA_MANAGED_AGENT_CANCELLED, 100, "cancelled");
}

ca_status ca_agent_manager_list(const ca_agent_manager *manager,
                                ca_agent_task_snapshot **out_tasks,
                                size_t *out_count) {
    const ca_agent_task *task;
    ca_agent_task_snapshot *items;
    size_t count = 0;
    size_t i = 0;
    if (out_tasks == NULL || out_count == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *out_tasks = NULL;
    *out_count = 0;
    for (task = manager == NULL ? NULL : manager->tasks; task != NULL; task = task->next) {
        count++;
    }
    if (count == 0) {
        return CA_OK;
    }
    items = (ca_agent_task_snapshot *)calloc(count, sizeof(ca_agent_task_snapshot));
    if (items == NULL) {
        return CA_NO_MEMORY;
    }
    for (task = manager->tasks; task != NULL; task = task->next) {
        items[i].id = task->id;
        items[i].name = ca_strdup(task->name);
        items[i].description = ca_strdup(task->description);
        items[i].state = task->state;
        items[i].progress = task->progress;
        items[i].message = ca_strdup(task->message);
        if (items[i].name == NULL || items[i].description == NULL || items[i].message == NULL) {
            ca_agent_task_snapshots_free(items, count);
            return CA_NO_MEMORY;
        }
        i++;
    }
    *out_tasks = items;
    *out_count = count;
    return CA_OK;
}

void ca_agent_task_snapshots_free(ca_agent_task_snapshot *tasks, size_t count) {
    size_t i;
    if (tasks == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(tasks[i].name);
        free(tasks[i].description);
        free(tasks[i].message);
    }
    free(tasks);
}

ca_status ca_agent_manager_render(const ca_agent_manager *manager, char **out) {
    ca_agent_task_snapshot *tasks = NULL;
    size_t count = 0;
    size_t i;
    ca_string_builder sb;
    ca_status status;
    if (out == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *out = NULL;
    status = ca_agent_manager_list(manager, &tasks, &count);
    if (status != CA_OK) {
        return status;
    }
    ca_sb_init(&sb);
    if (count == 0) {
        ca_sb_append(&sb, "No background agents active.\n");
    } else {
        ca_sb_appendf(&sb, "Active agents: %lu\n", (unsigned long)count);
        for (i = 0; i < count; i++) {
            ca_sb_appendf(&sb,
                          "[%u] %s - %s (%u%%) %s\n",
                          tasks[i].id,
                          tasks[i].name,
                          managed_state_name(tasks[i].state),
                          tasks[i].progress,
                          tasks[i].message == NULL ? "" : tasks[i].message);
        }
    }
    ca_agent_task_snapshots_free(tasks, count);
    *out = ca_sb_take(&sb);
    return *out == NULL ? CA_NO_MEMORY : CA_OK;
}
