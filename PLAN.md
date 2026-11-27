# VibeHammer Development Plan

Goal for V1: a Windows `.exe` where two people play Intercessor Squad (5) vs Termagants (10) hotseat on a
24"x24" board, through the full turn sequence (Command, Movement, Shooting, Charge, Fight, End of Turn),
following the 11th edition core rules. The rules come from `docs/rules/` and the units from `data/`.

Architecture, conventions and rules-lookup policy are in `CLAUDE.md`. This file is the order of work.

## How to run each milestone

1. `/clear`, then set the effort level shown for the milestone (e.g. `/effort high`).
2. If the milestone says **Plan mode**, enter plan mode (Shift+Tab) and paste the prompt. Review the design before approving.
3. Paste the prompt. Claude reads the listed rule files first.
4. The milestone is done when every acceptance check passes and `ctest` is green. Then ask Claude to commit.

Keep sessions small:
- Read only the current milestone's section of this file, and the decisions it cites (`docs/decisions/D-NNN.md`).
- `/clear` between milestones, batches included: a batch is approved up front, each milestone gets a fresh session.
- While iterating, run the tests for the work at hand (`ctest --preset debug -R <milestone or rule>`); run the full
  `ctest`, arena runs and `--per-game` checks once, at the end.

### Effort levels

| Level | Use for |
|---|---|
| `medium` | Scaffolding, wiring, UI layout, data plumbing: well-specified work with few rule subtleties. |
| `high` | Ordinary rules implementation: one rule area with clear text and good test coverage. |
| `xhigh` | Rule areas with many interacting clauses, "if possible" constraints, or alternating-player flow. |
| `max` | Core architecture decisions that everything else builds on. Use it sparingly. |

If your Claude Code version doesn't have `/effort`, put "think hard" (for high) or "ultrathink" (for xhigh/max) in the prompt.

---

## Done so far

V1 (M0–M13) and the post-V1 milestones M14–M99 are done. Their full text (prompts, acceptance checks, results and open
items) is in `docs/plan_archive.md`; read a section there only when the current milestone needs it. Still open from
M97 and M98: the manual mouse checks on Windows (M94–M98), Explosives' CP against a later Command Re-roll, the
thresholds, and Explosives in the Start and End of Shooting phase steps.
Open items still carried are listed under "Later" below.

