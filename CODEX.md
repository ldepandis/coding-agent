# CODEX.md

Operational context for agents working on this repository.

## Project Shape

This is a C-only codebase.

- `libcodeagent/include/codeagent/codeagent.h` is the only public installable API.
- `libcodeagent/src/` contains `libcodeagent` implementation and private helpers.
- `libcodeagent/src/provider.h` is private provider infrastructure.
- `libcodeagent/providers/` contains built-in providers compiled into `libcodeagent`.
- `codeagentctl/src/` contains `codeagentctl`.
- `libcodeagent/tests/` contains library tests.
- `codeagentctl/tests/` contains CLI tests.

There is no Rust workspace and no CMake build path.

## Build

Use GNU Autotools:

```sh
./autogen.sh
./configure
make
make check
```

Useful variants:

```sh
./configure --disable-cli
./configure --disable-library --with-system-libcodeagent
./configure --enable-shared --disable-static
./configure --enable-static --disable-shared
./configure --enable-codeagentctl-builtin
./configure --disable-library --enable-codeagentctl-builtin
./configure --enable-sanitizers
make codeagent-clang-tidy
make codeagent-cppcheck
```

`libucl` is mandatory for `codeagentctl`; library-only builds must not require it.

## Architecture Rules

`libcodeagent` owns:

- state machine and conversation loop;
- provider dispatch;
- built-in tools and external tool registry;
- runtime config structs;
- sessions and storage adapters;
- token/cost, permissions, Git, Obsidian, multi-agent status, diagnostics, and safe auto-fix helpers.

`codeagentctl` owns:

- command-line parsing;
- client config loading from `~/.config/coding-agent/config.ucl`;
- terminal input/output;
- slash-command routing and user-facing formatting.

Providers are selected by name through public config/API and implemented behind private
interfaces. Do not expose provider vtables, transport structs, or provider-specific
functions in `codeagent.h`.

Provider modules are compiled into `libcodeagent`; do not add runtime `.so`/`.dylib`
loading.

## Public API

Keep `libcodeagent/include/codeagent/codeagent.h` stable and documented. Every public function must
state ownership/lifetime rules in the adjacent comment.

Use:

- `ca_version()`, `ca_version_string()`, and `ca_abi_version()` for runtime compatibility;
- `ca_tool_registry` for frontend-defined tools;
- `ca_storage_adapter` for non-filesystem session stores;
- `ca_config_set_provider_option()` / `ca_config_provider_option()` for provider-specific settings.

## Quality

For C changes, run:

```sh
make check
make codeagent-clang-tidy
make codeagent-cppcheck
```

If Autotools is unavailable in the local environment, state that clearly and run any
available compiler/test path only as a temporary local validation.
