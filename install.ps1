# Copies the freshly built out\version.dll into the Boat Crew game folder, and clears the old log.
# Run from PowerShell with the game CLOSED:
#     .\install.ps1                      finds the game through Steam
#     .\install.ps1 -GamePath "D:\Games\Boat Crew"   if it cannot
param([string]$GamePath)

function Find-BoatCrew {
  param([string]$Hint)
  $candidates = @()
  if ($Hint) { $candidates += $Hint }
  $steam = $null
  try { $steam = (Get-ItemProperty "HKCU:\Software\Valve\Steam" -ErrorAction Stop).SteamPath } catch {}
  if ($steam) {
    $candidates += (Join-Path $steam "steamapps\common\Boat Crew")
    $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
    if (Test-Path $vdf) {
      foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
        $candidates += (Join-Path ($m.Groups[1].Value -replace '\\', '\') "steamapps\common\Boat Crew")
      }
    }
  }
  $candidates += "C:\Program Files (x86)\Steam\steamapps\common\Boat Crew"
  foreach ($c in $candidates) { if ($c -and (Test-Path (Join-Path $c "Boat Crew.exe"))) { return $c } }
  return $null
}

$game = Find-BoatCrew $GamePath
if (-not $game) { Write-Host "Could not find Boat Crew. Run again with -GamePath pointing at the folder that holds 'Boat Crew.exe'." -ForegroundColor Yellow; return }
$src = Join-Path $PSScriptRoot "out\version.dll"
$dst = Join-Path $game "version.dll"

if (Get-Process -Name "Boat Crew" -ErrorAction SilentlyContinue) { Write-Host "Boat Crew is running. Close it first, then run this again." -ForegroundColor Yellow; return }
if (-not (Test-Path $src)) { Write-Host "No build found at $src. Run build.bat first." -ForegroundColor Yellow; return }

Copy-Item $src $dst -Force
Remove-Item (Join-Path $game "BoatCrewMod\mod.log") -ErrorAction SilentlyContinue

$built = (Get-FileHash $src -Algorithm MD5).Hash
$there = (Get-FileHash $dst -Algorithm MD5).Hash
if ($built -eq $there) {
  Write-Host "Installed to $game" -ForegroundColor Green
  Write-Host "version.dll matches the build ($($built.Substring(0,8))..., $((Get-Item $dst).Length) bytes)."
  Write-Host "Start the game, select a crewmate and press Insert. The log is BoatCrewMod\mod.log in the game folder."
} else {
  Write-Host "The copy does not match the build. Installed $($there.Substring(0,8))..., built $($built.Substring(0,8))...." -ForegroundColor Red
}
