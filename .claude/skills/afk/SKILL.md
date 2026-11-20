---
name: afk
description: Works autonomously toward a broad goal for a long stretch while the user is away, making milestone commits to a feature branch and reporting back at the end. Use when the user says they are AFK, going away, or wants a long self-directed session with no check-ins.
---

# Skill: AFK

Take a broad goal, work toward it independently for a long stretch, and hand back a reviewable branch plus an honest account of what happened.

The defining constraint is **no feedback loop**. The user is not there. Every judgment call is yours, every verification is yours, and the only safety net is that the work lands on a feature branch as a series of green, individually revertable commits.

**This project drives real motors.** Unattended work is limited to what can be verified without hardware: building, host unit tests, documentation, research and planning. Anything that needs the board and a motor is listed for the user to test, not attempted.

## Invocation

```
/afk [<duration>] <goal>
```

| Form            | Example                                  | Meaning                                    |
| --------------- | ---------------------------------------- | ------------------------------------------ |
| Goal only       | `/afk Add host tests for the SDO client` | Work until the planned milestones are done |
| Duration + goal | `/afk 3h Build the parameter catalogue`  | Soft budget of three hours                 |
| Minutes         | `/afk 90m Tighten error handling`        | Soft budget of ninety minutes              |

**Parsing.** The duration is the first token only, and only when it looks like `<number><unit>` with an hours unit (`h`/`hr`/`hrs`/`hour`/`hours`) or a minutes unit (`m`/`min`/`mins`/`minute`/`minutes`). Decimals are fine (`2.5h`). Otherwise the whole argument is the goal — `/afk 5 files need work` is a goal, not a budget.

### What the duration controls

A **budget, not an alarm**. It sizes ambition and decides when to start wrapping up; it never truncates work mid-flight. Record the start with `date +%s` and re-check elapsed time at each milestone boundary.

- **Under ~1h** — one narrow, well-finished thing.
- **1–3h** — a small program of related milestones.
- **3h+** — a broad sweep; expect to survey properly and re-plan at least once.
- **No budget given** — work through the planned milestones, then stop.

Reserve the last ~15% (minimum 10 minutes) for wrap-up, and always finish the milestone in flight first. Never leave a half-done change as the final state.

Running out of valuable work before the budget is spent is a **success**. Stop and say so. Never pad the run with churn — cosmetic refactors, tests that assert nothing, or "while I'm here" edits are worse than an early finish.

## Constraints

- **Commit locally, never push.** Milestone commits stay on the local feature branch. The user reviews and pushes. This makes a wrong direction a plain branch delete.
- **Never work on `main`.** Branch off `main` into `feature/afk-<goal-slug>`.
- **No destructive git.** No `reset --hard`, `checkout --`, `clean -fd`, force-push, rebase, or branch deletion. To undo your own work, revert the commit.
- **Never ask questions mid-run.** Do not use `AskUserQuestion` — the user is away, so a question is a stall. At a genuine fork, take the lower-risk option, record the assumption, and continue; or skip the item and record why.
- **No hardware.** Do not flash the board, open its serial port, or send anything on a CAN bus. A motor may be connected and nobody is there to stop it.
- **Keep the diff scoped.** Touch only what the goal needs. Never run a repo-wide formatter or codemod over files you did not otherwise change.
- **Open questions stay open.** Do not settle anything listed in `docs/Open questions.md` by building as if it were decided. Build what is independent of it, or make the choice swappable and record the assumption.
- **Off limits without the user:** pushing, secrets and Wi-Fi credentials, ordering parts, and anything that sends messages to other people.
- **Dependencies are allowed** — add, remove, or upgrade components as the work needs, and record **every** change with its reason in the final report.
- **Stay recognisably in scope.** A goal of "add SDO tests" licenses fixing a bug the tests expose; it does not license redesigning the CANopen layer. Note out-of-scope discoveries rather than doing them.
- **Report honestly.** If something is unverified, partly done, or was skipped, say so plainly. Code that builds but has not run on the board is **untested on hardware** and is reported as such.
- **Never offer a cause you could not reproduce.** If a failure would not reproduce, say that _before_ naming a suspect. An unreproduced hypothesis is a lead, not a finding.

### Stop the whole run when

Stopping early and reporting is always better than pressing on blindly. End the run and write up what you have if:

- The working tree is dirty at the start (see Step 1).
- The only way forward is an off-limits action or hardware.
- Verification fails in a way you cannot diagnose, and the last green commit is the honest stopping point.
- Something needed is missing and cannot be obtained — a toolchain, a component, a document.
- The goal turns out to rest on a false premise. Say what you found instead of substituting a different goal.

## Workflow

### Step 1: Pre-flight

1. **Timestamp the start** — `date +%s`, and keep the value.
2. **Check the tree** — `git status` and `git branch --show-current`.
3. **Stop if dirty.** Uncommitted changes are not safe to build on unattended. Report the dirty files and stop. Do not stash.
4. **Get on a feature branch.** On `main`: `git pull --ff-only` (skip if there is no upstream), then create `feature/afk-<goal-slug>` (max 5 words, kebab-case). Already on a feature branch: use it, and say which.
5. **Read the conventions** — root `CLAUDE.md` (including its safety rules and motor references) and `docs/Decisions.md`.