| Milestone | Title |
|---|---|
| M0 | Source material |
| M1 | Build skeleton |
| M2 | Core types, data loading, serialization |
| M3 | Geometry |
| M4 | Dice and the attack sequence (pure functions) |
| M5 | Game flow, PendingDecision and the action engine |
| M6 | Read-only 2D viewer |
| M7 | Movement phase |
| M8 | Shooting phase |
| M9 | Charge phase |
| M10 | Fight phase |
| M11 | Command phase, battle-shock, end of turn, game end |
| M12 | Interactive client |
| M13 | Save, load and replay polish |
| M14 | Close formation and no-overlap model moves |
| M15 | Client UI improvements |
| M16 | Click-on-the-board interaction |
| M17 | Action bar under the phase bar |
| M18 | Custom shots after picking the target |
| M19 | Target-then-weapon fights and custom casualty order |
| M20 | Dice roll pop-ups |
| M21 | Datasheet windows |
| M22 | Measuring ruler |
| M23 | Skip steps with nothing to do |
| M24 | Explain weapon abilities and keywords |
| M25 | Quality-of-life fixes |
| M26 | Undo by input, and "Undo last step" in a move |
| M27 | Per-model reach area |
| M28 | Movement area follow-ups, Cancel fixes, click-to-remove for coherency |
| M29 | Selection that follows the board; blocked paths shaded |
| M30 | Cancel on every click menu, kept advance roll, charge-move area |
| M31 | Skip pile-ins and consolidations too small to matter |
| M32 | Near base contact counts for step skipping |
| M33 | Skip moves that can only get marginally closer |
| M34 | Auto move for pile-ins and consolidations |
| M35 | Pick casualties on the board |
| M36 | Fix illegal moves |
| M37 | Engagement Range in the movement area; no Roll for rolls with no dice |
| M38 | Start screen and saved settings |
| M39 | Bot framework, driver, baseline bot, arena |
| M40 | Odds and threat analysis |
| M41 | Shooting, defensive and fight decisions (level Normal) |
| M42 | Charge planner |
| M43 | Movement planner and whole-turn plan |
| M44 | Game-aware evaluator, lookahead, tuning (level Hard) (open items) |
| M45 | Bot polish, packaging, docs |
| M46 | Bots pile in and consolidate when it costs nothing |
| M47 | Default casualty order: furthest from the action first, keeping coherency (open items) |
| M48 | Bot pacing and Pause in the sidebar |
| M49 | The Roll button only when the bot's dice wait for a click |
| M50 | Short ring legend, full text on hover |
| M51 | Bot dice follow Pause; no auto-dice setting |
| M52 | Release workflow, first GitHub release v0.1.0 |
| M53 | Scenario format for several units; the 44"x60" scenario |
| M54 | Speed for big games, with no change in play |
| M55 | Per-unit tint and board labels |
| M56 | Easy and Normal across several units |
| M57 | Hard across several units |
| M58 | Tuning, bot-game replay, docs |
| M59 | Fetch tool fixes and the nine new datasheets |
| M60 | Unknown abilities refused; Hazardous, Sustained Hits, Extra Attacks; mixed-loadout default plans |
| M61 | [TWIN-LINKED] with a re-roll decision |
| M62 | Random characteristics |
| M63 | Monsters and vehicles (17.01, 17.03) |
| M64 | Oval geometry kernel |
| M65 | Oval bases in the engine |
| M66 | Oval bases in the client and bots; big-base polish |
| M67 | The portrait scenario and the points tiebreak |
| M68 | Easy and Normal for the new units |
| M69 | Hard for the new units |
| M70 | Bot-game replay, docs, final verification |
| M71 | List-building data from BSData |
| M72 | Army lists, auto-layout, arena flags |
| M73 | Bots on custom armies (open items) |
| M74 | Deployment step in the engine |
| M75 | Army builder screen |
| M76 | Hand deployment in the client |
| M77 | Bot deployment heuristic, final verification, docs, PR (open items) |
| M78 | Measuring teamwork |
| M79 | The army plan (Normal) |
| M80 | Normal moves as a group |
| M81 | Focus fire and joint charges (Normal) |
| M82 | Hard on the army plan |
| M83 | Tuning, replays, docs, pull request |
| M84 | Docs housekeeping |
| M85 | One bot |
| M86 | Objectives and a mission |
| M87 | The bot plays for objectives (open items) |
| M88 | Terrain in 2D |
| M89 | The bot plays for cover (open items) |
| M90 | Objective Consolidation and [IGNORES COVER] |
| M91 | The bot plays terrain and objectives in every phase (open items) |
| M92 | The app-only rules audit |
| M93 | M92's open items |
| M94 | Engine-paced dice rolls (open items) |
| M95 | Reaction windows, Command Points and the first stratagems (open items) |
| M96 | Fire Overwatch with Snap Shooting, Heroic Intervention and Counteroffensive (open items) |
| M97 | Explosives and Crushing Impact (open items) |
| M98 | M97's open items: the selection on the board, Crushing Impact in the charge planning (open items) |
| M99 | [ANTI-X Y+], [DEVASTATING WOUNDS], [MELTA X] and [LANCE] (open items, one for the user) |

---

## Phase 1 of the roadmap: the batch (M99 onwards)

The user asked on 2026-11-27 for the roadmap's Phase 1 ("finish the core rules", `docs/roadmap.md`) to be worked
through without questions: "Continue as far as possible by yourself. Don't take my input, and ignore stuff in the rules
that say you need it ... make a note of any decisions you make yourself". So for this batch:
- each milestone is added here by Claude and started without the user's agreement;
- a rule the text leaves open is decided by Claude, conservatively, and recorded in that milestone's decision file under
  "own decision" for the user's review (instead of "stop and ask");
- everything else in `CLAUDE.md` holds: the rules text is read first, nothing is implemented from memory, every rule has
  tests named after it, no test is skipped, deleted or weakened, and each milestone ends green, committed and merged.

