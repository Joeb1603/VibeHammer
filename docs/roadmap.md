# Roadmap: from today's engine to a full game with any army

Written 2026-11-20, after M89; brought up to date after M91 (M90 added 24.18 [IGNORES COVER], D-091), and for
M92–M95 in Phase 0. This is the long-range map, not a milestone list: `PLAN.md` stays the order of work,
and each item here still becomes a `PLAN.md` milestone with rules and acceptance criteria, agreed before it starts
(`CLAUDE.md`, "Working style"). Sizes are rough guesses in milestones, to show proportion, not promises.

## The goal

Two players (human or bot, either side) play a complete, legal game of Warhammer 40,000 11th edition:

- with **any army** that can be mustered under 25 Muster Armies, at Incursion or Strike Force;
- on **any of the 45 published layouts**, with **any of the 25 primary missions** and the **18 secondary missions**;
- with every rule the armies on the table use either implemented or, where it is not yet, refused with a rule-cited
  reason (the D-026 / D-071 principle, kept to the end: nothing is ever silently ignored);
- and with a **bot that plays whichever mission it is given**, without mission-specific code.

"Any army" is defined as: every standard, currently legal datasheet, detachment, enhancement and stratagem in the
pinned data version. Legends, Crusade and the like are out for now (the user, 2026-11-20).

## Where the project stands

| Area | Today | Gap to the goal |
|---|---|---|
| Core rules 01–14 | 67 of the 156 numbered rules have tests: the turn sequence, the attack sequence, moves, charges, fights, 2D terrain, objectives | 10.07 Indirect Shooting; the z axis (13.06, 13.11, 22.05); 14.03 securing |
| Core rules 15–23 | 15.01, 15.02, 15.04 (M95), 15.08, 15.09, 15.11, 15.12 (M96), 15.05, 15.06 (M97), 17.01 and 17.03 | The other three core stratagems (15.03 Epic Challenge, 15.07 Rapid Ingress, 15.10 Smokescreen: each waits for its rule system or keyword), actions, FRAME, transports, attached units, reserves, flying, surging, auras, psychic, aircraft |
| Core abilities (24) | 11 weapon abilities and random A/D | 26 of the 37 entries: see Phase 1 |
| Missions | One house mission (D-084), scored from the scenario file | The real mission pack: 25 primaries, 18 secondaries, force dispositions, layouts |
| Armies | 11 datasheets, 2 factions in custom battles, Incursion only | About 28 faction lists, 256 detachments, well over a thousand datasheets |
| Unit, faction and detachment rules | Loaded and shown as text; none implemented | The largest part of the whole roadmap: Phase 4 |
| Bot | Plays the house mission, uses cover, works as an army; Command Re-roll and Insane Bravery behind keys (M95); Fire Overwatch, Heroic Intervention and Counteroffensive behind keys (M96) | Mission-generic scoring, the other stratagems, actions, reserves, transports, abilities |
| Client | Hotseat, bot, army builder, hand deployment | A screen for every new decision, a mission and layout picker, the full army builder |

Sources of rules text, and their limits:

- `docs/rules/`: the PDF core rules. Source of truth.
- `docs/gdmissions/`: the same rules plus 101 app-only entries, the mission pack, layouts (images) and detachment
  names with costs. Third-party and unverified.
- BSData (`tools/fetch_bsdata.py`): datasheets and list-building data. Whether it carries detachment rules,
  stratagems and enhancements for 11th edition could not be checked from the cloud session (question 4 below).
- Nothing in the repo holds the text of faction rules, detachment rules, stratagems or enhancements.

## The order, and why

```
Phase 0  decisions and groundwork
Phase 1  finish the core rules           -- nothing below works without these
Phase 2  the mission system              -- can start alongside Phase 1 once actions (16) exist
Phase 3  mustering any army              -- data and list building; needs attached units (19) from Phase 1
Phase 4  army rules as data              -- needs Phases 1 and 3; by far the longest
Phase 5  the bot                         -- one slice at the end of each phase above, not a phase at the end
Phase 6  the client                      -- likewise, one slice per engine feature
Phase 7  proving it and keeping it true
```

