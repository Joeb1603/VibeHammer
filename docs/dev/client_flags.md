# Client command-line flags (screenshots, cloud sessions)

Moved verbatim from `CLAUDE.md` in M84. `CLAUDE.md` keeps a pointer here; read this file when the task needs it.

The client's mouse input can't be scripted, so in a cloud session check the UI through the logic-level tests (`wh40k_client_tests`) and screenshots under `xvfb-run`. The client's flags for this are `--screenshot <file.png>`, `--steps <n>` (click the first decision button n times), `--select <unit id>`, `--select-model <model id>`, `--seed <n>` (fixes the seed; without it each game's seed is random, D-038), `--scenario <file>` (default `data/scenarios/incursion_terrain_44x60.json` since M88; the objective scenario M86–M87), `--replay <file>`, `--frame <n>`, `--zoom <px per mm>`, `--dice <n>` (1 keeps the dice pop-ups that the `--steps` rolls made, 2 also reveals the first; they're dropped otherwise, D-033) `--menu 1` (shows the start screen, which any other flag skips; D-055), `--bot A|B|both|none` (the sides the bot plays; one bot since M85, D-085, so no `--bot-level`: an old command line that passes it runs with it ignored, as any unknown flag), `--bot-speed slow|normal|fast|instant` and `--bot-steps <n>` (after `--steps`, runs up to n bot decisions at once before the first frame, stopping at a human's decision; `--steps` itself stops at a bot's decision; D-054; it prints `bot-steps: <taken> of <n> bot actions taken` to stdout, which `package.ps1`'s second smoke test checks, M45). Flags added in M59–M70 for screenshots:
- `--load <save.json>` starts on a saved game. It can't be combined with `--scenario`, `--replay` or `--seed`.
- `--pick-dice <i,j,...>` picks wound dice in the first re-roll pop-up. Since M95 it also picks Command Re-roll's die
  (D-096) on the first card or pop-up of a window that has dice to pick.
- `--pick-models <id,...>` picks models in the first casualty pick. Since M98 the first id also chooses the model at
  15.05 / 15.06's selection on the board (D-100).
- `--click-units <id,...>` clicks units on the board and opens the last target's weapon menu.
- `--turn <model>:<deg>[:<dx>,<dy>]` makes a scripted turned drag of an oval model.
- `--window <w>x<h>` opens the window at that size instead of maximized.