The order follows the roadmap (what unlocks the most datasheets first): the weapon abilities (M99 onwards), then the
unit core abilities, then the rule sections 16 to 23.

---

## M100: [LETHAL HITS], [PSYCHIC], [CLEAVE X], [ONE SHOT] and the keyword-conditioned abilities (D-102) ✅ (done; the manual mouse checks open)

**Effort:** high (one rule area; a model's weapon gains state). **Plan mode:** no (the batch above).
**Scope:** the weapon abilities left after M99 that need no new shooting type and no CHARACTER group. In the pinned
BSData catalogues: Psychic 59 profiles, Lethal Hits 44, the conditioned forms 10, One Shot 7, Cleave 4. It frees the
Balefire tome, the last roster weapon still refused.

Left out (they stay refused, D-071): [PRECISION] and [INDIRECT FIRE] (M101), a random X ("[SUSTAINED HITS D3]", 17
profiles) and a random A on a melee weapon (3 of 655 melee profiles).

**Rules:** 24.23, 24.29, 24.06, 24.26, 24.01, with 05.01, 05.02, 04.02, 13.08 and the app's 01.05.01 (automatic wounds),
02.02.02 (ignoring modifiers), 24.26.01. Decisions: D-006, D-011, D-021, D-026, D-071, D-073, D-089, D-101, D-102.

**Design** (D-102 has the full text)
- *[LETHAL HITS]:* a critical hit wounds automatically. No wound roll is made for it, and it is never a critical wound.
  The engine takes the automatic wound without asking; a profile that also has [DEVASTATING WOUNDS] (where the choice
  matters) is refused until the choice is built. No profile in the catalogues has both.
- *[PSYCHIC]:* the modifiers that worsen the attack's BS or its hit roll are not recorded for it (cover, 10.06's and
  17.03's -1); [HEAVY]'s +1 stays.
- *[CLEAVE X]:* X dice per five models in the target, when the weapon's attacks are not split.
- *[ONE SHOT]:* a model's own weapon is spent when its unit's attacks start with it selected; its [ONE SHOT] profiles
  are then refused and offered nowhere.
- *24.01:* [LETHAL HITS], [SUSTAINED HITS X] and [DEVASTATING WOUNDS] can carry keywords. They are applied when the
  batches are gathered, against the target unit's keywords.
- *Bots:* the odds mix the lethal critical hits in; the hypothetical volleys leave out a spent weapon and the penalties
  a psychic weapon ignores.
- *Client:* labels, help and effect lines; the wound roll's card and log line name the automatic wounds.

**Acceptance**
- [x] **[LETHAL HITS]:** wound rolls only for the other hits, with [SUSTAINED HITS] and [TWIN-LINKED], paced and inline;
  refused with [DEVASTATING WOUNDS]. `R24_23_LethalHits`.
- [x] **24.01:** the three abilities parse with keywords and apply only against a unit that has them.
  `R24_01_ConditionedAbilities`.
- [x] **[PSYCHIC]:** no cover and no -1, [HEAVY] kept. `R24_29_Psychic`.
- [x] **[CLEAVE]:** the dice for one target, none for split attacks. `R24_06_Cleave`.
- [x] **[ONE SHOT]:** once per battle, ranged and melee, each model's own weapon. `R24_26_OneShot`.
- [x] **No roster weapon is refused any more.** `D102_Roster`, `D079_*`.
- [x] **Bots:** the exact odds within 2% of 20,000 engine batches; the tome fired when in range; clean games; the
  identity set identical to the M99 binary. `M100_*` bot tests.
- [x] **Client:** `M100_*` client tests.
- [x] **A replay:** `tests/replays/m100_weapon_abilities.json`; the older replays byte for byte.
- [x] **Full `ctest`** green.

**Results** (D-102 part 7 has them)
- *Rules:* the abilities play through the engine with scripted dice, paced and inline alike
  (`tests/m100_weapon_abilities_test.cpp`). The older tests that used [LETHAL HITS] as "an ability the engine refuses"
  use the stand-in ability; three that asserted old text were brought to the new truth.
- *The replay:* `tests/replays/m100_weapon_abilities.json` (seed 1). The 33 older replay files are byte for byte.
- *The client:* labels, help and effect lines for the four abilities and the conditioned forms; the wound roll's card
  and log line name the automatic wounds.
- *Tests:* the full `ctest` passes, 1,761 of 1,761.
- *The bots:*
  - the exact odds of [LETHAL HITS] (alone, with [SUSTAINED HITS] and with [TWIN-LINKED]) are within 2% of 20,000 engine
    batches; the bot fires the Balefire tome when it is in range;
  - **the identity set:** the nine runs with fixed loadouts, 200 games each, are identical to the M99 binary in every
    field (1,800 of 1,800 games);
  - the four `--random-armies` runs change on purpose (the Legionary with the Balefire tome can be drawn) and are clean:
    800 of 800 games finished with no rejected action, reverted move or fallback. The M100 binary is their reference.

**Open items.**
- **For the user:** D-102's own decisions (the automatic wound taken without a question and the refusal of [LETHAL
  HITS] with [DEVASTATING WOUNDS]; a psychic attack ignoring every penalty; a [ONE SHOT] weapon spent when selected; no
  condition on abilities that act before the hit roll).