Phases 5 and 6 are written as their own sections so their design is in one place, but the work is done feature by
feature: an engine milestone, then its client and bot milestones, as M86 / M87 and M88 / M89 were.

## Phase 0: decisions and groundwork (2–3 milestones)

1. **Audit the app-only rules.** *Done in M92 (D-093): the scraped app text overrides the supplied wording.* Compare the 101 *APP* entries in `docs/gdmissions/core_rules/` with the working
   assumptions in D-009, D-010, D-011, D-012, D-015 and D-069. Record each difference as a decision; fix the engine
   where the user agrees the scraped wording is right. This also settles which remaining "known gaps" are closed.
2. **Engine-paced dice rolls.** *Done in M94 (D-095): every roll is a RollDice decision of its roller's; the game
   session paces it (a human presses Roll with the dice pop-ups on), replays and saves record it (format 2), and
   apply_action still makes it inline. A random D is rolled once per batch, and no roll has a window after it yet: the
   per-attack damage split and the windows are for Command Re-roll and Feel No Pain.* Every roll becomes a point the engine
   can stop at. Command Re-roll, re-roll abilities, Feel No Pain, Fire Overwatch and most reactive stratagems need a
   decision between a roll and its result, so this comes before Phase 1's stratagems. Every replay is regenerated.
