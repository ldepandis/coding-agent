/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#include "ui.h"

#include "codeagent/codeagent.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curses.h>

void codeagentctl_ui_init(void) {
    initscr();
    raw();
    keypad(stdscr, TRUE);
    noecho();
    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_RED, -1);
        init_pair(3, COLOR_YELLOW, -1);
        init_pair(4, COLOR_GREEN, -1);
    }
    endwin();
}

void codeagentctl_ui_shutdown(void) {
    endwin();
}

void codeagentctl_print_banner(void) {
    puts("┌─────────────────────────────────────────────────────┐");
    puts("│                                                     │");
    puts("│   ██████╗ ██████╗ ██████╗ ███████╗                  │");
    puts("│  ██╔════╝██╔═══██╗██╔══██╗██╔════╝                  │");
    puts("│  ██║     ██║   ██║██║  ██║█████╗                    │");
    puts("│  ██║     ██║   ██║██║  ██║██╔══╝                    │");
    puts("│  ╚██████╗╚██████╔╝██████╔╝███████╗                  │");
    puts("│   ╚═════╝ ╚═════╝ ╚═════╝ ╚══════╝                  │");
    puts("│                                                     │");
    puts("│   codeagentctl " CODEAGENT_VERSION "                                │");
    puts("│                                                     │");
    puts("│   [n] New  [r] Resume                               │");
    puts("│   [h] Help [c] Config [q] Quit                      │");
    puts("│                                                     │");
    puts("└─────────────────────────────────────────────────────┘");
}

void codeagentctl_print_startup_menu(int has_history) {
    puts("\n[n] New session");
    printf("[r] Resume last session%s\n", has_history ? "" : " (none found)");
    puts("[h] Help");
    puts("[c] Config");
    puts("[q] Quit");
}

static void print_markdown_line(const char *line, int *in_code) {
    if (strncmp(line, "```", 3) == 0) {
        *in_code = !*in_code;
        puts(*in_code ? "┌─ code ─────────────────────────────────────────────" :
                        "└───────────────────────────────────────────────────");
        return;
    }
    if (*in_code) {
        printf("│ %s\n", line);
    } else if (line[0] == '#' && line[1] == ' ') {
        printf("\n%s\n", line + 2);
    } else if (line[0] == '#' && line[1] == '#' && line[2] == ' ') {
        printf("\n%s\n", line + 3);
    } else {
        puts(line);
    }
}

void codeagentctl_print_text(const char *text) {
    if (text != NULL && text[0] != '\0') {
        const char *start = text;
        const char *p = text;
        int in_code = 0;
        putchar('\n');
        while (*p != '\0') {
            if (*p == '\n') {
                char line[4096];
                size_t len = (size_t)(p - start);
                if (len >= sizeof(line)) {
                    len = sizeof(line) - 1;
                }
                memcpy(line, start, len);
                line[len] = '\0';
                print_markdown_line(line, &in_code);
                start = p + 1;
            }
            p++;
        }
        if (start != p) {
            char line[4096];
            size_t len = (size_t)(p - start);
            if (len >= sizeof(line)) {
                len = sizeof(line) - 1;
            }
            memcpy(line, start, len);
            line[len] = '\0';
            print_markdown_line(line, &in_code);
        }
        putchar('\n');
    }
}

void codeagentctl_print_error(const char *text) {
    fprintf(stderr, "\nError: %s\n", text == NULL ? "" : text);
}

void codeagentctl_print_warning(const char *text) {
    fprintf(stderr, "\nWarning: %s\n", text == NULL ? "" : text);
}

void codeagentctl_print_context_bar(size_t tokens, unsigned context_window) {
    unsigned percent = context_window == 0 ? 0 : (unsigned)((tokens * 100u) / context_window);
    unsigned filled;
    unsigned i;
    if (percent > 100u) {
        percent = 100u;
    }
    filled = (percent * 24u) / 100u;
    printf("context [");
    for (i = 0; i < 24u; i++) {
        putchar(i < filled ? '#' : '-');
    }
    printf("] %u%% (%lu/%u tokens)\n", percent, (unsigned long)tokens, context_window);
}

void codeagentctl_print_progress(const char *label, unsigned percent) {
    unsigned filled;
    unsigned i;
    if (percent > 100u) {
        percent = 100u;
    }
    filled = (percent * 20u) / 100u;
    printf("%s [", label == NULL ? "progress" : label);
    for (i = 0; i < 20u; i++) {
        putchar(i < filled ? '#' : '-');
    }
    printf("] %u%%\n", percent);
}

void codeagentctl_print_tool_started(const char *tool_name) {
    printf("● %s\n", tool_name == NULL ? "tool" : tool_name);
}

void codeagentctl_print_tool_finished(const char *summary) {
    printf("✓ %s\n", summary == NULL ? "done" : summary);
}

void codeagentctl_print_tool_result(const char *summary, int expanded) {
    if (expanded) {
        printf("\n--- tool result ---\n%s\n-------------------\n", summary == NULL ? "" : summary);
    } else {
        const char *text = summary == NULL ? "" : summary;
        size_t len = strlen(text);
        printf("tool result: %.*s%s\n", len > 120u ? 120 : (int)len, text, len > 120u ? "..." : "");
    }
}

char *codeagentctl_read_input(void) {
    char buffer[4096];
    {
        char *result = NULL;
        size_t len = 0;
        size_t cap = 0;
        int first = 1;

        for (;;) {
            size_t line_len;
            char *next;
            printf(first ? "\n> " : "| ");
            fflush(stdout);
            if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
                free(result);
                return NULL;
            }
            line_len = strcspn(buffer, "\r\n");
            buffer[line_len] = '\0';
            if (first && buffer[0] == '/') {
                return ca_strdup(buffer);
            }
            if (line_len == 0) {
                if (len == 0) {
                    continue;
                }
                return result == NULL ? ca_strdup("") : result;
            }
            if (len > SIZE_MAX - line_len - 2) {
                free(result);
                return NULL;
            }
            if (len + line_len + 2 > cap) {
                cap = cap == 0 ? 4096 : cap * 2;
                while (cap < len + line_len + 2) {
                    if (cap > SIZE_MAX / 2) {
                        free(result);
                        return NULL;
                    }
                    cap *= 2;
                }
                next = (char *)realloc(result, cap);
                if (next == NULL) {
                    free(result);
                    return NULL;
                }
                result = next;
            }
            if (!first) {
                result[len++] = '\n';
            }
            memcpy(result + len, buffer, line_len);
            len += line_len;
            result[len] = '\0';
            first = 0;
        }
    }
}

int codeagentctl_startup_choice(void) {
    char buffer[32];
    printf("\nSelect [n/r/h/c/q]: ");
    fflush(stdout);
    if (fgets(buffer, sizeof(buffer), stdin) == NULL) {
        return 'q';
    }
    if (buffer[0] == '\0' || buffer[0] == '\n' || buffer[0] == '\r') {
        return 'n';
    }
    return buffer[0];
}