- The choice of 24.23 as a decision, when abilities grant [LETHAL HITS] (the roadmap's Phase 4).
- A random X, and a random A on a melee weapon (20 profiles in the catalogues).
- The bot fires a [ONE SHOT] weapon as soon as it is worth firing.
- The manual checks by mouse on Windows, as before.

---

## Later (post-V1, rough order)
The long-range map from here to a full game with any army on any mission (phases, order, open questions) is
`docs/roadmap.md`. The items below are the nearer, smaller ones; each roadmap item still becomes a milestone here first.

- **Dice pop-up polish:** done in M95 (a shown critical die has a gold border).
- **Engine-paced dice rolls (Option 2 of M20):** done in M94 (D-095).
0. **Deployment step:** done in M74 (engine), M76 (client) and M77 (the bots' heuristic), in the custom-battles batch
   (M71–M77). What is left of it is under item 5, "Open items carried from M71–M77".
1. **Terrain** (13): 2D terrain is M88 (D-087–D-089), and the bot's use of it is M89 (D-090). Still to do: the z axis
   (13.06 Moving Vertically and upper surfaces, 13.11 gaps above 3", 22.05 Plunging Fire), Stealth (24.33, left out of
   M88 by the user's choice; the Genestealers list it only because the fetch tool ignores BSData's Chameleonic
   condition, D-070), and a terrain editor for custom battles. Left open from M89: charge odds round walls (D-090's
   open list). M91 built the army plan's terrain rates and holding fire to stay hidden, both
   off by default (no measured gain, D-092); the 55% head-to-head target is open.
2. **Objectives** (14) and a simple mission: the engine is done in M86 (D-084), the bot in M87 (D-086). This enables
   Bolter Discipline. Left open from M86: nothing secures an objective yet (14.03 is state only). Left open from
   M87: the objective scenario's balance (the Chaos Space Marines win about a third: the Tyranids' OC takes the
   Centre; an asymmetric mission, other armies or terrain are the levers) and units holding back (about as at M83).
   M90 adds Objective Consolidation (12.08). M91 built the objective terms into charges, pile-ins and consolidations
   (off by default: no measured gain, D-092).
   - **Bot personalities** (after M87): Aggressive (values kills and charges, takes risks) and Tactical (values
     objectives, holds home, trades carefully), as weight presets on the one bot.
3. **Unit-specific abilities** for the two V1 units (Bolter Discipline, Tactical Mainstay, Skulking Horrors, Synapse).
4. **Core stratagems** (15): M95 (D-096) built reaction windows, CP spending (15.01), Command Re-roll and Insane Bravery;
   M96 (D-097) Fire Overwatch with Snap Shooting, Heroic Intervention and Counteroffensive; M97 (D-099) Explosives and
   Crushing Impact. Still to do: Epic Challenge ([PRECISION], attached units), Rapid Ingress (reserves) and Smokescreen
   (a SMOKE unit).
5. **More unit types** (M59–M70 added the fetch-tool fixes, Hazardous, Sustained Hits, Extra Attacks, Twin-linked,
   Random Characteristics, 17.01/17.03 and oval bases). Still to do:
   - **Weapon abilities:** Lethal Hits (24.23), Devastating Wounds (24.10), Anti-X (24.03), Melta (24.25), Lance
     (24.21), Precision (24.28). Until then, weapons with them are refused when selected (D-071).
   - **Fetch tool:** "Hunter: Monster/Vehicle" profiles (refused today), wargear limits ("1 in 5", "1 per unit"),
     `modifiers`/`hidden`, and bases with no size ("Hull", "-").
   - **Unit-level core abilities:** Feel No Pain (24.12), Deadly Demise (24.08), Leader and attached units (19),
     FRAME (17.02); unit abilities stay out of scope until item 3.
   - **Candidate units:** Assault/Heavy Intercessors, Infernus, Bladeguard, Hellblasters; Hormagaunts, Neurogaunts,
     Tyranid Warriors.
   - **Open items carried from M59–M70** (each where it was left):
     - *Bots, teamwork:* done in M78–M83 (D-083): an army plan forms groups of two or three units on one target;
       Normal moves its groups together, focuses fire, finishes wounded units and charges jointly; a unit without a
       group closes in unless outreached. Left open (M83's Results): the rosters' Tyranid gang-ups (1.7x, not 2x), lone
       engagements and units never within 12" (not halved: the Tyranid infantry is outreached and still holds back
       40-50" away), Normal's rosters p95 (16 ms) on this machine, and the plan's stability (about 60% of an earlier
       mover's partners are still partners when they move, M80). M87 takes over the gang-up, lone-engagement and
       holding-back items (with objectives). (M85, D-085: the Hard and `normal-m77` items went with them.)
     - *Bots:* they never turn an oval (M66, M68, M69); random damage is planned at the preview's maximum (M62, M68);
       the Desperate Escape guard checks one to three failed hazard rolls (M68, M69); the firer's own hazard loss is
       priced in the choice of profile but not taken off Normal's move score, and D-071's "no hazard roll after the
       last enemy model is destroyed" is ignored there (M68); Normal's two-half-turn horizon keeps far units holding
       (M68: measured right for these armies).
     - *Client:* the per-model editors choose one declaration per model, and the Split popup splits only the main
       weapon (M60); no setting re-rolls [TWIN-LINKED] automatically (M61); at a narrow window the action bar can
       still clip a long casualty hint or the "Damage left to allocate" box (scroll the bar; M70).
     - *Engine:* the oval-against-oval turn check is conservative (M65; the rosters have one oval).
     - *Fetch tool / datasheets:* a mixed ranged/melee weapon's profiles list under the first profile's type, and
       ability-name modifiers (Deadly Demise X, Scouts X) and hidden-ability conditions are not evaluated (M59).
   - **Open items carried from M71–M77** (custom battles; each where it was left):
     - *Bots, random armies:* the rates of `kMaxLoadouts` and `kMaxAdvanceSteps` were measured at M73 and not
       re-instrumented since. (M85, D-085: Hard's items there went with Hard.)
     - *Bots, deployment:* the bot never turns an oval when setting it up, and its candidates are whole-inch
       translations of core's proposal (M77, D-081).
     - *Client, hand deployment:* no snapping or "Fix illegal moves" for a set-up (a refused ghost stays where it was let
       go, D-082 part 2, M76); at 1280x800 one long unit name in the builder ("Chaos Terminator Squad 3") is clipped
       before its number (M75).
     - *Manual checks by mouse on Windows* (for the user, M76, M77): build, save and reload two lists; deploy by hand
       against the bot (dragging, Shift-dragging, turning the Tyrannofex with Q / E and Shift+wheel, Enter, Undo);
       finish a custom battle.
     - *Factions:* custom battles offer Chaos Space Marines and Tyranids only, at Incursion (1000 points); Space Marines
       stay for the old scenarios (agreed scope).
6. **Networking:** a server-authoritative host runs the engine, clients send `GameAction`s and receive events or snapshots, and spectators subscribe to events. The engine API shouldn't need to change. If it does, that's a bug in the earlier design.
7. **Attached units, transports, reserves, aircraft** (17–23).