3. **Reaction windows.** *Done in M95 (D-096): a window is a UseStratagems decision for the side that can use a
   stratagem there, ordered by Rules Sequencing; its first timings are "just after a roll" (Command Re-roll) and "just
   before a battle-shock roll" (Insane Bravery), with CP spent (15.01) and a random D rolled per attack. Later timings
   (the end of a phase, after a unit's attacks) add to the same mechanism.* A general way for the opposing side to be
   offered a decision at a trigger ("just after an enemy unit ends a move", "just after a unit is selected to fight").
   Built once, used by stratagems, abilities and mission rules. Rules Sequencing (app-only) decides the order when both
   sides have one.
4. **Settle the open questions** at the end of this file; question 4 changes the size of Phase 4.

## Phase 1: finish the core rules (about 25–35 milestones)

Each line is roughly one engine milestone, with its client and bot slices to follow. The order inside the phase is by
what unlocks the most datasheets.

**Weapon abilities (24)**: Lethal Hits, Devastating Wounds, Anti, Melta, Lance, Precision, Cleave,
Close-Quarters, Indirect Fire with 10.07, One Shot, Psychic, random A on melee weapons. After this no weapon profile
is refused.

**Unit core abilities (24)**: Feel No Pain, Deadly Demise, Fights First, Stealth, Lone Operative, Scouts and the
Scout move, Infiltrators, Deep Strike, Leader, Support, Firing Deck, Hover, Super-Heavy Walker. Duplicated
Abilities (24.02) applies throughout.

**Rule sections**

| Section | What it adds | Depends on |
|---|---|---|
| 15 Stratagems | CP spending, 15.01, the eleven core stratagems (M95: 15.01, Command Re-roll, Insane Bravery; M96: Fire Overwatch with Snap Shooting, Heroic Intervention, Counteroffensive; M97: Explosives, Crushing Impact) | Phase 0 items 2 and 3 |
| 16 Actions | Starting, completing and interrupting actions | none; Phase 2 needs it |
| 17.02 FRAME | Models measured to a frame, not a base | the fetch tool's "Hull" bases |
| 19 Attached Units | Leaders and Support joined to bodyguards; 19.02 allocation, Precision | Leader, Support |
| 18 Transports | Capacity, embarking, the disembark moves, destroyed transports | Firing Deck uses it |
| 20 Strategic Reserves | Reserves, arriving, the ingress move; Deep Strike and Rapid Ingress | reaction windows |
| 21 Flying and Surging | Surge moves; flying over models and terrain | partly the z axis |
| 22 Other rules | Auras, faction abilities, psychic and wargear abilities, Plunging Fire | Phase 4's effect layer for 22.01–22.04 |
| 23 Aircraft | Aircraft deployment, movement, shooting, fighting | reserves, Hover |
| 14.03 | Securing objectives (state only today) | none |

**The z axis stays out.** 13.06 Moving Vertically, 13.11 gaps above 3", 22.05 Plunging Fire and the height part of
21.03 need height. The engine is 2D by decision (D-087), and the user confirmed 2D is fine for now (2026-11-20), so
these stay `TODO(z)` and are not part of this roadmap. Any ability that only matters with height is recorded as
"not modelled" in the coverage table instead of being refused.

**Battle size.** Strike Force (2,000 points) beside Incursion: unit limits, DP and enhancement limits from 25.03, and
the bot's time budgets re-measured at twice the army.

Done when: every one of the 156 rule numbers has tests or a recorded decision saying why it has none (the z-axis
rules among the latter), and every core ability is implemented.

## Phase 2: the mission system (about 10–14 milestones)

The aim is that a mission is **data**, read by one scoring engine, so that the 25 primaries and 18 secondaries are
files and not code, and so that the bot can read the same data (Phase 5).

1. **Battlefield roles.** Attacker and Defender, battlefield edges, deployment zones, territory, no man's land;
   objectives typed as home, central and expansion; terrain areas that are objectives. Replaces D-084's fixed areas
   (the house mission stays as one more mission file, so its replays keep working).
2. **Mission state.** Named markers and tags that cards create and read, kept generic: operation markers, and
   objectives or terrain areas marked as cleansed, consecrated, trapped, triangulated, surveilled, decoyed and so
   on. One mechanism (a tag on a unit, objective, terrain area or point, with an owner and a lifetime), not one field
   per card.
3. **Mission actions.** The cards' actions (Cleanse, Booby Trap, Sensor Sweep, ...) as data on top of 16 Actions:
   starts, units, use limit, completes, effect, restriction.
4. **The scoring language.** A small declarative format for a scoring rule: when it is checked (any battle round,
   second battle round onwards, end of battle), the trigger (end of your turn, end of your Command phase, ...), the
   condition, the VP, and how rows combine (alternative, cumulative, per unit, per event, capped). The site's card
   data already has this shape (`docs/gdmissions/missions/`), which is the evidence that it fits all 43 cards. The
   conditions need a fixed vocabulary of queries the engine answers: units destroyed this turn, control of an
   objective, units wholly within a zone, a tag present, starting strength, and the like.
5. **Primary missions.** The five decks, transcribed into that format, each with tests named after its card.
6. **Force disposition and the matrix.** Each player's disposition, taken from their detachments, picks their
   primary mission. The two players usually score different primaries, so missions are per side.
7. **Secondary missions.** Fixed or Tactical, chosen secretly; the Tactical deck, drawing, "when drawn" rules,
   discarding, and the VP caps from the Tournament Companion.
8. **The game sequence** from the Tournament Companion: muster, determine mission, determine layout, create the
   battlefield, attacker and defender, secondaries, deployment, first turn, and the end-of-battle result.
9. **Layouts.** Transcribe the 45 measured layout images into scenario terrain files. This is hand work checked by
   eye: render each file in the client and compare it with the image. A terrain editor (already listed under
   "Later", item 1) makes this much faster and should come first.

Done when: any pairing of dispositions on any of its three layouts can be set up and scored to the end, and each
card's scoring is covered by tests with scripted games.

## Phase 3: mustering any army (about 8–12 milestones)

1. **Bulk datasheets.** Run the fetch tool over whole BSData catalogues instead of one unit at a time, with a
   coverage report: datasheets converted, datasheets refused and why. Finish the fetch tool's known gaps (the
   "Hunter" profiles, wargear limits, `modifiers` and `hidden`, bases with no size, mixed ranged/melee profiles).
   `data/units/` grows from 11 files to all of them, so its layout becomes one folder per faction.
2. **The rest of the roster data.** Detachments with their force dispositions and DP (cross-checked against
   `docs/gdmissions/detachments.md`), enhancements with points, Leader and Support attachment lists, transport
   capacities, Warlord rules, allied units.
