---
name: code-review
description: Reviews changed files (branch diff against main, or uncommitted changes when on main), fixes simple issues, asks about non-trivial ones, validates against the plan, runs checks, and summarizes findings. Use when the user wants a code review, says "review", "make a code review", or "/code-review".
---

# Skill: Code Review

Review the changed files, fix simple issues, flag non-trivial ones, validate against the plan, run the checks, and summarize.

## Constraints

- **Changed files only** — Review only what changed. Do not review unchanged files or go on tangents.
- **Fix simple issues silently** — Typos, formatting, missing whitespace, missing Doxygen, naming convention violations. Just fix them.
- **Ask about non-trivial issues** — Logic errors, missing edge cases, wrong object indices or units, timing and concurrency problems, safety concerns, or anything that needs a design decision. Present 2–3 options and **WAIT** for the user to choose.
- **Stay scoped** — No refactoring of unrelated code, no new features, no "while we're here" improvements.

## Invocation

```
/code-review [focus hint]
```

| Form        | Example                    | Meaning                              |
| ----------- | -------------------------- | ------------------------------------ |
| No argument | `/code-review`             | Review all changed files             |
| Focus hint  | `/code-review sdo client`  | Review everything, SDO client first  |

Also triggered by natural language like "review", "code review", "review my changes".

---

## Workflow

Steps run in order. Steps marked **WAIT** need user input before continuing.

### Step 1: Gather context

Run in parallel:

1. `git branch --show-current`
2. `git status --porcelain`
3. `git diff main --name-status` and `git diff main --stat`
4. `git log main..HEAD --oneline`

**Pick the review scope:**

- **On a feature branch** — everything that differs from `main`, including uncommitted changes.
- **On `main`** — uncommitted changes only (`git diff HEAD --name-status` plus untracked files from `git status`).

Look for a related plan in `docs/Plans/` (by branch name, commit messages or the user's hint) and read it if found. If there is none, skip plan validation in Step 3.

**If nothing changed**, say so and stop.

### Step 2: Review changed files

Read every changed file (not deleted ones) and check:

**Correctness**

- Logic errors, off-by-one, null pointer and lifetime risks, integer overflow and signedness (drive values are often signed 32-bit)
- Byte order: CANopen is little-endian on the wire
- Missing handling of bus failures: SDO timeout, SDO abort, no response, bus-off, unexpected frames
- Edge cases: no motor connected, motor in fault, wrong node ID or bitrate, power lost mid-operation

**Concurrency and timing**

- Data shared between tasks is protected or passed through queues
- No blocking call on the UI task (an SDO round trip must never freeze the stop button)
- No unbounded waits; every wait has a timeout
- Stack sizes and heap use are reasonable for what the task does

**Drives and safety** (from root `CLAUDE.md` and `docs/NiMotion motors.md`)

- Object indices and sub-indices are named constants and match the manual or `docs/NiMotion motors.md`
- Units are explicit in names and converted at the boundary (`currentCentiamps` → `currentA`)
- No write to `200C:01`, no NiMotion-internal save codes, no `0A`/`0B` save sub-indices
- Every write, save or factory reset is behind a confirming user action
- Every motion path has a working stop, and motor power can be cut
- Undervoltage cutoff is respected before motion

**Code quality** (from `CLAUDE.md`)

- Descriptive names, full words; booleans `is`/`has`/`should`/`can`
- Blank lines around control flow and before returns
- Class member order as in `CLAUDE.md`
- Comments explain "why" and cite the source for protocol facts; Doxygen on public APIs with every `@param` and `@return`
- Include ordering is **not** a review concern — clang-format sorts includes in Step 4

**Duplication**

- Logic that duplicates an existing module or helper
- Copy-pasted code that should be a shared function

**Completeness**

- No leftover TODOs unless agreed, no placeholder implementations
- No unused includes, dead code or leftover debug logging

#### Handling issues

- **Simple issues:** fix directly; they appear in the summary.
- **Non-trivial issues:** **WAIT.** Give the file and line, what is wrong, why it matters, and 2–3 concrete options. Apply the chosen fix, then continue.

If a focus hint was given, review that area first and in more detail.

### Step 3: Validate against the plan

If a plan was found:

- **Completeness** — every acceptance criterion has a matching implementation.
- **Scope** — no significant unplanned work.
- **Deviations** — note where the implementation differs, and why.
- **Decisions** — if the change settles or changes a design decision, check `docs/Decisions.md` was updated.

Skip silently when there is no plan.

### Step 4: Run validation

Use the build, format and test commands in the "Commands" section of root `CLAUDE.md`. At minimum:

1. Format the changed files with clang-format
2. Build the firmware
3. Run the host unit tests, if any exist

Read the whole output, not just the exit code — compiler warnings count. If a step fails, fix it when straightforward and re-run; otherwise report the failure.

Hardware testing is not part of the review. If the change needs it, list what should be tried on the board and motor.

### Step 5: Summary

```markdown
## Code Review Summary

**Scope:** branch `feature/xyz` vs main | uncommitted changes on main
**Plan:** docs/Plans/<name>.md (if found)
**Files reviewed:** N (N added, N modified, N deleted)

### Issues Found & Fixed

- [file:line] Description (category)

### Issues Found & Discussed

- [file:line] Description → resolution chosen

### Plan Validation

- ✅ All acceptance criteria met (or list gaps)
- ⚠️ Deviations: ...

### Validation Results

- Format: ✅ / ❌
- Build: ✅ / ❌ (N warnings)
- Tests: ✅ / ❌ / ⏭️ None yet

### Needs hardware testing

- What to try on the board and motor (or "nothing")

### Changes Made During Review

- Files modified by the review itself (empty if none)
```

If nothing was found, say so clearly — a clean review is a good outcome.

## Rules

- **Never commit** — the review may change files; the user decides when to commit.
- **Wait points are mandatory** — never guess the resolution of a non-trivial issue.
- **Be concise** — don't explain obvious fixes.
