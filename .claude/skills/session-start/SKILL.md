---
name: session-start
description: Start a SynthCore development session. Use at the beginning of a new session, or when the user says "start the session", "where are we", "what's next" or runs /session-start. Reviews the state and open items, talks through the engine features with the user, writes the agreed todo list into CONTINUE.md, then proposes options to start with.
---

# Start a development session

Goal: in a few minutes, get from "new session, no context" to "we agree on what to do today, and it is written down".
Keep every step short. Do not write code in this skill; it ends when the user picks an option.

## 1. Catch up (silently, no long report)

Read, in this order:
1. `CONTINUE.md`: state, latest measurements, next steps, known debt, working style and traps. The section **"Session todo"** holds the list from the last session.
2. `DEVELOPING.md` and `ENGINE_DESIGN.md`: only the headings and the sections the open items point at (ADR-035 for board work, ADR-036 for the modular engine). Do not re-read everything.
3. `git status --short` and `git log --oneline -8`: uncommitted work, what changed since the notes were written.
   If the code and `CONTINUE.md` disagree (work done but not recorded, or recorded but not committed), note it for step 2.

## 2. Short status to the user

At most ~15 lines:
- **Where we are**: one or two sentences (what runs on the board, what runs on the host only).
- **Open from last time**: the "Session todo" items still open, plus anything unverified (not flashed, not listened to, not measured).
- **Mismatches** found in step 1, if any.

## 3. Exchange on the engine features

Ask 2-4 short questions, chosen from what is open, for example:
- What did you hear or see since last session (board, simulator)? Anything to change in the sound?
- Which feature matters most now: new modules / engines, sampler, sequencer, UI pages, performance (more voices), hardware items?
- Is there an idea you want to discuss before building it (design session, options with pros and cons)?

Listen, answer briefly, give your opinion when asked. Follow the project rule: the user decides design and quality trades; offer options with pros and cons,
record the decision and its risk, never re-litigate a recorded decision (see "Decisions that went against my recommendation" in `CONTINUE.md`).
One or two rounds are enough; do not turn it into an interview.

## 4. Write the todo in CONTINUE.md

Update the section `## Session todo` in `CONTINUE.md` (create it right after the intro paragraph if missing). Format:

```markdown
## Session todo (updated YYYY-MM-DD)

- [ ] <item>: <one line: why / what "done" means> (from: user | notes | debt)
- [x] <item done last session> (done YYYY-MM-DD)
```

Rules:
- Use today's date (absolute, never "today").
- Keep open items from before unless the user dropped them; move items finished last session to `[x]` and drop `[x]` items older than one session.
- Order by what the user said matters most. At most ~10 open items; long-term ideas go to "Next performance steps" or "Known debt", not here.
- Keep the file's line endings (check before editing). Do not commit.

## 5. Propose options to start with

Give 2-4 options, each as:

> **A. <name>** (~effort: small / half session / full session)
> what we would do, what it unblocks. Pros / cons in one line each. Needs the board? Needs listening?

Mark one as your recommendation and say why in one sentence. Then wait for the user's choice.
When the choice is board performance work, continue with the skill `esp32-optimize`.
