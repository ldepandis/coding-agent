# CLI Architecture

`codeagentctl` is the C terminal frontend for `libcodeagent`.

## Boundaries

`libcodeagent` owns:

- state machine and conversation loop;
- provider dispatch;
- tools and external tool registry;
- session objects and storage adapters;
- permissions, token/cost, Git, Obsidian, diagnostics, and multi-agent state.

`codeagentctl` owns:

- command-line parsing;
- loading `~/.config/coding-agent/config.ucl` with `libucl`;
- startup screen and terminal I/O;
- slash-command routing;
- display formatting for messages, tool progress, warnings, and errors.

The library must never parse the client config file. It receives populated `ca_config`
values from the frontend.

## Config

`codeagentctl` uses `codeagentctl/src/config.c` to load UCL config and populate `ca_config`.
`libucl` is mandatory for CLI builds.

## Slash Commands

The CLI supports:

- `/help`
- `/cancel`
- `/clear`
- `/commit`
- `/commit --pick`
- `/config`
- `/context`
- `/cost`
- `/diff`
- `/document`
- `/exit`, `/quit`, `/q`
- `/history`
- `/land`
- `/model`
- `/results`
- `/spec`
- `/status`
- `/undo`

Command metadata available to embedders lives in the public command discovery API.
Command implementations remain frontend code.

## UI

The CLI provides:

- startup screen;
- multi-line input;
- Ctrl+C/Ctrl+D handling;
- markdown-style terminal rendering;
- context/status bars;
- progress and tool result formatting;
- session resume/history display.

## Quality

CLI behavior is covered by C tests in `codeagentctl/tests/`, including config and snapshot-oriented
tests.

Use:

```sh
make check
```
