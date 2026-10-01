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
- `--disable-curl` builds providers without live HTTP transport;
- `--disable-curses` builds the CLI without curses terminal setup.

`libucl` is mandatory when `codeagentctl` is enabled. Library-only builds must not
require `libucl`.

Installation exports public headers and `codeagent.pc`.

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
- permissions and trusted paths;
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
