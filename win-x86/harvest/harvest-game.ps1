# Rocksmith 2014 Re-ARMed -- one-step harvester (run on a Windows PC where you own the game).
# It launches YOUR installed game, records the code as it runs, and writes ONE file you copy to
# your Mac. Nothing of the game is sent anywhere; the capture stays on your machine.
param([string]$GameExe = "")
$ErrorActionPreference = "Continue"
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$EXPECT = "BB056959C0C6371D4ECF78F84C5D27E2FAE93A9D0258830C2A36F33E6A2778EB"

function Find-Game {
    param($given)
    if ($given -and (Test-Path $given)) { return $given }
    $cands = @("C:\Program Files (x86)\Steam\steamapps\common\Rocksmith2014\Rocksmith2014.exe")
    # other Steam library folders
    $vdf = "C:\Program Files (x86)\Steam\steamapps\libraryfolders.vdf"
    if (Test-Path $vdf) {
        foreach ($m in [regex]::Matches((Get-Content -Raw $vdf), '"path"\s*"([^"]+)"')) {
            $cands += (Join-Path ($m.Groups[1].Value -replace '\\\\','\') "steamapps\common\Rocksmith2014\Rocksmith2014.exe")
        }
    }
    foreach ($c in $cands) { if (Test-Path $c) { return $c } }
    return $null
}

Write-Host "=== Rocksmith 2014 Re-ARMed : game capture ==="
$exe = Find-Game $GameExe
if (-not $exe) {
    Write-Host "Could not find Rocksmith2014.exe automatically."
    Write-Host "Install the Windows 'Rocksmith 2014 - Learn & Play' via Steam, then run:"
    Write-Host "    powershell -ExecutionPolicy Bypass -File harvest-game.ps1 -GameExe `"C:\path\to\Rocksmith2014.exe`""
    Read-Host "press Enter to close"; exit 1
}
Write-Host "game: $exe"
$sha = (Get-FileHash $exe -Algorithm SHA256).Hash
if ($sha -ne $EXPECT) {
    Write-Warning "This is not the game build Re-ARMed supports (yours: $sha)."
    Write-Warning "It may still work, but the Mac build expects the supported version. Continuing in 5s..."
    Start-Sleep 5
}
$rsharvest = Join-Path $here "rsharvest2.exe"
if (-not (Test-Path $rsharvest)) { Write-Host "rsharvest2.exe is missing next to this script."; Read-Host "press Enter"; exit 1 }

$out = Join-Path ([Environment]::GetFolderPath("Desktop")) "rs-harvest"
Remove-Item $out -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $out | Out-Null
Copy-Item $rsharvest (Join-Path $out "rsharvest2.exe")

Write-Host ""
Write-Host "Starting the game and the recorder..."
$game = Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe) -PassThru
Start-Sleep -Seconds 3
$rec = Start-Process -FilePath (Join-Path $out "rsharvest2.exe") -WorkingDirectory $out -PassThru -WindowStyle Minimized
Write-Host ""
Write-Host "************************************************************************"
Write-Host "*  The game is launching under the recorder.                           *"
Write-Host "*  1) Let it reach the MAIN MENU.                                       *"
Write-Host "*  2) Start any song and play ~30 seconds (this makes it reveal all     *"
Write-Host "*     its code so the Mac build is complete). Quitting back to menu is   *"
Write-Host "*     fine.                                                             *"
Write-Host "*  3) Then come back to THIS window and press Enter.                     *"
Write-Host "************************************************************************"
[void](Read-Host "Press Enter when you've reached the menu and played a little")

Write-Host "finishing the recording (saving)..."
Start-Sleep 3
Get-Process Rocksmith2014 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep 6   # give the recorder time to flush on game exit
Stop-Process -Id $rec.Id -Force -ErrorAction SilentlyContinue
Start-Sleep 2
Remove-Item (Join-Path $out "rsharvest2.exe") -ErrorAction SilentlyContinue

$bin = Join-Path $out "rs_harvest2.bin"
if (-not (Test-Path $bin)) {
    Write-Warning "No capture was produced. Make sure the game actually reached the menu, and try again."
    Read-Host "press Enter"; exit 1
}
$zip = Join-Path ([Environment]::GetFolderPath("Desktop")) "rs-harvest.zip"
if (Test-Path $zip) { Remove-Item $zip }
Compress-Archive -Path (Join-Path $out "*") -DestinationPath $zip
$mb = [int]((Get-Item $zip).Length/1MB)
Write-Host ""
Write-Host "DONE.  Created:  $zip  ($mb MB)"
Write-Host ""
Write-Host "Copy rs-harvest.zip to your Mac, then run the Re-ARMed installer and point it at it"
Write-Host "(it unzips to a folder and builds your game from it). Nothing but your own game's data"
Write-Host "is in this file, and it stays with you."
Read-Host "press Enter to close"
