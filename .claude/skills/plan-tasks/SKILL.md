---
name: plan-tasks
description: Plans a feature in two separate passes — first a short decision brief for the user to review, then a detailed working plan for the implementer. Produces a markdown plan file in docs/Plans/. Use when the user wants to plan work, create a feature plan, or break a feature or milestone into tasks.
---

# Skill: Plan Tasks

Plan a feature in **two separate invocations** with two different audiences.

| Pass             | Written for           | Optimised for | Length                                 |
| ---------------- | --------------------- | ------------- | -------------------------------------- |
| **Brief**        | The user              | Judgement     | ~150 lines is comfortable, not a limit |
| **Working plan** | The implementer (you) | Execution     | Unconstrained                          |

The brief is the only thing the user is expected to read carefully. The working plan is a thinking and tracking artifact — the user gets a short delta, not the body.

## Invocation

```
/plan-tasks <feature name>            → writes the brief, then stops
/plan-tasks expand <feature name>     → expands the approved brief into the working plan
```

Both write `docs/Plans/<Sentence case name>.md`. The working plan is appended below the brief in the same file, so decisions sit next to the tasks that implement them. Add new plans to the "Plans" section of `docs/README.md`.

**Never run both passes in one invocation.** The user approves the brief in between. If asked to "plan X", write the brief and stop, even if it feels obvious.

---

## Pass 1 — The brief

1. **Require a discussion first.** Briefs follow a conversation, they do not replace one. If the feature has not been discussed and obvious questions are unresolved, ask them before writing.
2. **Discover docs** from `docs/README.md`. Always read `docs/Overview.md`, `docs/Milestones.md`, `docs/Architecture.md`, `docs/Decisions.md` and `docs/Open questions.md`; pick the rest by judgement (`docs/NiMotion motors.md` for anything that talks to a drive, `docs/Hardware.md` for anything touching pins, power or wiring).
3. **Read the code the feature touches** — existing modules and patterns. A brief that contradicts the codebase is worse than no brief.
4. **Read `docs/NiMotion motors.md`** for anything that talks to a drive, and check both the English and Chinese manuals before planning any write to a drive (the English one is wrong in places).
5. **Research external docs** for any library, chip or ESP-IDF component central to the feature, via Context7 (`resolve-library-id` → `query-docs`) or web search. Verify against the version we use — ESP-IDF APIs change between releases. Gotchas often become decisions.
6. **Check `docs/Plans/`** for overlap or contradiction with existing plans.
7. **Write the brief**, then **run the critique pass**, then present and stop.

### Brief template

```markdown
# Plan: <Name>

**Brief status:** Draft
**Created:** YYYY-MM-DD

## What we are building

Two or three sentences. What it does, for whom, which use case (UC-1..UC-9) and milestone it serves.

## Scope

**In**

- ...

**Out**

- ... (give a reason for anything a reasonable person would expect to be in)

## Decisions

### D1 — <short title>

One or two sentences framing the fork.

- **Option A (recommended)** — ...
- **Option B** — ...

**If B instead:** what materially changes — work, behaviour, safety, hardware or timeline.

## Milestones

### M1 — <outcome-shaped title>

One line on what it delivers.
**Testable when done:** what the user can actually try on the board and a motor.

## Open questions

- **Q1** — the question, and what it blocks or shapes.

## Risks

- ...
```

### What counts as a decision

This filter is the whole point of the brief. Get it wrong and the brief becomes a summary, which is useless.

**Surface it when** a different choice means materially different work, it is expensive to reverse (hardware, wiring, anything written to a drive), it changes user-visible behaviour or safety, or it depends on context you do not have.

**Do not surface** library choices with an obvious default, naming, anything cheap to correct while building, or patterns the codebase or `docs/Decisions.md` already establish.

**Always give a recommendation.** A menu without one pushes the work back onto the user. State the fork, pick a side, say why, say what changes if overruled.

**Design reasoning belongs here, not in technical notes.** If you work out that approach A beats B for a non-obvious reason, that is a decision.

**When the user approves the brief,** decisions that shape the project beyond this feature also go into `docs/Decisions.md`.

### Length and shape

- **~150 lines is a comfortable goal, not a limit.** Do not pad to reach it and do not compress substance to stay under it.
- **The real constraint is signal per line.** Cut anything that restates what the reader knows, justifies a point already made, or narrates instead of deciding.
- **3–10 decisions.** Fewer usually means the filter is too strict or the feature is trivial; more means the feature should be split.
- **No task detail** — no acceptance criteria, testing requirements or technical notes.

---

## Pass 2 — The working plan

Only run when the brief's status line reads `Approved`. If it says `Draft`, say so and stop.

