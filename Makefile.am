# Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
#
# SPDX-License-Identifier: ISC

ACLOCAL_AMFLAGS = -I m4

AM_CPPFLAGS = \
	-I$(top_srcdir)/libcodeagent/include \
	-I$(top_srcdir)/libcodeagent/src \
	-D_POSIX_C_SOURCE=200809L \
	$(CODEAGENT_HARDENING_CPPFLAGS)

AM_CFLAGS = \
	$(CODEAGENT_WARNING_CFLAGS) \
	$(CODEAGENT_HARDENING_CFLAGS) \
	$(CODEAGENT_SANITIZER_CFLAGS)

AM_LDFLAGS = \
	$(CODEAGENT_HARDENING_LDFLAGS) \
	$(CODEAGENT_SANITIZER_LIBS)

lib_LTLIBRARIES =
bin_PROGRAMS =
check_PROGRAMS =
TESTS =
pkgconfig_DATA =
codeagentinclude_HEADERS =

pkgconfigdir = $(libdir)/pkgconfig
codeagentincludedir = $(includedir)/codeagent

LIBCODEAGENT_SOURCES = \
	libcodeagent/src/agent.c \
	libcodeagent/src/agents.c \
	libcodeagent/src/autofix.c \
	libcodeagent/src/commands.c \
	libcodeagent/src/config.c \
	libcodeagent/src/conversation.c \
	libcodeagent/src/git.c \
	libcodeagent/src/json.c \
	libcodeagent/src/metrics.c \
	libcodeagent/src/obsidian.c \
	libcodeagent/src/permissions.c \
	libcodeagent/src/provider.c \
	libcodeagent/src/sandbox.c \
	libcodeagent/src/session.c \
	libcodeagent/src/state_machine.c \
	libcodeagent/src/string_builder.c \
	libcodeagent/src/tools.c \
	libcodeagent/providers/anthropic.c \
	libcodeagent/providers/openai_compatible.c \
	libcodeagent/providers/ollama.c

LIBCODEAGENT_PRIVATE_HDRS = \
	libcodeagent/src/internal.h \
	libcodeagent/src/provider.h

LIBCODEAGENT_TEST_CPPFLAGS =
if BUILD_TESTS
LIBCODEAGENT_TEST_CPPFLAGS += -DCODEAGENT_TESTING=1
endif

if BUILD_LIBRARY
lib_LTLIBRARIES += libcodeagent.la
pkgconfig_DATA += codeagent.pc
codeagentinclude_HEADERS += libcodeagent/include/codeagent/codeagent.h

libcodeagent_la_SOURCES = \
	$(LIBCODEAGENT_SOURCES) \
	$(LIBCODEAGENT_PRIVATE_HDRS)

libcodeagent_la_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	$(LIBCODEAGENT_TEST_CPPFLAGS) \
	$(CODEAGENT_CURL_CPPFLAGS)

libcodeagent_la_LDFLAGS = \
	$(AM_LDFLAGS) \
	-version-info $(CODEAGENT_LT_VERSION) \
	-no-undefined

libcodeagent_la_LIBADD = $(CURL_LIBS)
endif

CODEAGENTCTL_CODEAGENT_SOURCES =

if CODEAGENTCTL_BUILTIN
CODEAGENTCTL_CODEAGENT_CPPFLAGS = -I$(top_srcdir)/libcodeagent/include
CODEAGENTCTL_CODEAGENT_LIBS = $(CURL_LIBS)
CODEAGENTCTL_CODEAGENT_SOURCES += \
	$(LIBCODEAGENT_SOURCES) \
	$(LIBCODEAGENT_PRIVATE_HDRS)
else
if USE_SYSTEM_LIBCODEAGENT
CODEAGENTCTL_CODEAGENT_CPPFLAGS = $(CODEAGENT_SYSTEM_CFLAGS)
CODEAGENTCTL_CODEAGENT_LIBS = $(CODEAGENT_SYSTEM_LIBS)
else
CODEAGENTCTL_CODEAGENT_CPPFLAGS = -I$(top_srcdir)/libcodeagent/include
CODEAGENTCTL_CODEAGENT_LIBS = libcodeagent.la
endif
endif

if BUILD_CLI
bin_PROGRAMS += codeagentctl/codeagentctl

codeagentctl_codeagentctl_SOURCES = \
	codeagentctl/src/config.c \
	codeagentctl/src/config.h \
	codeagentctl/src/main.c \
	codeagentctl/src/ui.c \
	codeagentctl/src/ui.h \
	$(CODEAGENTCTL_CODEAGENT_SOURCES)

