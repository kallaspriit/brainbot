---
name: yeet
description: Commits all current changes with an auto-generated commit message and pushes to the remote. Use when the user wants to quickly commit and push without writing a commit message.
---

# Skill: Yeet

Commit all staged and unstaged changes with a generated message and push.

## Invocation

```
/yeet [optional message hint]
```

- `[optional message hint]` — context to work into the commit message

## Workflow

### Step 1: Gather context

Run in parallel:

1. `git status` (never with `-uall`)
2. `git diff`
3. `git diff --cached`
4. `git log -5 --oneline` — recent commit style

If there is nothing to commit, say so and stop.

**Check for things that must not be committed:** build output (`build/`, `.pio/`), `sdkconfig.old`, Wi-Fi credentials or other secrets in the diff, large binaries that are not design files. If any appear, stop and tell the user.

### Step 2: Generate the commit message

- **Format:** subject line in imperative mood, max 72 characters. Add a short body only when the "why" is not obvious from the subject.
- **Style:** match recent commits in the repository.
- **Content:** what and why, not how.
- **Hint:** if the user gave one, work it in.

Examples: `Add SDO client with abort code decoding`, `Fix statusword decode for quick stop active`, `Document motor CNET pinouts`.

### Step 3: Stage, commit and push

1. `git add -A`
2. Commit, ending the message with the `Co-Authored-By` trailer from the session's attribution instructions.
3. `git push`. If there is no upstream: `git push -u origin <branch>`.

### Step 4: Confirm

Briefly: files committed, the message used, push result.

## Rules

- **Never skip hooks** — no `--no-verify`
- **Never force push**
- **One commit** with all changes
- **Keep it brief** — minimal output