1. **Re-read the approved brief.** Decisions are binding. If expansion shows one was wrong, raise it rather than silently changing it.
2. **Expand every milestone in one pass** — thinking it through end to end is what catches incoherence.
3. **Write tasks** under a `## Working plan` heading below the brief.
4. **Run the critique pass.**
5. **Present a delta, never the body** — task count, anything that contradicts or extends the brief, any new open questions. If nothing surprising came up, say so and stop.

### Task template

```markdown
### PREFIX-NNN: Short imperative title

**Milestone:** M1
**Status:** Not Started
**Blocked by:** PREFIX-001 (or "None")
**Implements:** D3, D7 (or "None")

#### Summary

What this delivers and why.

#### Acceptance criteria

- [ ] Checklist describing the done state

#### Testing requirements

- [ ] Host unit tests (protocol and parsing logic)
- [ ] On hardware: what to run on the board, with which motor, and what to observe

#### Technical notes

Which APIs, which existing patterns, which motor references, gotchas found during research. No large code blocks, no step-by-step instructions.
```

One short prefix per plan (e.g. `SDO` for the SDO client), numbered sequentially. Milestone is a field rather than a nesting level, so tasks move between milestones without renumbering.

Also include a standing `## Open questions` section that stays live during implementation, and a one-line caveat that detail for later milestones is speculative and will be revised once earlier ones are built.

**When a decision changes later**, regenerate only the tasks whose `Implements:` cites it. Do not rewrite the plan.

---

## The critique pass

Runs automatically at the end of **both** passes, before anything is presented. Read the document as though someone else wrote it and you have been asked to find what is wrong with it.

**Always ask:**

- What would someone who disagrees attack first?
- What did I assume without stating it — about the board, the drive, the wiring?
- What is the simplest version still worth building, and why is the plan not that? This is a single-person tool.
- What have I contradicted — in the codebase, `docs/Decisions.md`, an earlier plan, or `docs/NiMotion motors.md`?
- Does anything depend on an item in `docs/Open questions.md` as if it were settled?

**On a brief, also ask:**

- Does every decision have a real alternative someone might genuinely pick? Delete the ones that do not.
- Is a real decision sitting in prose as an assumption?
- Is a decision actually downstream of an unanswered open question? Then it is not decidable yet — move it.
- Does each milestone deliver something testable on hardware, or is it a horizontal layer wearing an outcome's name?
- Is each recommendation the best option, or the first one I thought of?

**On a working plan, also ask:**

- Does every decision have at least one task implementing it?
- Does every task trace to a decision or a scope item? If not, why is it here?
- Can each acceptance criterion actually be checked, or is it an aspiration?
- **Safety:** does every task that moves a motor or writes to a drive respect the safety rules in `CLAUDE.md` — stop control, confirmed writes, forbidden objects, undervoltage cutoff?
- Which real ordering dependency is missing from `Blocked by`?
- Is the first milestone testable end to end, or does it need the second one to be worth anything?
- Which failure paths are missing — no motor on the bus, SDO timeout or abort, bus-off, drive in fault, power lost mid-write, low pack voltage?

**Apply the fixes rather than listing concerns.** Then report in one or two lines what the critique changed. If it changed nothing, say that too — usually a sign the pass was not adversarial enough.

---

## Task sizing

A task delivers a **coherent, testable outcome** — one concern end to end, typically 1–5 files.

**Merge** when a task is only meaningful combined with the next: config and scaffold steps, sequential setup, anything whose criteria would be met in a few lines.

**Split** when criteria cover two independently testable outcomes, or the task spans unrelated domains.

**Spikes are tasks too.** When feasibility is unknown (a drive behaviour, a peripheral, a component), the first task is a minimal experiment on the hardware whose acceptance criterion is a recorded answer in the docs, not production code.

## UI tasks must be testable on the device

Never produce screens or widgets that render nowhere. Every UI task ends with the screen reachable and usable on the board's display. Include the wiring to real data where it exists, prefer fewer screens fully wired, and split vertically rather than horizontally.

| Avoid                                              | Prefer                                                     |
| -------------------------------------------------- | ---------------------------------------------------------- |
| "Build StatusTile, ValueLabel, StateBadge widgets" | "Show the motor's state and statusword on the main screen" |
| "Create the LVGL chart helpers"                    | "Plot live velocity while jogging"                         |
| "Build parameter list" → "Wire up parameter edit"  | "Read, edit and write a speed-loop gain"                   |

Protocol and driver work is exempt — it is verified by host unit tests and on the bus.

## Rules

- **Refuse to expand a `Draft` brief.**
- **No hardcoded doc mappings beyond the ones above** — discover the rest from `docs/README.md`.
- **Planning only** — never write code from this skill.
- **Ask when in doubt** — an open question beats a guess buried in a task.
- **Idempotent** — if the plan file exists, warn before overwriting.
