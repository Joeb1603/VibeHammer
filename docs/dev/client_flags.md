# Client command-line flags (screenshots, cloud sessions)

Moved verbatim from `CLAUDE.md` in M84. `CLAUDE.md` keeps a pointer here; read this file when the task needs it.

The client's mouse input can't be scripted, so in a cloud session check the UI through the logic-level tests (`wh40k_client_tests`) and screenshots under `xvfb-run`. The client's flags for this are `--screenshot <file.png>`, `--steps <n>` (click the first decision button n times), `--select <unit id>`, `--select-model <model id>`, `--seed <n>` (fixes the seed; without it each game's seed is random, D-038), `--scenario <file>`, `--replay <file>`, `--frame <n>`, `--zoom <px per mm>`, `--dice <n>` (1 keeps the dice pop-ups that the `--steps` rolls made, 2 also reveals the first; they're dropped otherwise, D-033) `--menu 1` (shows the start screen, which any other flag skips; D-055), `--bot A|B|both|none` (the sides the bot plays; one bot since M85, D-085, so no `--bot-level`: an old command line that passes it runs with it ignored, as any unknown flag), `--bot-speed slow|normal|fast|instant` and `--bot-steps <n>` (after `--steps`, runs up to n bot decisions at once before the first frame, stopping at a human's decision; `--steps` itself stops at a bot's decision; D-054; it prints `bot-steps: <taken> of <n> bot actions taken` to stdout, which `package.ps1`'s second smoke test checks, M45). Flags added in M59–M70 for screenshots:
- `--load <save.json>` starts on a saved game. It can't be combined with `--scenario`, `--replay` or `--seed`.
- `--pick-dice <i,j,...>` picks wound dice in the first re-roll pop-up.
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


None of these submits an action. While dragging an oval model, Q / E turn it 15° and Shift+wheel turns it 5°. A run with any flag uses the default settings and never reads or writes `vibehammer_settings.json`. `--steps` only clicks panel buttons, so it can't reach decisions made by clicking the board (picking shooters, targets, charges), or the rolls they lead to. A bad flag value exits with code 2. A full game by mouse still needs a manual run on Windows.
