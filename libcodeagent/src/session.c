/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

struct ca_session {
    char *id;
    ca_message *messages;
    size_t message_count;
};

struct ca_session_manager {
    char *directory;
};

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

static ca_status append_text_message(ca_session *session, const char *role, const char *text) {
    ca_message message;
    if (session == NULL || role == NULL || text == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    if (strcmp(role, "assistant") == 0) {
        ca_content_block block = ca_content_text(text);
        message = ca_message_assistant(&block, 1);
        ca_content_block_free(&block);
    } else {
        message = ca_message_user(text);
        free(message.role);
        message.role = ca_strdup(role);
    }
    {
        ca_status status = ca_session_append(session, &message);
        ca_message_free(&message);
        return status;
    }
}

ca_session *ca_session_new(const char *id) {
    ca_session *session = (ca_session *)calloc(1, sizeof(ca_session));
    if (session == NULL) {
        return NULL;
    }
    session->id = ca_strdup(id == NULL || id[0] == '\0' ? "default" : id);
    if (session->id == NULL) {
        ca_session_free(session);
        return NULL;
    }
    return session;
}

void ca_session_free(ca_session *session) {
    size_t i;
    if (session == NULL) {
        return;
    }
    free(session->id);
    for (i = 0; i < session->message_count; i++) {
        ca_message_free(&session->messages[i]);
    }
    free(session->messages);
    free(session);
}

const char *ca_session_id(const ca_session *session) {
    return session == NULL ? NULL : session->id;
}

ca_status ca_session_append(ca_session *session, const ca_message *message) {
    ca_message *next;
    if (session == NULL || message == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    next = (ca_message *)realloc(session->messages, sizeof(ca_message) * (session->message_count + 1));
    if (next == NULL) {
        return CA_NO_MEMORY;
    }
    session->messages = next;
    memset(&session->messages[session->message_count], 0, sizeof(ca_message));
    if (ca_message_clone(message, &session->messages[session->message_count]) != CA_OK) {
        return CA_NO_MEMORY;
    }
    session->message_count++;
    return CA_OK;
}

const ca_message *ca_session_messages(const ca_session *session, size_t *message_count) {
    if (message_count != NULL) {
        *message_count = session == NULL ? 0 : session->message_count;
    }
    return session == NULL ? NULL : session->messages;
}

ca_status ca_session_save_markdown(const ca_session *session, const char *path) {
    FILE *file;
    char *markdown = NULL;
    ca_status status;
    if (session == NULL || path == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    status = ca_session_render_markdown(session, &markdown);
    if (status != CA_OK) {
        return status;
    }
    file = fopen(path, "wb");
    if (file == NULL) {
        free(markdown);
        return CA_IO_ERROR;
    }
    if (fwrite(markdown, 1, strlen(markdown), file) != strlen(markdown)) {
        free(markdown);
        fclose(file);
        return CA_IO_ERROR;
    }
    free(markdown);
    return fclose(file) == 0 ? CA_OK : CA_IO_ERROR;
}

ca_status ca_session_render_markdown(const ca_session *session, char **out_markdown) {
    ca_string_builder sb;
    size_t i;
    if (session == NULL || out_markdown == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *out_markdown = NULL;
    ca_sb_init(&sb);
    ca_sb_appendf(&sb, "# Session %s\n\n", session->id == NULL ? "default" : session->id);
    for (i = 0; i < session->message_count; i++) {
        size_t j;
        const ca_message *message = &session->messages[i];
        ca_sb_appendf(&sb, "## %s\n\n", message->role == NULL ? "message" : message->role);
        for (j = 0; j < message->block_count; j++) {
            const ca_content_block *block = &message->blocks[j];
            if (block->type == CA_BLOCK_TEXT) {
                ca_sb_appendf(&sb, "%s\n\n", block->text == NULL ? "" : block->text);
            } else if (block->type == CA_BLOCK_TOOL_RESULT) {
                ca_sb_appendf(&sb,
                              "Tool result `%s`: %s\n\n",
                              block->tool_use_id == NULL ? "" : block->tool_use_id,
                              block->content == NULL ? "" : block->content);
            } else {
                ca_sb_appendf(&sb,
                              "Tool use `%s` `%s`: %s\n\n",
                              block->id == NULL ? "" : block->id,
                              block->name == NULL ? "" : block->name,
                              block->input_json == NULL ? "{}" : block->input_json);
            }
        }
    }
    *out_markdown = ca_sb_take(&sb);
    return *out_markdown == NULL ? CA_NO_MEMORY : CA_OK;
}

ca_status ca_session_load_markdown_text(ca_session **session,
                                        const char *session_id,
                                        const char *markdown) {
    char *content;
    char *cursor;
    char *section_role = NULL;
    ca_string_builder section_text;
    if (session == NULL || markdown == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *session = NULL;
    content = ca_strdup(markdown);
    if (content == NULL) {
        return CA_NO_MEMORY;
    }
    *session = ca_session_new(session_id == NULL ? "markdown" : session_id);
    if (*session == NULL) {
        free(content);
        return CA_NO_MEMORY;
    }
    ca_sb_init(&section_text);
    cursor = content;
    while (cursor != NULL && *cursor != '\0') {
        char *line = cursor;
        char *next = strchr(cursor, '\n');
        if (next != NULL) {
            *next = '\0';
            cursor = next + 1;
        } else {
            cursor = NULL;
        }
        if (strncmp(line, "## ", 3) == 0) {
            if (section_role != NULL) {
                char *text = ca_sb_take(&section_text);
                append_text_message(*session, section_role, text == NULL ? "" : text);
                free(text);
                ca_sb_init(&section_text);
                free(section_role);
            }
            section_role = ca_strdup(line + 3);
        } else if (section_role != NULL) {
            ca_sb_append(&section_text, line);
            ca_sb_append(&section_text, "\n");
        }
    }
    if (section_role != NULL) {
        char *text = ca_sb_take(&section_text);
        append_text_message(*session, section_role, text == NULL ? "" : text);
        free(text);
        free(section_role);
    } else {
        ca_sb_free(&section_text);
        append_text_message(*session, "user", content);
    }
    free(content);
    return CA_OK;
}

ca_status ca_session_load_markdown(ca_session **session, const char *path) {
    FILE *file;
    char *content = NULL;
    ca_status status;
    if (session == NULL || path == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *session = NULL;
    file = fopen(path, "rb");
    if (file == NULL) {
        return CA_NOT_FOUND;
    }
    if (ca_read_all(file, &content, NULL, 0) != CA_OK) {
        fclose(file);
        return CA_IO_ERROR;
    }
    fclose(file);
    status = ca_session_load_markdown_text(session, path, content);
    free(content);
    return status;
}

ca_session_manager *ca_session_manager_new(const char *directory) {
    ca_session_manager *manager = (ca_session_manager *)calloc(1, sizeof(ca_session_manager));
    if (manager == NULL) {
        return NULL;
    }
    manager->directory = ca_strdup(directory == NULL || directory[0] == '\0' ? ".specstory/history/" : directory);
    if (manager->directory == NULL) {
        free(manager);
        return NULL;
    }
    ensure_dir(manager->directory);
    return manager;
}

void ca_session_manager_free(ca_session_manager *manager) {
    if (manager == NULL) {
        return;
    }
    free(manager->directory);
    free(manager);
}

const char *ca_session_manager_directory(const ca_session_manager *manager) {
    return manager == NULL ? NULL : manager->directory;
}

ca_status ca_session_manager_save(ca_session_manager *manager,
                                  const ca_session *session,
                                  char **out_path) {
    char path[PATH_MAX];
    time_t now = time(NULL);
    struct tm value;
    struct tm *ptr = localtime(&now);
    if (manager == NULL || session == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    if (ensure_dir(manager->directory) != 0) {
        return CA_IO_ERROR;
    }
    if (ptr != NULL) {
        value = *ptr;
    } else {
        memset(&value, 0, sizeof(value));
    }
    snprintf(path,
             sizeof(path),
             "%s/%04d%02d%02d-%02d%02d%02d-%ld.md",
             manager->directory,
             value.tm_year + 1900,
             value.tm_mon + 1,
             value.tm_mday,
             value.tm_hour,
             value.tm_min,
             value.tm_sec,
             (long)getpid());
    if (ca_session_save_markdown(session, path) != CA_OK) {
        return CA_IO_ERROR;
    }
    if (out_path != NULL) {
        *out_path = ca_strdup(path);
        if (*out_path == NULL) {
            return CA_NO_MEMORY;
        }
    }
    return CA_OK;
}

static int is_markdown_file(const char *name) {
    size_t len = name == NULL ? 0 : strlen(name);
    return len >= 4 && strcmp(name + len - 3, ".md") == 0;
}

ca_status ca_session_manager_list(ca_session_manager *manager,
                                  char ***out_paths,
                                  size_t *out_count) {
    DIR *dir;
    struct dirent *entry;
    char **items = NULL;
    size_t count = 0;
    if (manager == NULL || out_paths == NULL || out_count == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *out_paths = NULL;
    *out_count = 0;
    dir = opendir(manager->directory);
    if (dir == NULL) {
        return CA_NOT_FOUND;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        char **next;
        if (!is_markdown_file(entry->d_name)) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", manager->directory, entry->d_name);
        next = (char **)realloc(items, sizeof(char *) * (count + 1));
        if (next == NULL) {
            closedir(dir);
            ca_string_list_free(items, count);
            return CA_NO_MEMORY;
        }
        items = next;
        items[count] = ca_strdup(path);
        if (items[count] == NULL) {
            closedir(dir);
            ca_string_list_free(items, count);
            return CA_NO_MEMORY;
        }
        count++;
    }
    closedir(dir);
    *out_paths = items;
    *out_count = count;
    return CA_OK;
}

ca_status ca_session_manager_load_latest(ca_session_manager *manager, ca_session **session) {
    DIR *dir;
    struct dirent *entry;
    char best_path[PATH_MAX];
    time_t best_time = 0;
    int found = 0;
    if (manager == NULL || session == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *session = NULL;
    dir = opendir(manager->directory);
    if (dir == NULL) {
        return CA_NOT_FOUND;
    }
    while ((entry = readdir(dir)) != NULL) {
        char path[PATH_MAX];
        struct stat st;
        if (!is_markdown_file(entry->d_name)) {
            continue;
        }
        snprintf(path, sizeof(path), "%s/%s", manager->directory, entry->d_name);
        if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }
        if (!found || st.st_mtime > best_time) {
            snprintf(best_path, sizeof(best_path), "%s", path);
            best_time = st.st_mtime;
            found = 1;
        }
    }
    closedir(dir);
    if (!found) {
        return CA_NOT_FOUND;
    }
    return ca_session_load_markdown(session, best_path);
}

ca_status ca_storage_save_session(const ca_storage_adapter *adapter,
                                  const char *key,
                                  const ca_session *session,
                                  char **out_ref) {
    char *markdown = NULL;
    ca_status status;
    if (adapter == NULL || adapter->save == NULL || session == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    if (out_ref != NULL) {
        *out_ref = NULL;
    }
    status = ca_session_render_markdown(session, &markdown);
    if (status != CA_OK) {
        return status;
    }
    status = adapter->save(key == NULL ? ca_session_id(session) : key,
                           markdown,
                           out_ref,
                           adapter->userdata);
    free(markdown);
    return status;
}

ca_status ca_storage_load_session(const ca_storage_adapter *adapter,
                                  const char *ref,
                                  ca_session **session) {
    char *markdown = NULL;
    ca_status status;
    if (adapter == NULL || adapter->load == NULL || ref == NULL || session == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *session = NULL;
    status = adapter->load(ref, &markdown, adapter->userdata);
    if (status != CA_OK) {
        return status;
    }
    status = ca_session_load_markdown_text(session, ref, markdown == NULL ? "" : markdown);
    free(markdown);
    return status;
}

ca_status ca_storage_list_sessions(const ca_storage_adapter *adapter,
                                   char ***out_refs,
                                   size_t *out_count) {
    if (adapter == NULL || adapter->list == NULL || out_refs == NULL || out_count == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *out_refs = NULL;
    *out_count = 0;
    return adapter->list(out_refs, out_count, adapter->userdata);
}

void ca_string_list_free(char **items, size_t count) {
    size_t i;
    if (items == NULL) {
        return;
    }
    for (i = 0; i < count; i++) {
        free(items[i]);
    }
    free(items);
}
