# codeagent

`codeagent` is an AI coding-agent toolkit designed to be embedded, packaged, and
used from a terminal without depending on a language-specific runtime. The project
is split into:

- `libcodeagent`: reusable C99 AI coding-agent library;
- `codeagentctl`: terminal frontend built on top of the public library API.

The codebase started as a Rust workshop prototype focused on a testable,
event-driven state machine for AI tool use. It was later rewritten as a portable C
implementation so the core agent loop, provider dispatch, tools, sessions,
permissions, token accounting, and workflow helpers could live in an installable
library. `codeagentctl` is intentionally only a client of that API: it owns terminal
UX and local configuration, while `libcodeagent` remains reusable by other
applications.

The project uses GNU Autotools as its only supported build system.

## Layout

```text
libcodeagent/include/codeagent/      Public installable C API
libcodeagent/src/                    Library implementation and private helpers
libcodeagent/providers/              Built-in provider modules compiled into libcodeagent
codeagentctl/src/                    codeagentctl frontend
libcodeagent/tests/                  Library unit, integration, and fuzz-smoke tests
codeagentctl/tests/                  CLI config and snapshot tests
docs/                     Rebuild-level architecture documentation
specs/                    Historical product notes, not build inputs
```

## Dependencies

Required:

- C99 compiler such as `clang` or `gcc`;
- GNU Autotools: `autoconf`, `automake`, `libtool`;
- `pkg-config`;
- `libucl` when building `codeagentctl`.

Optional:

- `libcurl` for live provider HTTP transports;
- curses/ncurses for terminal setup;
- `clang-tidy` and `cppcheck` for quality checks;
- `ripgrep` for the built-in `code_search` tool.

## Build

From a source checkout:

```sh
./autogen.sh
./configure
make
make check
```

Useful configure variants:

```sh
./configure --disable-cli
./configure --disable-library --with-system-libcodeagent
./configure --enable-shared --disable-static
./configure --enable-static --disable-shared
./configure --enable-codeagentctl-builtin
./configure --disable-library --enable-codeagentctl-builtin
./configure --enable-sanitizers
./configure --disable-curl
./configure --disable-curses
```

Install:

```sh
make install
```

Installation provides the library, `codeagentctl`, public headers, and `codeagent.pc`
for `pkg-config`.

## Run

```sh
export ANTHROPIC_API_KEY="..."
./codeagentctl/codeagentctl
```

Other built-in provider names are:

- `anthropic`
- `openai`
- `ollama-cloud`
- `ollama-server`
- `openai-compatible`

Use `codeagentctl --providers` to inspect provider availability.

## Public API

The only public header is:

```c
#include <codeagent/codeagent.h>
```

The library owns:

- state machine and conversation loop;
- provider dispatch;
- built-in tools;
- external tool registration through `ca_tool_registry`;
- config structs and provider options;
- session, storage adapter, token/cost, permissions, Git, Obsidian, multi-agent status, and auto-fix helper APIs.

Provider internals remain private in `libcodeagent/src/provider.h`. Provider modules are compiled
into `libcodeagent`; runtime loading of `.so`/`.dylib` provider code is intentionally not
supported.

Runtime API/ABI checks are available through `ca_version()`, `ca_version_string()`, and
`ca_abi_version()`.

## Quality

```sh
make check
make codeagent-clang-tidy
make codeagent-cppcheck
```

`clang-tidy` and `cppcheck` targets exit with status `77` when the tool is not installed.

## Documentation

- [Software Specification](docs/SOFTWARE_SPEC.md)
- [CLI Architecture](docs/CLI_ARCHITECTURE.md)
- [State Machine](docs/STATE_MACHINE.md)
- [Agent Instructions](CODEX.md)

## License

ISC license. See `LICENSE`.
