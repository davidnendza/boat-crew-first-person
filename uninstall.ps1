# Removes the mod from the Boat Crew game folder. Run from PowerShell with the game closed:
#     .\uninstall.ps1                  or   .\uninstall.ps1 -GamePath "D:\Games\Boat Crew"
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
if (Get-Process -Name "Boat Crew" -ErrorAction SilentlyContinue) { Write-Host "Boat Crew is running. Close it first."; return }
$dll = Join-Path $game "version.dll"
if (Test-Path $dll) { Remove-Item $dll; Write-Host "Removed $dll" -ForegroundColor Green } else { Write-Host "No version.dll in $game; nothing to remove." }
Write-Host "The BoatCrewMod folder (log and config only) is left in place; delete it if you like."
