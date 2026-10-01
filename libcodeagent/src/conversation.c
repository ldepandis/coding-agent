/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

static ca_status clone_blocks(const ca_content_block *src, size_t count, ca_content_block **dst) {
    size_t i;

    *dst = NULL;
    if (count == 0) {
        return CA_OK;
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

ca_content_block ca_content_text(const char *text) {
    ca_content_block block;
    memset(&block, 0, sizeof(block));
    block.type = CA_BLOCK_TEXT;
    block.text = ca_strdup(text);
    return block;
}

ca_content_block ca_content_tool_use(const char *id, const char *name, const char *input_json) {
    ca_content_block block;
    memset(&block, 0, sizeof(block));
    block.type = CA_BLOCK_TOOL_USE;
    block.id = ca_strdup(id);
    block.name = ca_strdup(name);
    block.input_json = ca_strdup(input_json == NULL ? "{}" : input_json);
    return block;
}

ca_content_block ca_content_tool_result(const char *tool_use_id, const char *content, int is_error) {
    ca_content_block block;
    memset(&block, 0, sizeof(block));
    block.type = CA_BLOCK_TOOL_RESULT;
    block.tool_use_id = ca_strdup(tool_use_id);
    block.content = ca_strdup(content);
    block.is_error = is_error;
    return block;
}

void ca_content_block_free(ca_content_block *block) {
    if (block == NULL) {
        return;
    }
    free(block->text);
    free(block->id);
    free(block->name);
    free(block->input_json);
    free(block->tool_use_id);
    free(block->content);
    memset(block, 0, sizeof(*block));
}

ca_status ca_content_block_clone(const ca_content_block *src, ca_content_block *dst) {
    if (src == NULL || dst == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof(*dst));
    dst->type = src->type;
    dst->is_error = src->is_error;
    dst->text = ca_strdup(src->text);
    dst->id = ca_strdup(src->id);
    dst->name = ca_strdup(src->name);
    dst->input_json = ca_strdup(src->input_json);
    dst->tool_use_id = ca_strdup(src->tool_use_id);
    dst->content = ca_strdup(src->content);
    if (dst->text == NULL || dst->id == NULL || dst->name == NULL || dst->input_json == NULL ||
        dst->tool_use_id == NULL || dst->content == NULL) {
        ca_content_block_free(dst);
        return CA_NO_MEMORY;
    }
    return CA_OK;
}

ca_message ca_message_user(const char *text) {
    ca_message message;
    memset(&message, 0, sizeof(message));
    message.role = ca_strdup("user");
    message.blocks = (ca_content_block *)calloc(1, sizeof(ca_content_block));
    if (message.blocks != NULL) {
        message.blocks[0] = ca_content_text(text);
        message.block_count = 1;
    }
    return message;
}

ca_message ca_message_assistant(ca_content_block *blocks, size_t block_count) {
    ca_message message;
    memset(&message, 0, sizeof(message));
    message.role = ca_strdup("assistant");
    clone_blocks(blocks, block_count, &message.blocks);
    message.block_count = block_count;
    return message;
}

ca_message ca_message_tool_result(const char *tool_use_id, const char *content, int is_error) {
    ca_message message;
    memset(&message, 0, sizeof(message));
    message.role = ca_strdup("user");
    message.blocks = (ca_content_block *)calloc(1, sizeof(ca_content_block));
    if (message.blocks != NULL) {
        message.blocks[0] = ca_content_tool_result(tool_use_id, content, is_error);
        message.block_count = 1;
    }
    return message;
}

void ca_message_free(ca_message *message) {
    size_t i;
    if (message == NULL) {
        return;
    }
    free(message->role);
    for (i = 0; i < message->block_count; i++) {
        ca_content_block_free(&message->blocks[i]);
    }
    free(message->blocks);
    memset(message, 0, sizeof(*message));
}

ca_status ca_message_clone(const ca_message *src, ca_message *dst) {
    ca_status status;
    if (src == NULL || dst == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    memset(dst, 0, sizeof(*dst));
    dst->role = ca_strdup(src->role);
    if (dst->role == NULL) {
        return CA_NO_MEMORY;
    }
    status = clone_blocks(src->blocks, src->block_count, &dst->blocks);
    if (status != CA_OK) {
        ca_message_free(dst);
        return status;
    }
    dst->block_count = src->block_count;
    return CA_OK;
}
