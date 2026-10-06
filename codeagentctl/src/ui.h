/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#ifndef CODEAGENTCTL_UI_H
#define CODEAGENTCTL_UI_H

#include <stddef.h>

void codeagentctl_ui_init(void);
void codeagentctl_ui_shutdown(void);
void codeagentctl_print_banner(void);
void codeagentctl_print_startup_menu(int has_history);
void codeagentctl_print_text(const char *text);
void codeagentctl_print_error(const char *text);
void codeagentctl_print_warning(const char *text);
void codeagentctl_print_context_bar(size_t tokens, unsigned context_window);
void codeagentctl_print_progress(const char *label, unsigned percent);
void codeagentctl_print_tool_started(const char *tool_name);
void codeagentctl_print_tool_finished(const char *summary);
void codeagentctl_print_tool_result(const char *summary, int expanded);
char *codeagentctl_read_input(void);
int codeagentctl_startup_choice(void);

#endif