### Step 2: Survey before planning

The quality of a long unattended run is decided here.

1. **Measure a baseline** with real numbers appropriate to the goal — build warnings, test counts and failures, TODO count. Include how noisy the checks already are, so "did I introduce this?" is answerable by comparing a number.
2. **Establish whether the baseline is green.** If checks already fail, say so in the report and never describe a later run as green when it inherited those failures.
3. **Find the actual gaps from that data**, ranked by consequence rather than ease. Prefer code where a defect would hurt: anything that writes to a drive, moves a motor, or decodes a value the user acts on.
4. **Check what the repo already says.** `docs/` often names known gaps and deliberate decisions. Verify before believing and before contradicting.
5. **Research current docs** for anything version-sensitive you plan to introduce or change — ESP-IDF, LVGL, components. Do not rely on memory for APIs or config.
6. **Pin down a vague goal yourself.** Pick the reading the survey supports best, state it at the start of the report, and proceed.

### Step 3: Plan milestones

1. **Create one task per milestone**, ordered by value. The task list is the user's window into progress if they check in.
2. **A good milestone** is independently valuable, independently revertable, and roughly 20–60 minutes. If it cannot be committed green on its own, split it.
3. **Size the list to the budget**, and expect to re-plan. Add newly discovered tasks rather than silently widening one milestone.

### Step 4: The loop

For each milestone, in order:

1. **Mark it `in_progress`.**
2. **Read every file before you change it.** A file that looks new often is not.
3. **Implement** to the conventions in `CLAUDE.md` and the surrounding code.
4. **Verify** with the build, format and test commands in the "Commands" section of root `CLAUDE.md`.
5. **Read the whole output, not the exit code.** Compiler warnings and deprecation notices all count.
6. **Never verify through a truncating pipe.** Do not send a check through `tail` or `head` — the summary is printed last and is always the clean part. Capture to a file and scan it.
7. **Confirm the check actually ran.** No output may mean it never started. Never report a check as passing on the strength of silence.
8. **Commit,** then confirm with `git log -1`. Subject in imperative mood matching repo style; the body explains **why** — what was wrong, what you decided and on what grounds.
9. **Mark it `completed`** and move on.

Rules across the loop:

- **Never leave the tree red between milestones.**
- **A dead end is fine.** Revert it, record it, move on.
- **Keep a running log** for the report: defects found, decisions and assumptions, dependency changes, anything skipped, everything that needs hardware testing.
- **Fix what you break, including noise.**
- **A milestone that finds nothing is a result.** "Audited X, found no defects" is worth reporting. Never manufacture a change to justify a milestone.

### Step 5: Wrap-up

Begin when the budget is ~85% spent or the milestones are done.

1. **Full verification sweep.** Read all output.
2. **Leave the tree clean.** Everything committed, or leftovers called out explicitly.
3. **Read the full branch diff yourself** before summarising, checking new code against conventions and safety rules. `/code-review` over the diff is a useful addition, never a substitute.
4. **Terminal summary** — always. What changed, grouped to skim; baseline → result with real numbers; defects fixed and deliberately left, with reasoning; areas audited that were clean; every dependency change and why; assumptions and deviations, including the goal interpretation; what was skipped; anything unverified.
5. **Hardware test guide** — always, when firmware changed: how to build and flash, which motor to connect, what to do on the screen, what should happen, and what would mean it is wrong.
6. **State the branch name and that nothing was pushed or flashed.**
7. Optionally spawn task chips for out-of-scope discoveries worth doing later.

## Judgment: decide alone vs. leave for the user

**Decide alone** — implementation approach, test design, refactors within the goal, fixing a defect the work exposes, dependency changes, file layout, which milestone is worth more.

**Leave for the user, with a written explanation** — anything in `docs/Open questions.md`; anything that changes what is written to a drive or how a motor moves; pin assignments and hardware choices; scope well past the stated goal; anything in the off-limits list.

## Anti-patterns

Observed failure modes from real unattended runs:

- **Overwriting a file that already existed** because it was written without reading first.
- **Reporting a check as clean when it never ran** — wrong working directory, no output, read as success.
- **Treating a green exit code as a clean run** and leaving warnings for the user to find.
- **Verifying through a truncating pipe** — `| tail` discarded the warnings. Reads identical to a clean run.
- **Padding the budget** with low-value changes after the real work was done.
- **Stalling on a question** the user was never going to answer, instead of choosing the safer option and recording it.
- **Reporting "works" for code that only compiles.** Without the board and a motor, it is untested on hardware.
- **A summary that reads better than the branch.** Overstating completeness destroys the trust the whole skill depends on.

## Formatting note

Steps use real ordered lists (`1.`) rather than `a.` / `b.` markers. Markdown formatters do not recognise `a.` as a list and reflow it. Keep ordered lists here if you edit this file.
