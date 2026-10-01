# AGENTS.md

Guidelines for AI agents working on this C-only codebase.

## Project Context

`codeagent` provides:

- `libcodeagent`, a reusable C99 AI coding-agent library;
- `codeagentctl`, a terminal frontend.

GNU Autotools is the only supported build system. CMake and Rust have been removed.

## Key Files

1. `libcodeagent/include/codeagent/codeagent.h` - public API and ownership contracts
2. `libcodeagent/src/state_machine.c` - pure state-machine transitions
3. `libcodeagent/src/agent.c` - library-owned conversation loop
4. `libcodeagent/src/tools.c` - built-in tools and external tool registry
5. `libcodeagent/src/provider.h` - private provider interface
6. `libcodeagent/providers/` - built-in providers
7. `codeagentctl/src/` - codeagentctl frontend
8. `libcodeagent/tests/test_main.c` - main C behavior tests

## Build And Test

```sh
./autogen.sh
./configure
make
make check
```

Quality targets:

```sh
make codeagent-clang-tidy
make codeagent-cppcheck
```

## State Machine

When modifying `libcodeagent/src/state_machine.c`:

- keep transitions deterministic and free of I/O;
- return actions for callers to execute;
- add or update C tests for every changed transition;
- update `docs/STATE_MACHINE.md` when states, events, or actions change.

## Public API

When changing `libcodeagent/include/codeagent/codeagent.h`:

- document ownership/lifetime next to every public function;
- keep provider internals private;
- prefer opaque handles for mutable subsystems;
- preserve API/ABI versioning through `ca_version()` and `ca_abi_version()`.

## Providers

Provider modules are compiled into `libcodeagent`. Do not add runtime plugin loading.
Provider-specific options must use the generic config option API.

## Tools

Built-in tools live in `libcodeagent/src/tools.c`. External tools are registered with
`ca_tool_registry`. Tool output strings are allocated by the library and freed by callers
with `ca_free()` unless the specific function documents otherwise.