Flags added in M75–M76 (custom battles, D-082):
- `--builder 1` opens the army builder. With it, `--select <unit id>` selects a builder unit (its id in the battle,
  side A's units first) and `--open-slot <group>:<slot>` opens one of its wargear combos.
- `--army-a <file>`, `--army-b <file>` (army lists; a side without one keeps its stock army), `--first A|B` and
  `--deploy auto|hand` start a custom battle, or preload the builder with `--builder 1`. They can't be combined with
  `--scenario`, `--replay` or `--load`, and `--builder 1` not with `--menu 1`.
- `--deploy-unit <unit>:<dx>,<dy>[:<model>:<x>,<y>]` starts placing that unit by hand in its side's first
  deployment decision, moved from core's proposal (inches), one model optionally put at (x, y). `--turn` also turns
  a model being placed.

Flag added in M95 (reaction windows, D-096):
- `--dice-popups 0|1` sets "Show dice pop-ups" for the run (a scripted run's defaults have it on). With 0 a human's
  rolls are made for them and a window after a roll shows in its own pop-up.


None of these submits an action. While dragging an oval model, Q / E turn it 15° and Shift+wheel turns it 5°. A run with any flag uses the default settings and never reads or writes `vibehammer_settings.json`. `--steps` only clicks panel buttons, so it can't reach decisions made by clicking the board (picking shooters, targets, charges), or the rolls they lead to. M94 (D-095): a human's roll waiting with the dice pop-ups on (the default) counts as a decision: `--steps` presses its Roll as the pre-roll card's button would (one step per roll; a bot's rolls are made by the session and don't count, nor for `--bot-steps`). To screenshot a pre-roll card, `--load` a save waiting for a roll (`M94_Screens.DISABLED_WriteSavesWaitingForARoll` writes two, with `WH40K_SCREEN_DIR`); `--steps 1 --dice 2` then shows the card rolled. M95 (D-096): a human's window counts as a decision too: `--steps` declines it, as "Keep the roll" on its card would. `M95_Screens.DISABLED_WriteSavesWaitingForAWindow` writes four saves:
- waiting for Side A's hit rolls: `--steps 1 --dice 2 --pick-dice 1` shows the rolled card with its window's buttons, a die picked and the gold critical dice;
- at Side A's window after them, with no card: the window's pop-up;
- waiting for Side B's save rolls: `--steps 1 --dice 2` shows the defender's window;
- at Insane Bravery's window: the battle-shock roll's face-down card with the stratagem.

M96 (D-097): the windows that follow no roll wait in the decision panel, one button per stratagem, mode and unit, the
decline first (so `--steps` declines them). `M96_Screens.DISABLED_WriteSavesWaitingForTheNewWindows` writes five saves,
each shown by `--load <save> --screenshot <png>`:
- at Side B's Fire Overwatch window (V1, the Intercessors stationary);
- at its snap shot: the shot editor with every Termagant at the Intercessors ("using snap shooting (15.09)");
- waiting for the snap shot's hit rolls: `--steps 1 --dice 2` shows the card, "Need 6 to hit (snap shooting, 15.09)",
  its 6s green, not gold;
- at Heroic Intervention's window, Leap to Defend and Into the Fray with their costs;
- at Counteroffensive's window, after the Intercessors have fought.

M97 (D-099): 15.05 Explosives is a button in the Shoot step's action bar ("Explosives (1 CP): <unit>", its unit
outlined in gold while hovered); after it, and after 15.06 Crushing Impact (its window as M96's, just after a charge
move), a bar asks for the enemy unit, one button per enemy unit with its first legal model (Crushing Impact's with the
model's T), the enemy unit and the model outlined in gold while hovered. `--steps` takes the first button. A side that
declines a stratagem ("Offer stratagems" off, or a bot whose key is 0) is never shown its answers
(`GameSession::legal_offered`). `M97_Screens.DISABLED_WriteSavesWaitingForTheNewSteps` writes six saves, each shown by
`--load <save> --screenshot <png>`:
- at Side A's Shoot step (V1, the Termagants 6" away), with the Explosives button;
- at its selection (one button: the Termagants);
- waiting for its six D6: the face-down card ("6 dice: each 4+ is 1 mortal wound on Termagants"); `--steps 1 --dice 2`
  shows it rolled;
- at Side B's Crushing Impact window, just after the Hive Tyrant's charge (the monsters fixture);
- at its selection ("Legionaries 2 (model 37, T10: 10 dice)");
- waiting for its ten D6: `--steps 1 --dice 2` shows the card, the 5+s green, the 1s failed and the rest grey.

M98 (D-100): the selection is also picked on the board. Its enemy units and the stratagem's unit can be clicked, and
every other unit is grey. A click on an enemy unit submits its answer: the chosen model's, or else the first legal
model's. A click on a model of the stratagem's unit first chooses that model (a gold fill and ring); a second click or
Esc drops it. A model in no answer is dimmed. Hovering a grey enemy unit gives the engine's reason ("Can't be picked:
15.05: unit 11 is not within 8" of model 8"). The bar keeps its buttons, narrowed to the chosen model's answers.
`M98_Screens.DISABLED_WriteSavesWaitingForTheSelection` writes two saves:
- Explosives' selection in the monsters fixture (Side A first), with the Hive Tyrant within 8" of the rank's east end
  and the Tyrant Guard of its west end. `--pick-models 8` chooses model 8 (the bar then has only the Tyrant's button,
  and the Guard turns grey); `--zoom 1.5` shows the rank;
- Crushing Impact's selection after the Hive Tyrant's charge; `--pick-models 37` chooses the Tyrant's model.

A bad flag value exits with code 2. A full game by mouse still needs a manual run on Windows.
