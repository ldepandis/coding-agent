# Copyright (c) 2026 Luca De Pandis <git.69kft@slmail.me>
#
# SPDX-License-Identifier: ISC

AC_INIT([codeagent], [0.1.0], [https://example.invalid/codeagent/issues])
AC_CONFIG_SRCDIR([libcodeagent/include/codeagent/codeagent.h])
AC_CONFIG_MACRO_DIRS([m4])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_FILES([Makefile codeagent.pc])

AM_INIT_AUTOMAKE([foreign subdir-objects -Wall])
AM_SILENT_RULES([yes])

AC_USE_SYSTEM_EXTENSIONS
AC_PROG_CC
AM_PROG_AR
LT_INIT
PKG_PROG_PKG_CONFIG

AC_ARG_ENABLE([library],
    [AS_HELP_STRING([--disable-library], [do not build libcodeagent from this source tree])],
    [],
    [enable_library=yes])

AC_ARG_ENABLE([cli],
    [AS_HELP_STRING([--disable-cli], [do not build codeagentctl])],
    [],
    [enable_cli=yes])

AC_ARG_WITH([system-libcodeagent],
    [AS_HELP_STRING([--with-system-libcodeagent], [build codeagentctl against an installed libcodeagent found by pkg-config])],
    [],
    [with_system_libcodeagent=no])

AC_ARG_ENABLE([codeagentctl-builtin],
    [AS_HELP_STRING([--enable-codeagentctl-builtin], [build codeagentctl with libcodeagent compiled directly into the executable])],
    [],
    [enable_codeagentctl_builtin=no])

AS_IF([test "x$with_system_libcodeagent" = "xyes"], [enable_library=no])
AS_IF([test "x$enable_codeagentctl_builtin" = "xyes" && test "x$with_system_libcodeagent" = "xyes"], [
    AC_MSG_ERROR([--enable-codeagentctl-builtin conflicts with --with-system-libcodeagent])
])
AS_IF([test "x$enable_codeagentctl_builtin" = "xyes" && test "x$enable_cli" != "xyes"], [
    AC_MSG_ERROR([--enable-codeagentctl-builtin requires --enable-cli])
])
AS_IF([test "x$enable_cli" = "xyes" && test "x$enable_library" != "xyes" && test "x$with_system_libcodeagent" != "xyes" && test "x$enable_codeagentctl_builtin" != "xyes"], [
    AC_MSG_ERROR([--enable-cli with --disable-library requires --with-system-libcodeagent or --enable-codeagentctl-builtin])
])

AC_ARG_ENABLE([curl],
    [AS_HELP_STRING([--disable-curl], [compile providers without libcurl HTTP transport])],
    [],
    [enable_curl=yes])

AC_ARG_ENABLE([curses],
    [AS_HELP_STRING([--disable-curses], [compile codeagentctl without curses terminal support])],
    [],
    [enable_curses=yes])

AC_ARG_ENABLE([sanitizers],
    [AS_HELP_STRING([--enable-sanitizers], [enable AddressSanitizer and UndefinedBehaviorSanitizer with Clang/GCC])],
    [],
    [enable_sanitizers=no])

AC_ARG_ENABLE([fuzzers],
    [AS_HELP_STRING([--disable-fuzzers], [do not build lightweight fuzz smoke tests during make check])],
    [],
    [enable_fuzzers=yes])

AC_ARG_ENABLE([tests],
    [AS_HELP_STRING([--disable-tests], [do not build or run C test programs])],
    [],
    [enable_tests=yes])

CODEAGENT_WARNING_CFLAGS="-Wall -Wextra -Wpedantic"
AS_CASE([$CC],
    [*clang*|*gcc*|*cc], [],
    [CODEAGENT_WARNING_CFLAGS=""])
AC_SUBST([CODEAGENT_WARNING_CFLAGS])

CODEAGENT_SANITIZER_CFLAGS=
CODEAGENT_SANITIZER_LIBS=
AS_IF([test "x$enable_sanitizers" = "xyes"], [
    CODEAGENT_SANITIZER_CFLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
    CODEAGENT_SANITIZER_LIBS="-fsanitize=address,undefined"
])
AC_SUBST([CODEAGENT_SANITIZER_CFLAGS])
AC_SUBST([CODEAGENT_SANITIZER_LIBS])

CODEAGENT_CURL_CPPFLAGS=
AS_IF([test "x$enable_curl" = "xyes" && { test "x$enable_library" = "xyes" || test "x$enable_codeagentctl_builtin" = "xyes"; }], [
    PKG_CHECK_MODULES([CURL], [libcurl], [
        CODEAGENT_CURL_CPPFLAGS="-DCODEAGENT_HAVE_CURL=1 $CURL_CFLAGS"
    ], [
        AC_MSG_WARN([libcurl not found; provider HTTP transports will return clear runtime errors])
        CURL_CFLAGS=
        CURL_LIBS=
    ])
], [
    CURL_CFLAGS=
    CURL_LIBS=
])
AC_SUBST([CODEAGENT_CURL_CPPFLAGS])

CODEAGENTCTL_CURSES_CPPFLAGS=
CODEAGENTCTL_CURSES_LIBS=
AS_IF([test "x$enable_cli" = "xyes" && test "x$enable_curses" = "xyes"], [
    AC_CHECK_HEADERS([curses.h ncurses.h])
    AC_SEARCH_LIBS([initscr], [ncursesw ncurses curses], [
        CODEAGENTCTL_CURSES_CPPFLAGS="-DCODEAGENTCTL_HAVE_CURSES=1"
        AS_IF([test "x$ac_cv_search_initscr" = "xnone required"], [
            CODEAGENTCTL_CURSES_LIBS=
        ], [
            CODEAGENTCTL_CURSES_LIBS="$ac_cv_search_initscr"
        ])
    ], [
        AC_MSG_WARN([curses not found; codeagentctl will build without curses terminal setup])
    ])
])
AC_SUBST([CODEAGENTCTL_CURSES_CPPFLAGS])
AC_SUBST([CODEAGENTCTL_CURSES_LIBS])

AS_IF([test "x$enable_cli" = "xyes"], [
    PKG_CHECK_MODULES([UCL], [libucl])
])

AS_IF([test "x$with_system_libcodeagent" = "xyes"], [
    PKG_CHECK_MODULES([CODEAGENT_SYSTEM], [codeagent])
])

AC_PATH_PROG([CLANG_TIDY], [clang-tidy])
AC_PATH_PROG([CPPCHECK], [cppcheck])

CODEAGENT_LT_VERSION=0:0:0
AC_SUBST([CODEAGENT_LT_VERSION])

AM_CONDITIONAL([BUILD_LIBRARY], [test "x$enable_library" = "xyes"])
AM_CONDITIONAL([BUILD_CLI], [test "x$enable_cli" = "xyes"])
AM_CONDITIONAL([USE_SYSTEM_LIBCODEAGENT], [test "x$with_system_libcodeagent" = "xyes"])
AM_CONDITIONAL([CODEAGENTCTL_BUILTIN], [test "x$enable_codeagentctl_builtin" = "xyes"])
AM_CONDITIONAL([BUILD_TESTS], [test "x$enable_tests" = "xyes"])
AM_CONDITIONAL([BUILD_FUZZERS], [test "x$enable_fuzzers" = "xyes"])

AC_OUTPUT

AC_MSG_NOTICE([configuration summary:])
AC_MSG_NOTICE([  build libcodeagent:        $enable_library])
AC_MSG_NOTICE([  build codeagentctl:        $enable_cli])
AC_MSG_NOTICE([  use system libcodeagent:   $with_system_libcodeagent])
AC_MSG_NOTICE([  built-in codeagentctl:     $enable_codeagentctl_builtin])
AC_MSG_NOTICE([  libcurl transport:         $enable_curl])
AC_MSG_NOTICE([  curses frontend support:   $enable_curses])
AC_MSG_NOTICE([  sanitizers:                $enable_sanitizers])
AC_MSG_NOTICE([  tests:                     $enable_tests])
AC_MSG_NOTICE([  fuzz smoke tests:          $enable_fuzzers])
