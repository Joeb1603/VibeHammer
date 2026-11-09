# Bot feedback: play observations to act on

Notes from playing the bots, written 2026-11-09 against `main` at M70 (`bc78540`). Nothing here has been done
yet. Each item says what was seen, why the bot does it today and what a fix could look like. A fix is bot work
(`src/ai`), so it needs a milestone in `PLAN.md` first, with arena measurements as its acceptance.

**The game behind these notes.** A human (Side A, Chaos Space Marines) against Hard (Side B, Tyranids) on
`rosters_44x60.json`, dice seed 402906411, 612 steps. Side A won 307.5 to 253 on points destroyed after round 5.
The replay was exported to `replays/full-game.json`, which is gitignored, so it lives only on the machine that
played it. Copy it to `tests/replays/` only if a milestone turns it into a test.

## 1. Power in numbers: a combined advance

- [ ] **Seen.** The Tyranids never moved forward as one army. The cheap infantry held back all game, and the
  monsters went in one at a time, so each one fought the whole Chaos Space Marine army alone.
  - Termagants 1 (unit 6) moved into the top-left corner in round 1 (about 3", 52") and stayed there, moving
    under half an inch a turn. It never fired: it was over 18" from every enemy all game.
  - Termagants 9 (unit 14) held top-right for three rounds and even backed toward its edge in round 3 (about
    40", 58.5"). It first fired in round 4. Its 20 shots in the whole game did no damage.
  - Round 4: Screamer-killer 7 (unit 12) charged Legionaries 5 alone and killed 4 of them. In Side A's next turn
    the Chaos Terminators charged it and destroyed it.
  - Round 5: the Hive Tyrant failed a 4" charge. Screamer-killer 3 (unit 8) charged Legionaries 1 alone.
  - Both Termagant units and both Genestealer units finished at full strength, having done nothing.
- **Why the bot does it.** Each unit's move is scored mostly on its own exchange with the closest enemy unit
  (D-066, D-077). By that measure every Tyranid infantry unit loses its trade. For example, Genestealers charging
  boltgun Legionaries deal about 0.24 (points-weighted shares) and take about 0.50 back (D-077). So each one
  holds back, as a losing side does. Under the points tiebreak (D-076) that is locally sound: a unit nobody can
  reach gives away no points.
  - D-066's marginal exposure counts the other enemy units' replies, but nothing scores the army's position as a
    whole. Nothing says "if nine units arrive in the same turn, the enemy can shoot only some of them, and it can't
    charge them all".
  - Hard on this side moves as Normal while it has more than 7 units (`move_search_units`, D-078), so this is
    Normal's move planner at work.
- **Why a combined advance should do better.** The Chaos Space Marines have 5 units. Each can shoot one target a
  turn (or split fire) and charge once. If the Tyranid units arrive together, fire that would wipe out one
  isolated unit is spread over many, so the loss each unit expects is much lower than its one-on-one estimate.
  The units that survive then reach melee at the same time, where the monsters already win their fights (D-077:
  Normal vs Normal, the Tyranids win 140 of 200).
- **Ideas for a fix**, cheapest first:
  - *Saturation in the move score:* when a unit judges how much enemy fire it will draw, share each enemy unit's
    shooting between all the friendly units it could target next turn. The enemy can't shoot every one of them
    with full force. This keeps the per-unit planner and only changes its threat estimate.
  - *Move as a wave:* give each move a term for staying level with the rest of the army, e.g. a cost for ending
    more than X" ahead of or behind the army's centre line. The bot then advances together, or not at all.
  - *An army-level decision each turn:* "advance" or "hold", chosen by simulating both for a turn or two (Hard's
    lookahead already has the pieces), then each unit's move is planned within that stance.
- **How to measure.** Release arena on `rosters_44x60.json`, seeds 1–200 per seat, against today's Normal and
  Hard. Count each side's wins, points destroyed, and the Tyranid infantry's damage dealt and lost. Also check
  against a human: the bot currently loses to one.
- **Keep.** V1, the two bot scenarios and Strike must play exactly as before (`--per-game` hashes against
  `baseline/m60`), as every bot milestone since M68 has done.

## 2. Joining an ongoing combat (ganging up)

- [ ] **Seen.** When one Tyranid unit was in combat, no other Tyranid unit moved toward that fight to join it.
  - Round 4: Screamer-killer 7 fought Legionaries 5 alone.
  - Round 5: Screamer-killer 3 fought Legionaries 1 alone.
  - Nobody could have joined either fight in that turn. Termagants 9 had Advanced, so it could not charge (11.02),
    and both Termagant units were 22"+ away. That was a result of holding back in rounds 1–3 (item 1).
- **It is legal.** 11.02 stops a unit that *is* engaged from declaring a charge. Nothing stops a unit charging an
  enemy unit that is already engaged with one of yours. The 11.04 charge move has the usual constraints: end
  engaged with every charge target, and not with any other enemy unit.
- **Why it is a good idea:**
  - An engaged enemy unit can shoot only units it is engaged with, except MONSTERS and VEHICLES (10.06, 17.03).
    So moving up to it is safer than moving up to a free enemy.
  - The second unit's attacks can finish the enemy unit off, so the first unit takes fewer attacks back next
    fight, and consolidation frees both units sooner.
  - Even weak attackers count once the target's numbers are down. Termagants (S3, wounding T5 on 6s) won't kill
    Legionaries alone, but 10 extra attacks on a half-dead squad can finish it.
- **Why the bot doesn't do it.** The charge planner and the move score value a charge on its own exchange with
  the target, plus the replies from other enemy units. Nothing counts what our unit already in that fight is
  doing, or how much sooner the target dies with two of our units attacking it.
- **Ideas for a fix:**
  - *Charge value:* when the target is already engaged with a friendly unit, value the charge on the combined
    fight. Our attacks are added to the ones already landing, and the target's attacks back are split between
    our two units. The engine's fight sequence decides the order, so build it from the simulated fight, not a
    formula.
  - *Move toward a fight:* in the Movement phase, a unit that could reach charge range of an engaged enemy unit
    next turn gets a bonus for ending within reach (about 12" minus the move it still has to make).
  - Interactions to check: 12.03 pile-ins (a second unit's models arriving), 12.08 consolidation choices (D-077),
    and Fights First from the charge (11.02).
- **How to measure:** the same arena runs as item 1, plus a count of fights with more than one unit per side.
  Item 2 matters more once item 1 makes armies arrive together, so measure them together and on their own.

## 3. Smaller notes from the same game

- [ ] **Lone characters are never hunted.** The bot only looks at its nearest enemies, so a lone CHARACTER behind
  the lines is never singled out. In this game the Chaos Lord stood 4.6" from Side A's own edge, behind the whole
  army and over 45" from every Tyranid unit, so nothing could have reached it anyway. Hunting characters only
  pays when the path is open. Worth noting, not worth a fix yet.
- [ ] **Hard's slow turn when the Tyranids drop to 7 units.** Below 8 units Hard switches from Normal's moves to
  its own move search (`move_search_units = 7`). In the Debug client that turn took 44 s, and 4.4 s in Release.
  If a fix for item 1 changes how the Tyranids move, look at whether that switch is still wanted.
- [ ] **No objectives.** With no objectives (14) the cheap units have no job, so "hold back and keep your points"
  is the best they can do under D-076. Objectives would give them one (holding ground, screening) and may fix
  part of item 1 on their own. Decide the order of the two milestones with that in mind.