3. **Muster Armies (25).** Army faction, battle size, DP, detachment restrictions, unit limits (Battleline and
   Dedicated Transport doubled, Epic Hero 1), Warlord, attaching Leaders and Support, enhancements. All as
   validation with rule-cited reasons, like the rest of the engine.
4. **The army builder** for every faction, and **importing a roster** from a New Recruit export so a player can
   bring a list they already have.
5. **Updating the data.** A repeatable way to move the pinned BSData commit forward: regenerate, diff, list what
   changed (the site's faction changelog in `docs/gdmissions/changelog/` shows how often this happens: 15 data
   versions in five months).

Done when: every faction's catalogue converts with a coverage report, and a legal 2,000-point list of any faction
can be built, saved, loaded and deployed.

## Phase 4: army rules as data (open-ended; the bulk of the project)

Datasheet abilities, faction rules, detachment rules, enhancements and stratagems. There are thousands of them, so
they cannot be hand-written one function each. The plan is an **effect layer**: a rule is a data record made of a
few building blocks the engine implements once. (The user left the choice between this and plain code per rule to
Claude, 2026-11-20. The effect layer is chosen because the volume is in the thousands, because data records can be
counted for the coverage table, and because the bot's simulator then sees every ability through the same few blocks.
Rules that do not fit still get hand-written code: item 5.)

