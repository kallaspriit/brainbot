---
name: summarize-today
description: Summarizes today's git commits into a concise one-sentence daily work log entry with the time span worked. Use when the user wants a summary of what they worked on today.
---

# Skill: Summarize Today

Generate a concise daily work summary from today's git commits.

## Invocation

```
/summarize-today
```

## Workflow

### Step 1: Get today's commits

```
git log --after="midnight" --format="%H|%aI|%s" --all
```

If there are no commits today, say so and stop.

### Step 2: Calculate duration

1. Parse the timestamps of the earliest and latest commits.
2. Express the difference as a human-readable duration (e.g. "6 hours 15 minutes").
3. With a single commit, write "single commit" instead of a duration.

### Step 3: Generate summary

Write a **single sentence** summarizing what got done:

- **Length:** about one or two commit messages long
- **Focus:** the most impactful work; skip trivial changes
- **Tone:** past tense, factual ("Implemented X, added Y, and fixed Z")
- **Skip:** merge commits, formatting-only changes

### Step 4: Output

```
**{date}** ({duration}) — {summary sentence}
```

Example:

```
**2026-10-08** (4 hours 30 minutes) — Checked feasibility of the board, power and motor protocol, and set up the documentation, CLAUDE.md and project skills.
```

## Rules

- **One sentence only** — no bullet points, no second sentence
- **No fluff** — be direct
- **Impact over volume** — 15 commits still get one sentence about what mattered
- **Human-readable duration** — "X hours Y minutes", minutes rounded to the nearest 5
