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
VibeHammer - Warhammer 40,000 (11th edition) skirmish, hotseat
==============================================================

How to play
  1. Unzip this whole folder somewhere (don't run the exe from inside the zip).
  2. Double-click VibeHammer.exe.
  3. Two players share the computer: Space Marine Intercessors vs Tyranid Termagants.
     Follow the decision panel on the right; click the board to pick units, targets and moves.

Keep the "data" folder next to VibeHammer.exe - the game loads its units and scenario from it.
"tests\replays" holds example games you can load in the replay viewer.
Saved games go in a "saves" folder created next to the exe.

If Windows SmartScreen warns about an unknown app, click "More info" -> "Run anyway".
'@ | Out-File -Encoding utf8 "$stage\README.txt"

# Smoke test: run the staged exe from another folder and check it renders a game.
$shot = Join-Path (Split-Path $stage) "smoke.png"
$p = Start-Process "$stage\VibeHammer.exe" -ArgumentList "--screenshot", $shot, "--steps", "3", "--seed", "1" `
    -WorkingDirectory $env:TEMP -PassThru -Wait
if ($p.ExitCode -ne 0 -or -not (Test-Path $shot)) { throw "Smoke test failed (exit $($p.ExitCode))" }

Compress-Archive -Path $stage -DestinationPath "$root\VibeHammer.zip" -Force
Write-Host "Wrote $root\VibeHammer.zip (smoke screenshot: $shot)"