1. **The building blocks.** Worked out from a survey of real abilities, not guessed; expected to include:
   - *when it applies:* always, while leading, in a phase, in an aura (22.01), once per battle or turn, when a
     trigger fires (a reaction window from Phase 0);
   - *who it applies to:* this model, this unit, friendly units with a keyword in range, the target, the attacker;
   - *what it does:* modify a characteristic or a roll (through D-011's caps), grant a core or weapon ability,
     allow a re-roll, deal mortal wounds, let a unit move, shoot or fight out of sequence, change eligibility
     (advance and charge, fall back and shoot), return models, change set-up, change CP or VP.
2. **The refusal rule, extended.** A datasheet with an ability that has no record is playable only with that
   ability shown as "not implemented" and both players told so before the game; a roster's coverage is shown in the
   builder. This keeps every intermediate version honest.
3. **Coverage, tracked.** A generated table: per faction, how many abilities, detachment rules, enhancements and
   stratagems have records. It is the progress bar for the whole phase.
4. **One faction at a time.** Chaos Space Marines and Tyranids first, because their datasheets and rosters are
   already here, then by the user's order (question 5). A faction is done when its datasheets, army rule,
   detachments, enhancements and stratagems are all covered and a bot-against-bot game with each detachment runs to
   the end.
5. **The long tail.** Some rules will not fit the building blocks. Each gets either a new block (if others will use
   it) or a small hand-written rule with its own tests. Expect this to be a real share of the work.

The blocking question is the **text**: faction rules, detachment rules, enhancements and stratagems are not in the
repo and may not be in BSData (question 4).

## Phase 5: the bot

The bot is already a player that only submits actions and judges positions with its own simulator (D-054), so every
engine feature reaches it as new legal actions. What has to change is how it values a position.

1. **Mission-generic scoring.** Today's objective terms (D-086) are written for the house mission. Replace them
   with one term: the VP each side can expect from the mission data in play. The engine exposes the scoring rules
   as a read-only query ("what does this side score if the turn ends like this", "which conditions are still open");
   the bot values moves by expected VP swing. With missions as data (Phase 2), a new mission needs no bot code.
   This is what makes the bot adapt to all the mission types.
2. **The asymmetry.** The two sides score different primaries, so the bot values its own scoring and denies its
   opponent's, each read from that side's card.
3. **Mission actions.** Starting an action costs shooting or more; the bot weighs the VP against what the unit
   would otherwise do.
4. **Secondaries.** Choosing Fixed or Tactical before the game; with Tactical, which cards to keep, play for or
   discard.
5. **Stratagems and CP.** When to spend, and what to keep back for a reaction. Reactions need the simulator to
   model the opponent's possible replies, which it does not do today.
6. **Reserves, transports, leaders, abilities.** Arrival turns and spots; embark and disembark plans; keeping
   leaders alive; using once-per-battle abilities at a good time. Abilities reach the simulator through the effect
   layer, so the bot sees their results without being told about each one.
7. **Measured across the pack.** The arena runs the mission matrix: all 25 primaries, a spread of layouts and a
   spread of armies. A change must not make the bot worse on any mission family while improving another; the
   "identity set" (`docs/dev/bot_arena.md`) grows to cover them.
8. **Time.** 2,000 points with reserves and reactions is far more to search than today's 1,000; budgets and the
   p95 time per decision are re-measured at each step.

Done when: on every primary mission the bot beats a baseline that ignores the mission, and loses no mission family
badly to itself with sides swapped.

## Phase 6: the client

One slice per engine feature, each through `GameSession::submit` as now:

- a mission, disposition and layout picker; the cards in play shown on the board, with a VP tracker per card;
- secondary missions: choosing, the hand, drawing and discarding;
- stratagems: a prompt at each reaction window that can be answered or passed quickly, with CP shown;
- actions, reserves and arrivals, embarking and disembarking, attaching leaders, transports' passengers;
- ability effects made visible: which modifiers apply to a roll and why, auras drawn on the board;
- the army builder for every faction and battle size, roster import, and a coverage warning per list;
- the terrain editor (Phase 2 needs it early).

Mouse-only flows still need the user's manual pass on Windows after each.

## Phase 7: proving it and keeping it true

- A replay per mission card and per rule section, as now.
- Bot-against-bot soak games across factions and missions, run in the arena, checking only that every game ends
  legally: the cheapest way to find rules that break each other.
- A full game by mouse on Windows for each new kind of decision.
- Data updates as a routine (Phase 3 item 5), with the coverage table re-generated.

## Decisions and open questions

Answered by the user on 2026-11-20:

1. **The z axis:** 2D is fine for now. Vertical movement, gaps above 3" and Plunging Fire stay out (Phase 1).
2. **How army rules are written:** left to Claude; the effect layer (Phase 4).
3. **What "any army" leaves out:** Legends and similar content; standard legal units only for now.

4. **Where the army rules text comes from.** Answered on 2026-11-25 (M95), from the fetch tool's cache of the pinned
   commit (`data/.cache/bsdata/0f9035541630ae6d83869bab34b63d909767d081/`, gitignored, downloaded in M59). BSData's
   repository itself can't be reached from a cloud session: the session's GitHub access only reaches the user's own
   repositories, so adding BSData was refused and the git proxy refuses an anonymous read. At that commit:
   - **army rules** are there, as shared rules: Chaos Space Marines' Dark Pacts; the Tyranids' Synapse, Shadow in
     the Warp, Synaptic Web and Insidious Ambushers;
   - **detachments** are there with their rule text: 17 for Chaos Space Marines and 14 for the Tyranids (the
     Tyranid library also holds 12 Genestealer Cults detachments);
   - **enhancements** are there with their text and points: 62 for Chaos Space Marines (the Tyranid ones are linked
     another way and were not counted);
   - **stratagems are not**: no text in the stratagem form (WHEN, TARGET, EFFECT) is in any catalogue or in the
     game-system file `Warhammer 40,000.json`.

   So Phase 4 can read datasheet abilities, army rules, detachment rules and enhancements from BSData with the fetch
   tool. Each detachment's stratagems have to come from the app or the codexes, supplied by the user. The core
   stratagems are in `docs/rules/` (section 15).

Still open:

5. **Faction order** after Chaos Space Marines and Tyranids.
6. **Networking** ("Later", item 6) is not on this roadmap. Is it part of "complete"?

## What this adds up to

Phases 0 to 3 are bounded: roughly 45 to 65 milestones at the size the project has been working in, and at the end
of them any army can be mustered and played on any mission with core rules only, its special rules shown and
flagged. Phase 4 is where "any army, fully" is earned, and its size depends on question 4; it should be
expected to be larger than everything before it put together.
