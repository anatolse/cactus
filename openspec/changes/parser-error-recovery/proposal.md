## Why

The parser currently has a critical infinite loop bug when encountering unexpected tokens. The `consume()` method reports errors but doesn't advance the token position on failure, causing ~33 parsing loops to freeze indefinitely. This leads to compiler hangs and memory exhaustion on any malformed input, making the compiler unusable for real-world development where syntax errors are common.

## What Changes

- Add panic-mode error recovery to the parser
- Implement `synchronize()` method that skips to safe boundaries (NEWLINE, DEDENT, declaration keywords)
- Update parsing loops to call synchronization on errors
- Add parser state tracking to prevent infinite loops
- Add test cases for malformed input to prevent regression

## Capabilities

### New Capabilities

_None - this change improves existing error handling without adding new features._

### Modified Capabilities

- `dsl-parser`: Add error recovery requirements - parser must guarantee forward progress and handle malformed input gracefully without hanging

## Impact

- **Files affected**: `src/frontend/parser.h`, `src/frontend/parser.cpp`
- **Behavior change**: Parser will now recover from syntax errors and continue parsing, reporting multiple errors in one pass instead of hanging
- **Testing**: Requires new test cases for error recovery scenarios
- **User experience**: Better error messages, faster failure on invalid input, ability to see multiple errors at once
