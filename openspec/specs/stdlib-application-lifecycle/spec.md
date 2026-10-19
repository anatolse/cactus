# stdlib-application-lifecycle Specification

## Purpose
Let Cactus application logic request an orderly shutdown through the same event model used for other gameplay and UI actions.

## Requirements
### Requirement: Quit is a standard application event
std.app SHALL expose QuitRequested. A request SHALL set an observable runtime quit state after the current activation commits. Repeated requests SHALL coalesce. Requesting quit SHALL not directly terminate the process or interrupt a handler.

#### Scenario: UI requests exit
- **WHEN** a button handler emits QuitRequested
- **THEN** the handler and its activation finish normally and the host can observe that quit was requested

### Requirement: Generated and custom hosts control shutdown
The generated entry point SHALL honor quit state after finishing the current frame, deliver unload exactly once, and release runtime/graphics resources. Custom hosts and headless tests SHALL be able to observe/reset quit state without terminating their process. Resetting a project SHALL clear previous quit state.

#### Scenario: Repeated exit clicks
- **WHEN** several QuitRequested events arrive before shutdown
- **THEN** the generated entry point performs one orderly unload and shutdown

#### Scenario: Headless lifecycle test
- **WHEN** a headless host runs a frame that requests quit
- **THEN** the host remains alive and can assert the quit state and reset it
