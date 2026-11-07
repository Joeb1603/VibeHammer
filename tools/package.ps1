# Builds the client in Release and packs it into VibeHammer.zip (repo root, gitignored) for sending to
# someone to play. The zip holds the exe, the VC++ runtime DLLs, data/ and the example replays; the exe
# finds data/ next to itself (main.cpp find_data_root). Never add docs/rules/ or the PDF (GW copyright).
#
# Usage (from the repo root):  powershell -ExecutionPolicy Bypass -File tools\package.ps1
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

cmake --preset default | Out-Null
cmake --build --preset release --target wh40k_client_2d
if ($LASTEXITCODE -ne 0) { throw "Release build failed" }

# The VC++ runtime from the newest installed Visual Studio redist (app-local deployment).
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT" -Directory |
    Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw "VC++ redist DLLs not found under $vs\VC\Redist\MSVC" }

$stage = Join-Path ([System.IO.Path]::GetTempPath()) "VibeHammer_package\VibeHammer"
if (Test-Path $stage) { Remove-Item -Recurse -Force $stage -Confirm:$false }
New-Item -ItemType Directory -Force "$stage\data\units", "$stage\data\scenarios", "$stage\tests\replays" | Out-Null

Copy-Item "$root\build\default\src\client_2d\Release\wh40k_client_2d.exe" "$stage\VibeHammer.exe"
Copy-Item "$root\data\units\*.json" "$stage\data\units"
Copy-Item "$root\data\scenarios\*.json" "$stage\data\scenarios"
Copy-Item "$root\tests\replays\*.json" "$stage\tests\replays"
foreach ($dll in 'vcruntime140.dll', 'vcruntime140_1.dll', 'msvcp140.dll') {
    Copy-Item (Join-Path $crt.FullName $dll) $stage
}

@'
VibeHammer - Warhammer 40,000 (11th edition) skirmish, hotseat or against the bot
=================================================================================

How to play
  1. Unzip this whole folder somewhere (don't run the exe from inside the zip).
  2. Double-click VibeHammer.exe. It opens on the start screen:
     - Play hotseat: two players share the computer.
     - Play vs bot: choose the side you play (Space Marines or Tyranids); the bot plays the other.
     - Watch bot vs bot: the bot plays both sides. In any game with a bot, the sidebar has
       "Pause the bots" (key P); the bar above the board has Step (one action, or the bot's
       next dice roll) and the bot speed (keys N, + and -).
     - Continue saved game (when there is one), Watch a replay, Quit.
  3. Space Marine Intercessors vs Tyranid Termagants. Follow the decision panel at the top;
     click the board to pick units, targets and moves.

The bot
  Levels: Easy, Normal and Hard (Hard is the default). Speed: Slow, Normal, Fast or Instant;
  it can be changed during the game. The bot's dice pop-ups roll and continue by
  themselves, with no Roll button (pause the bots, key P, to roll them yourself). The log (Events tab) shows what the bot did
  and why; hover a bot line for its full reasoning.

Settings
  The start screen's options are saved in vibehammer_settings.json next to VibeHammer.exe.
  Delete that file to go back to the defaults.

Keep the "data" folder next to VibeHammer.exe - the game loads its units and scenarios from it.
"tests/replays" holds example games you can load in the replay viewer.
Saved games go in a "saves" folder created next to the exe.

If Windows SmartScreen warns about an unknown app, click "More info" -> "Run anyway".
'@ | Out-File -Encoding utf8 "$stage\README.txt"

# Smoke test 1: run the staged exe from another folder and check it renders a game.
$shot = Join-Path (Split-Path $stage) "smoke.png"
if (Test-Path $shot) { Remove-Item -Force $shot -Confirm:$false }
$p = Start-Process "$stage\VibeHammer.exe" -ArgumentList "--screenshot", $shot, "--steps", "3", "--seed", "1" `
    -WorkingDirectory $env:TEMP -PassThru -Wait
if ($p.ExitCode -ne 0 -or -not (Test-Path $shot)) { throw "Smoke test failed (exit $($p.ExitCode))" }
Write-Host "Smoke test 1 passed: $shot"

# Smoke test 2 (M45): the bot plays side B. --steps passes side A's turn (it stops at the bot's first
# decision), then --bot-steps makes 5 bot decisions; the exe prints how many it took, and all 5 must run.
$botShot = Join-Path (Split-Path $stage) "smoke_bot.png"
$botOut = Join-Path (Split-Path $stage) "smoke_bot.txt"
foreach ($f in $botShot, $botOut) { if (Test-Path $f) { Remove-Item -Force $f -Confirm:$false } }
$p = Start-Process "$stage\VibeHammer.exe" `
    -ArgumentList "--seed", "1", "--bot", "B", "--steps", "40", "--bot-steps", "5", "--screenshot", $botShot `
    -WorkingDirectory $env:TEMP -RedirectStandardOutput $botOut -PassThru -Wait
if ($p.ExitCode -ne 0 -or -not (Test-Path $botShot)) { throw "Bot smoke test failed (exit $($p.ExitCode))" }
$taken = Select-String -Path $botOut -Pattern "bot-steps: (\d+) of 5 bot actions taken"
if (-not $taken -or $taken.Matches[0].Groups[1].Value -ne "5") {
    throw "Bot smoke test failed: the bot did not take 5 actions ($(if ($taken) { $taken.Line } else { 'no bot-steps line' }))"
}
Write-Host "Smoke test 2 passed: $($taken.Line) ($botShot)"

Compress-Archive -Path $stage -DestinationPath "$root\VibeHammer.zip" -Force
Write-Host "Wrote $root\VibeHammer.zip (smoke screenshots: $shot, $botShot)"
