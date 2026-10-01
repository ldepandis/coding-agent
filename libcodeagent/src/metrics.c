/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "internal.h"

#include <stdlib.h>
#include <string.h>

size_t ca_estimate_tokens_text(const char *text) {
    return text == NULL || text[0] == '\0' ? 0 : (strlen(text) / 4u) + 1u;
}

size_t ca_estimate_tokens_messages(const ca_message *messages, size_t message_count) {
    size_t total = 0;
    size_t i;
    for (i = 0; i < message_count; i++) {
        size_t j;
        total += ca_estimate_tokens_text(messages[i].role);
        for (j = 0; j < messages[i].block_count; j++) {
            total += ca_estimate_tokens_text(messages[i].blocks[j].text);
            total += ca_estimate_tokens_text(messages[i].blocks[j].content);
            total += ca_estimate_tokens_text(messages[i].blocks[j].input_json);
        }
    }
    return total;
}

ca_status ca_estimate_cost_usd(const char *model,
                               size_t input_tokens,
                               size_t output_tokens,
                               double *cost_usd) {
    double input_per_million = 3.0;
    double output_per_million = 15.0;
    if (cost_usd == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    if (model != NULL && strstr(model, "haiku") != NULL) {
        input_per_million = 0.25;
        output_per_million = 1.25;
    } else if (model != NULL && strstr(model, "gpt-4.1-mini") != NULL) {
        input_per_million = 0.40;
        output_per_million = 1.60;
    } else if (model != NULL && strstr(model, "gpt-4.1") != NULL) {
        input_per_million = 2.00;
        output_per_million = 8.00;
    }
    *cost_usd = ((double)input_tokens * input_per_million +
                 (double)output_tokens * output_per_million) /
                1000000.0;
    return CA_OK;
}

void ca_cost_tracker_init(ca_cost_tracker *tracker, const char *model) {
    if (tracker == NULL) {
        return;
    }
    memset(tracker, 0, sizeof(*tracker));
    tracker->model = ca_strdup(model == NULL || model[0] == '\0' ? CODEAGENT_DEFAULT_MODEL : model);
}

void ca_cost_tracker_free(ca_cost_tracker *tracker) {
    if (tracker == NULL) {
        return;
    }
    free(tracker->model);
    memset(tracker, 0, sizeof(*tracker));
}

ca_status ca_cost_tracker_add(ca_cost_tracker *tracker, size_t input_tokens, size_t output_tokens) {
    if (tracker == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    tracker->input_tokens += input_tokens;
    tracker->output_tokens += output_tokens;
    tracker->messages++;
    return CA_OK;
}

ca_status ca_cost_tracker_total_usd(const ca_cost_tracker *tracker, double *cost_usd) {
    if (tracker == NULL || cost_usd == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    return ca_estimate_cost_usd(tracker->model, tracker->input_tokens, tracker->output_tokens, cost_usd);
}

ca_status ca_cost_tracker_render(const ca_cost_tracker *tracker, char **out) {
    ca_string_builder sb;
    double cost = 0.0;
    if (tracker == NULL || out == NULL) {
        return CA_INVALID_ARGUMENT;
    }
    *out = NULL;
    ca_cost_tracker_total_usd(tracker, &cost);
    ca_sb_init(&sb);
    ca_sb_appendf(&sb,
                  "Model: %s\nInput tokens: %lu\nOutput tokens: %lu\nMessages: %lu\nEstimated cost: $%.6f\n",
                  tracker->model == NULL ? CODEAGENT_DEFAULT_MODEL : tracker->model,
                  (unsigned long)tracker->input_tokens,
                  (unsigned long)tracker->output_tokens,
                  (unsigned long)tracker->messages,
                  cost);
    *out = ca_sb_take(&sb);
    return *out == NULL ? CA_NO_MEMORY : CA_OK;
}