codeagentctl_codeagentctl_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/codeagentctl/src \
	$(CODEAGENTCTL_CODEAGENT_CPPFLAGS) \
	$(CODEAGENT_CURL_CPPFLAGS) \
	$(UCL_CFLAGS) \
	$(CODEAGENTCTL_CURSES_CPPFLAGS)

codeagentctl_codeagentctl_LDADD = \
	$(CODEAGENTCTL_CODEAGENT_LIBS) \
	$(UCL_LIBS) \
	$(CODEAGENTCTL_CURSES_LIBS)
endif

if BUILD_TESTS
if BUILD_LIBRARY
check_PROGRAMS += codeagent_tests
TESTS += codeagent_tests

codeagent_tests_SOURCES = libcodeagent/tests/test_main.c
codeagent_tests_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-DCODEAGENT_TESTING=1
codeagent_tests_LDADD = libcodeagent.la

if BUILD_FUZZERS
check_PROGRAMS += codeagent_fuzz_smoke
TESTS += codeagent_fuzz_smoke

codeagent_fuzz_smoke_SOURCES = libcodeagent/tests/fuzz_json_config.c
codeagent_fuzz_smoke_CPPFLAGS = $(AM_CPPFLAGS)
codeagent_fuzz_smoke_LDADD = libcodeagent.la
endif
endif

if BUILD_CLI
check_PROGRAMS += codeagentctl_config_tests codeagentctl_snapshot_tests
TESTS += codeagentctl_config_tests codeagentctl_snapshot_tests

codeagentctl_config_tests_SOURCES = \
	codeagentctl/src/config.c \
	codeagentctl/src/config.h \
	codeagentctl/tests/test_cli_config.c \
	$(CODEAGENTCTL_CODEAGENT_SOURCES)

codeagentctl_config_tests_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/codeagentctl/src \
	$(CODEAGENTCTL_CODEAGENT_CPPFLAGS) \
	$(CODEAGENT_CURL_CPPFLAGS) \
	$(UCL_CFLAGS)

codeagentctl_config_tests_LDADD = \
	$(CODEAGENTCTL_CODEAGENT_LIBS) \
	$(UCL_LIBS)

codeagentctl_snapshot_tests_SOURCES = \
	codeagentctl/src/ui.c \
	codeagentctl/src/ui.h \
	codeagentctl/tests/test_cli_snapshot.c \
	$(CODEAGENTCTL_CODEAGENT_SOURCES)

codeagentctl_snapshot_tests_CPPFLAGS = \
	$(AM_CPPFLAGS) \
	-I$(top_srcdir)/codeagentctl/src \
	$(CODEAGENTCTL_CODEAGENT_CPPFLAGS) \
	$(CODEAGENT_CURL_CPPFLAGS) \
	$(CODEAGENTCTL_CURSES_CPPFLAGS)

codeagentctl_snapshot_tests_LDADD = \
	$(CODEAGENTCTL_CODEAGENT_LIBS) \
	$(CODEAGENTCTL_CURSES_LIBS)
endif
endif

.PHONY: codeagent-clang-tidy codeagent-cppcheck
.PHONY: build test lint clang-tidy cppcheck c-build c-test c-clean run-codeagentctl

codeagent-clang-tidy:
	@test -n "$(CLANG_TIDY)" || { echo "clang-tidy not found"; exit 77; }
	$(CLANG_TIDY) \
		--checks=clang-analyzer-*,bugprone-*,performance-*,portability-* \
		$(top_srcdir)/libcodeagent/src/*.c \
		$(top_srcdir)/codeagentctl/src/*.c \
		-- \
		-std=c99 \
		$(AM_CPPFLAGS) \
		$(CODEAGENT_CURL_CPPFLAGS) \
		-I$(top_srcdir)/codeagentctl/src \
		$(CODEAGENTCTL_CODEAGENT_CPPFLAGS) \
		$(UCL_CFLAGS) \
		$(CODEAGENTCTL_CURSES_CPPFLAGS)

codeagent-cppcheck:
	@test -n "$(CPPCHECK)" || { echo "cppcheck not found"; exit 77; }
	$(CPPCHECK) \
		--enable=warning,style,performance,portability \
		--std=c99 \
		--inline-suppr \
		-I$(top_srcdir)/libcodeagent/include \
		-I$(top_srcdir)/libcodeagent/src \
		-I$(top_srcdir)/codeagentctl/src \
		$(top_srcdir)/libcodeagent/src \
		$(top_srcdir)/codeagentctl/src

c-build: all

build: all

c-test: check

test: check

c-clean: distclean

run-codeagentctl: codeagentctl/codeagentctl
	./codeagentctl/codeagentctl

clang-tidy: codeagent-clang-tidy

cppcheck: codeagent-cppcheck

lint: codeagent-clang-tidy codeagent-cppcheck

EXTRA_DIST = docs
