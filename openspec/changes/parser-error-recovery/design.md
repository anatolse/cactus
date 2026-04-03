## Context

The Cactus parser currently uses a recursive descent approach with ~33 parsing loops that iterate through token sequences. The `consume()` method is called throughout to validate expected tokens. When `consume()` encounters an unexpected token, it:
1. Reports the error to ErrorReporter
2. Returns the current token WITHOUT advancing
3. Continues execution

This creates a critical bug: parsing loops that call `consume()` can get stuck on the same token forever if it's unexpected, leading to infinite loops and memory exhaustion.

The parser needs industry-standard error recovery (panic mode) to:
- Guarantee forward progress through the token stream
- Recover to sensible boundaries after errors
- Continue parsing to find additional errors in one pass

## Goals / Non-Goals

**Goals:**
- Prevent all infinite loops in parsing
- Implement panic-mode synchronization to recover from errors
- Add synchronization points at natural boundaries (NEWLINE, DEDENT, keywords)
- Enable multi-error reporting (continue after first error to find more)
- Maintain existing error message quality

**Non-Goals:**
- Advanced error recovery (e.g., error productions, automatic semicolon insertion)
- Changing the parser architecture or AST structure
- Improving error message content (only fixing the hang bug)
- Handling lexer errors (lexer already doesn't hang)

## Decisions

### Decision 1: Panic Mode Recovery over Loop Guards

**Choice:** Implement classic panic-mode error recovery with a `synchronize()` method.

**Rationale:**
- Industry standard approach used in compilers (C++, Java, Rust parsers)
- Single centralized recovery logic instead of duplicated guards in 33 loops
- Better error recovery quality - skips to sensible boundaries
- More maintainable - new parsing loops automatically benefit

**Alternatives considered:**
- Loop guards: Would require checking `current_` position in every loop - verbose and error-prone
- Forced advance in `consume()`: Could skip critical tokens, harder to reason about
- Exception-based recovery: C++ exceptions have performance overhead, parser is performance-sensitive

### Decision 2: Synchronization Boundaries

**Choice:** Synchronize on statement/declaration boundaries:
- `NEWLINE` - statement separator in Cactus
- `DEDENT` - block end marker
- `EOF_TOKEN` - file end
- Declaration keywords: `TRAIT`, `SYSTEM`, `FUNC`, `STRUCT`, `ENUM`, `EVENT`, `UNIT`, `TEMPLATE`, `VIEW`, `INTERFACE`, `ASSET`, `INPUT`, `MODULE`, `USE`, `CONST`

**Rationale:**
- These are natural recovery points in the language grammar
- Declarations are top-level constructs - safe to resume parsing
- NEWLINE/DEDENT align with Python-style indentation-based syntax
- Matches what a human would do when manually fixing syntax errors

**Alternatives considered:**
- Only synchronizing on keywords: Misses nested block recovery
- Only synchronizing on NEWLINE: Too aggressive, could skip entire declarations
- Synchronizing on all punctuation: Too fine-grained, may not recover enough

### Decision 3: Synchronization Placement

**Choice:** Add explicit `synchronize()` calls after each parsing loop that calls error-reporting functions.

**Rationale:**
- Explicit control over when to synchronize
- Can be selective based on error context
- Easier to understand and debug than automatic synchronization

Pattern:
```cpp
while (!check(DEDENT) && !check(EOF_TOKEN)) {
    skip_newlines();
    auto item = parse_something();
    if (errors_.has_errors()) {  // Check if parsing failed
        synchronize();  // Skip to safe boundary
        continue;  // Try next item
    }
    expect_newline();
}
```

**Alternatives considered:**
- Automatic synchronization in `consume()`: Could synchronize too aggressively
- Exception-based unwinding: Performance overhead + C++ complexity
- Error flags in Parser class: Requires threading state through all methods

### Decision 4: Error Tracking

**Choice:** No additional error tracking beyond what ErrorReporter already provides. Rely on ErrorReporter's error count to detect when synchronization is needed.

**Rationale:**
- ErrorReporter already tracks errors
- Avoids duplicated state
- Parser stays stateless regarding errors

## Risks / Trade-offs

**Risk:** Synchronization may skip too much code, hiding cascading errors
→ **Mitigation:** Carefully chosen synchronization boundaries minimize over-skipping. Test with common error patterns.

**Risk:** Performance impact from error checking in hot loops
→ **Mitigation:** Error checking is only active after actual errors occur. Happy path has minimal overhead. Consider adding error count caching if needed.

**Risk:** May recover incorrectly and produce confusing error messages
→ **Mitigation:** Start conservative - only synchronize after clear errors. Can refine boundaries based on real-world usage.

**Trade-off:** More code complexity in parsing loops
→ **Benefit:** Prevents infinite loops, improves developer experience dramatically. One-time complexity cost for ongoing reliability.

**Trade-off:** Error messages may be less precise (reporting error location before synchronization point)
→ **Benefit:** Users see multiple errors per compilation, faster overall development iteration.

## Migration Plan

**Phase 1: Core Implementation**
1. Add `synchronize()` method to Parser class
2. Add `is_synchronization_point()` helper method
3. Update critical parsing loops (start with the most common: struct/trait/system/unit parsing)

**Phase 2: Testing**
1. Create test files with intentional syntax errors
2. Verify parser doesn't hang (add timeout to tests)
3. Verify error messages are still clear
4. Check for no regressions in valid file parsing

**Phase 3: Rollout**
1. Deploy with integration tests
2. Monitor for any new parsing failures
3. Refine synchronization boundaries based on user feedback

**Rollback:** If issues arise, can disable synchronization by making `synchronize()` a no-op while investigating. Core parser logic unchanged.
