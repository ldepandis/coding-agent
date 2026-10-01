# State Machine

The C state machine lives in `libcodeagent/src/state_machine.c` and is exposed through
`libcodeagent/include/codeagent/codeagent.h`.

## Contract

The state machine is deterministic and performs no I/O. Callers feed events into
`ca_state_machine_handle()` and receive one `ca_agent_action` describing the side effect
to perform.

## States

- `CA_STATE_WAITING_FOR_USER_INPUT`
- `CA_STATE_CALLING_LLM`
- `CA_STATE_PROCESSING_LLM_RESPONSE`
- `CA_STATE_EXECUTING_TOOLS`
- `CA_STATE_POST_TOOLS_HOOK`
- `CA_STATE_ERROR`
- `CA_STATE_SHUTTING_DOWN`

## Events

- `CA_EVENT_USER_INPUT`
- `CA_EVENT_LLM_COMPLETED`
- `CA_EVENT_LLM_ERROR`
- `CA_EVENT_TOOL_COMPLETED`
- `CA_EVENT_HOOKS_COMPLETED`
- `CA_EVENT_RETRY_TIMEOUT`
- `CA_EVENT_SHUTDOWN_REQUESTED`

## Actions

- `CA_ACTION_SEND_LLM_REQUEST`
- `CA_ACTION_EXECUTE_TOOLS`
- `CA_ACTION_RUN_POST_TOOLS_HOOKS`
- `CA_ACTION_DISPLAY_TEXT`
- `CA_ACTION_DISPLAY_ERROR`
- `CA_ACTION_DISPLAY_WARNING`
- `CA_ACTION_PROMPT_FOR_INPUT`
- `CA_ACTION_SCHEDULE_RETRY`
- `CA_ACTION_WAIT_FOR_EVENT`
- `CA_ACTION_SHUTDOWN`

## Ownership

Actions returned from `ca_state_machine_handle()` own nested memory and must be released
with `ca_agent_action_free()`.

Borrowed state returned by `ca_state_machine_state()` is valid only until the machine is
mutated or freed.

## Tests

State-machine behavior is covered by `libcodeagent/tests/test_main.c`. Every transition change must
add or update C tests.
