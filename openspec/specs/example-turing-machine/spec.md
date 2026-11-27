# example-turing-machine

## Purpose

Let a learner edit and run a classic single-head Turing machine and inspect each transition in a standalone Cactus example.

## Requirements

### Requirement: Deterministic single-head execution

The machine SHALL read the symbol at its head, select the transition for the current state and symbol, write at that same address, move one cell left or right, enter the destination state, and count one step. Unvisited addresses on either side SHALL read blank.

#### Scenario: Trace binary carry
- **WHEN** binary increment starts on the rightmost digit of `011` at addresses 0 through 2 and the user takes three steps
- **THEN** the tape, head address and state after successive steps are `010, 1, A`, `000, 0, A`, and `100, -1, Halt`
- **AND** another step leaves the count at three and preserves the final configuration

#### Scenario: Missing transition
- **WHEN** a step has no row for the current state and symbol
- **THEN** execution pauses, identifies that pair and preserves the tape, head, state and step count

### Requirement: Visible tape and program

The example SHALL display cell addresses and symbols, one distinct head marker, current state, step count, run status and the program table. It SHALL identify the next applicable row before each step and keep the head visible during execution.

#### Scenario: Leave and revisit a written cell
- **WHEN** the machine writes a symbol, moves beyond the visible cells and returns
- **THEN** the marker tracks the head across the window and the original address still displays the written symbol

### Requirement: Manual and timed controls

The example SHALL start paused and provide Step, Run, Pause, Reset and a speed in transitions per second. Both modes SHALL execute the same transitions. Reset SHALL restore the edited seed tape, starting head, initial state and zero count while retaining the current program.

#### Scenario: Change speed and pause
- **WHEN** a nonhalting machine runs for one second at 2 steps/s and one second at 4 steps/s starting at a step boundary
- **THEN** its configuration equals six manual steps from the same seed
- **AND** time spent paused adds no steps or backlog on resume

### Requirement: In-window editor

While paused the user SHALL be able to create a program, add, edit and remove transitions, set the seed symbols and choose the starting head address. A row SHALL specify source state, read symbol, written symbol, direction and destination state including Halt. The alphabet SHALL include zero, one and blank. Each source state and symbol SHALL identify at most one executable row.

#### Scenario: Repair a program
- **WHEN** the user adds a missing transition on pause and presses Step
- **THEN** the machine executes the edited transition from the current configuration
- **AND** the editor prevents two executable rows for the same key

#### Scenario: Reset to a custom seed
- **WHEN** the user edits the seed and start position, runs the machine and resets
- **THEN** the displayed configuration returns to that seed and position in the initial state with zero steps

### Requirement: Edit the displayed transition

The editor SHALL show one row per read symbol on the displayed state page. A row SHALL show the instruction for (displayed state, its symbol) as a relation row from the row to that instruction, or no relation row when there is none. Its five buttons SHALL name the row with a required link and SHALL read the instruction only through the row. No button SHALL store an instruction handle or a flag saying whether one is shown.

#### Scenario: Edit after the instruction is removed

- **WHEN** a click on a row's Write button carries an instruction that is destroyed before the edit is delivered
- **THEN** the edit has no effect

#### Scenario: Add on an empty row

- **WHEN** Add is clicked on a row that shows no instruction
- **THEN** one instruction for the displayed state and the row's symbol is created
- **AND** on the next frame the row shows it and Add is no longer offered

### Requirement: One instruction per key

Every preset and every completed edit SHALL leave at most one instruction for each (state, symbol) key.

#### Scenario: Key invariant after edits and presets

- **WHEN** a test adds, removes and re-adds an instruction, pages between states and loads every preset in consecutive frames
- **THEN** after each frame every key has at most one instruction

#### Scenario: Refresh after an edit or preset load

- **WHEN** an edit completes or a preset replaces the program
- **THEN** on the next frame every row button's caption and the next-instruction highlight match the current table

### Requirement: Four editable presets

The example SHALL supply unary increment, bit inversion, binary increment and parity as editable transition tables with sample input and starting head. Selecting a preset SHALL load its table and seed and pause at step zero.

#### Scenario: Run the supplied programs
- **WHEN** the user loads each preset and runs it to Halt
- **THEN** unary increment maps `111` to `1111`, inversion maps `10110` to `01001`, binary increment maps `011` to `100`, and parity maps `1011` to `10111`
- **AND** changing the parity input to `1010` produces `10100`

### Requirement: Release compiler compatibility

The runnable example SHALL implement the interpreter, editor, playback and presentation in Cactus using the project's existing Release executable and local standard library.

#### Scenario: Build the standalone example
- **WHEN** the example is generated with `build-clang-o3-release/cactus.exe` and the project's `stdlib/` and linked with its existing runtime
- **THEN** the executable opens the simulator and supports the same controls and programs without example-specific native handlers
