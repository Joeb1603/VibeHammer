# Client command-line flags (screenshots, cloud sessions)

Moved verbatim from `CLAUDE.md` in M84. `CLAUDE.md` keeps a pointer here; read this file when the task needs it.

The client's mouse input can't be scripted, so in a cloud session check the UI through the logic-level tests (`wh40k_client_tests`) and screenshots under `xvfb-run`. The client's flags for this are `--screenshot <file.png>`, `--steps <n>` (click the first decision button n times), `--select <unit id>`, `--select-model <model id>`, `--seed <n>` (fixes the seed; without it each game's seed is random, D-038), `--scenario <file>` (default `data/scenarios/incursion_terrain_44x60.json` since M88; the objective scenario M86–M87), `--replay <file>`, `--frame <n>`, `--zoom <px per mm>`, `--dice <n>` (1 keeps the dice pop-ups that the `--steps` rolls made, 2 also reveals the first; they're dropped otherwise, D-033) `--menu 1` (shows the start screen, which any other flag skips; D-055), `--bot A|B|both|none` (the sides the bot plays; one bot since M85, D-085, so no `--bot-level`: an old command line that passes it runs with it ignored, as any unknown flag), `--bot-speed slow|normal|fast|instant` and `--bot-steps <n>` (after `--steps`, runs up to n bot decisions at once before the first frame, stopping at a human's decision; `--steps` itself stops at a bot's decision; D-054; it prints `bot-steps: <taken> of <n> bot actions taken` to stdout, which `package.ps1`'s second smoke test checks, M45). Flags added in M59–M70 for screenshots:
- `--load <save.json>` starts on a saved game. It can't be combined with `--scenario`, `--replay` or `--seed`.
- `--pick-dice <i,j,...>` picks wound dice in the first re-roll pop-up. Since M95 it also picks Command Re-roll's die
  (D-096) on the first card or pop-up of a window that has dice to pick.
- `--pick-models <id,...>` picks models in the first casualty pick.
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

A bad flag value exits with code 2. A full game by mouse still needs a manual run on Windows.
