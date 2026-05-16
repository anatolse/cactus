# Commit Message

Summarize this git diff into a conventional commit message (max 72 chars title + optional body):

Generate a concise Conventional Commit message from a git diff.

**Input**: The argument after `/commit-message` may be a pasted git diff. If no diff is provided, inspect the repository diff yourself.

**Steps**

1. **Collect the diff**
   - If the user pasted a diff, use it directly.
   - Otherwise, run `git diff --staged` first.
   - If there is no staged diff, run `git diff` and use the unstaged changes.
   - If there are no changes, tell the user there is no diff to summarize.

2. **Summarize intent**
   - Identify the primary change and impacted area.
   - Prefer the most specific Conventional Commit type:
     - `feat` for new functionality
     - `fix` for bug fixes
     - `docs` for documentation-only changes
     - `test` for test-only changes
     - `refactor` for behavior-preserving code changes
     - `perf` for performance improvements
     - `build` for build/dependency/tooling changes
     - `ci` for CI workflow changes
     - `chore` for maintenance-only changes
   - Add a scope when it is clear and useful, e.g. `feat(parser): ...`.

3. **Write the commit message**
   - Title must be at most 72 characters.
   - Use imperative mood and lowercase summary text after the type/scope.
   - Add an optional body only when it clarifies important context, motivation, or notable side effects.
   - Keep body lines wrapped at 72 characters when practical.
   - Do not include markdown fences unless the user asks for them.

**Output**

Return only the proposed commit message, with no extra commentary.