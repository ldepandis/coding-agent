# Software Specification

This is the authoritative specification for the C-only `codeagent` repository.

## Workspace

The project contains:

- `libcodeagent`, a reusable C99 library;
- `codeagentctl`, a terminal frontend;
- built-in provider modules compiled into the library;
- Library tests under `libcodeagent/tests/`.
- CLI tests under `codeagentctl/tests/`.

There is no Rust workspace and no CMake build.

## Build

The only supported build system is GNU Autotools:

```sh
./autogen.sh
./configure
make
make check
make install
```

Important configure options:

- `--disable-cli` builds only `libcodeagent`;
- `--disable-library --with-system-libcodeagent` builds `codeagentctl` against an installed `libcodeagent`;
- `--enable-shared --disable-static` builds only the dynamic `libcodeagent`;
- `--enable-static --disable-shared` builds only the static `libcodeagent`;
- `--enable-codeagentctl-builtin` compiles `libcodeagent` sources directly into `codeagentctl`;
- `--disable-library --enable-codeagentctl-builtin` builds only the built-in CLI binary without producing a separate library;
- `--enable-sanitizers` enables ASan/UBSan where supported;
- `--disable-hardening` disables compiler/linker hardening probes;
- `--disable-curl` builds providers without live HTTP transport.

`libucl` and curses/ncurses are mandatory when `codeagentctl` is enabled. Library-only
builds must not require them.

Installation exports public headers and `codeagent.pc`.

Compiler hardening is enabled by default. The configure script probes support before
using hardening flags such as stack protector, `_FORTIFY_SOURCE`, strict-overflow
avoidance, and ELF RELRO/NOW linker flags where the platform accepts them.

## Public API

The only installable header is `libcodeagent/include/codeagent/codeagent.h`.

The API covers:

- API/ABI version checks: `ca_version()`, `ca_version_string()`, `ca_abi_version()`;
- content blocks and messages;
- pure state-machine lifecycle and event handling;
- library-owned conversation loop through `ca_agent`;
- provider selection and provider discovery by metadata;
- runtime config and provider key/value options;
- built-in tools and external tool registration through `ca_tool_registry`;
- sessions, markdown serialization, filesystem session manager, and `ca_storage_adapter`;
- command discovery;
- token and cost estimates;
- permissions, trusted paths, and sandbox policy;
- Git workflow helpers;
- Obsidian note helpers;
- in-memory multi-agent progress tracking;
- diagnostics and conservative safe auto-fix helpers.

Every public function must document memory ownership and lifetime in the header.

## Ownership

- Borrowed strings are returned as `const char *` and must not be freed.
- Owned strings are returned as `char *` and must be released with `ca_free()` unless a
  more specific free function is documented.
- Opaque handles allocated by `*_new()` are released with the matching `*_free()`.
- List APIs have explicit list free functions.

## Providers

Provider internals are private in `libcodeagent/src/provider.h`.

Built-in provider names:

- `anthropic`
- `openai`
- `ollama-cloud`
- `ollama-server`
- `openai-compatible`

Providers are compiled into `libcodeagent`; runtime loading of external provider code is
not supported.

Providers receive registered tool definitions and normalize provider responses into
`ca_content_block` values. HTTP providers honor timeout/retry settings and surface HTTP
status/body diagnostics where available.

## Conversation Loop

`ca_agent_submit()` owns the synchronous conversation loop:

1. submit user input to the state machine;
2. dispatch provider requests;
3. execute requested tool batches;
4. retry provider/tool failures according to config;
5. run post-tool hooks;
6. warn at context and tool-iteration thresholds;
7. return when the turn is complete or an unrecoverable error occurs.

`codeagentctl` is only the frontend. It loads client config, renders terminal output, and
routes slash commands.

## Sandbox

`libcodeagent` exposes sandbox policy through `ca_config` and enforces it inside built-in
tools. Frontends choose the mode; the library performs the checks.

Supported modes:

- `disabled`: preserves legacy behavior and performs no sandbox checks;
- `read_only`: permits reads inside the workspace or trusted paths and denies writes;
- `workspace_write`: permits reads and writes inside the workspace or trusted paths;
- `full_access`: explicitly disables sandbox path restrictions.

Workspace paths are relative paths that do not escape through `..`. Absolute paths must
match `trusted_paths` exactly or as path-boundary children. Built-in `read_file`,
`write_file`, `edit_file`, `list_files`, and `code_search` apply these path checks before
touching the filesystem. Sandbox checks canonicalize existing paths, canonicalize parent
directories for new writes, reject symlink escapes that resolve outside the allowed
roots, and reject regular-file hardlinks because they can alias data outside the
sandbox.

Native child-process backends are used where the target operating system provides a
usable per-process primitive:

- OpenBSD: `unveil()` plus `pledge()`;
- Linux: Landlock filesystem rules, with `PR_SET_NO_NEW_PRIVS`;
- macOS: Seatbelt profiles through `sandbox_init()`;
- FreeBSD: Capsicum capability mode.

NetBSD and DragonFly BSD currently use the custom path-policy fallback because they do
not expose an equivalent process sandbox backend through a small portable C API here.
On platforms without a native process sandbox, the custom sandbox still enforces path
policy and rejects `bash` execution when sandboxing is active.

## Threading

`libcodeagent` does not create worker threads. Separate object instances may be used on
separate threads. Individual mutable handles such as `ca_agent`, `ca_state_machine`,
`ca_session`, and `ca_agent_manager` are not internally synchronized.

## Validation

Required local validation:

```sh
make check
make codeagent-clang-tidy
make codeagent-cppcheck
```

The static-analysis targets may return status `77` when the corresponding tool is not
installed.

Security validation must include red-team style attempts against the local code surface,
including sandbox escapes, parser confusion, command injection, path traversal,
symlink/hardlink aliasing, untrusted config input, oversized inputs, and compiler/linker
hardening regressions.
