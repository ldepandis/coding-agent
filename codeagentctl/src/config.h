/*
 * Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
 *
 * SPDX-License-Identifier: ISC
 */

#ifndef CODEAGENTCTL_CONFIG_H
#define CODEAGENTCTL_CONFIG_H

#include "codeagent/codeagent.h"

ca_status codeagentctl_config_load(ca_config *config, const char *path);
char *codeagentctl_default_config_path(void);

#endif
