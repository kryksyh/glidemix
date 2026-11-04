#
# GlideMix: a clean, deep uninstall -- everything an installed GlideMix
# leaves on a Windows PC goes to the Recycle Bin (so it can be put back), and
# its settings are cleared. For testing a package as a brand-new user. The
# Windows counterpart of buildscripts/packaging/MacOS/glidemix_clean_uninstall.sh.
#
#   powershell -ExecutionPolicy Bypass -File buildscripts\packaging\Windows\glidemix_clean_uninstall.ps1
#
# Removed: the installed app (its own uninstaller first, so the Apps list is
# clean; any folder left behind goes to the Recycle Bin); %APPDATA%\Glidemix
# (library, analysis, settings -- Glidemix\Glidemix.ini -- the stems engine
# and stems, helpers); %LOCALAPPDATA%\Glidemix (logs, caches, Audacity-side
# data); HKCU\Software\Glidemix if anything ever wrote there (exported to a
# .reg file on the Desktop first). Documents\Glidemix (projects) stays, as
# ~/Documents does on the Mac.
#
# Windows asks before deleting something too big for the Recycle Bin (the
# stems engine can be over 1 GB); say Yes to delete it for good.

$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName Microsoft.VisualBasic

if (Get-Process -Name GlideMix -ErrorAction SilentlyContinue) {
    Write-Output "GlideMix is running: quit it first."
    exit 1
}

function Send-ToRecycleBin([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path)) { return }
    $display = [Microsoft.VisualBasic.FileIO.UIOption]::OnlyErrorDialogs
    $recycle = [Microsoft.VisualBasic.FileIO.RecycleOption]::SendToRecycleBin
    try {
        if (Test-Path -LiteralPath $Path -PathType Container) {
            [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($Path, $display, $recycle)
        } else {
            [Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($Path, $display, $recycle)
        }
        Write-Output "  recycled $Path"
    } catch {
        Write-Output "  could not recycle $Path ($($_.Exception.Message))"
    }
}

# The installer (glidemix.iss) registers under this AppId; per-user installs
# land in HKCU, all-users installs in HKLM.
$AppId = '{332C5FF2-1D0E-427E-B5B2-1D433E93F6D8}_is1'
$UninstallKeys = @(
    "HKCU:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppId",
    "HKLM:\Software\Microsoft\Windows\CurrentVersion\Uninstall\$AppId",
    "HKLM:\Software\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\$AppId"
)

Write-Output "== App"
$installDirs = @()
foreach ($key in $UninstallKeys) {
    $entry = Get-ItemProperty -LiteralPath $key -ErrorAction SilentlyContinue
    if (-not $entry) { continue }
    if ($entry.InstallLocation) { $installDirs += $entry.InstallLocation.TrimEnd('\') }
    $uninstaller = $entry.UninstallString -replace '"', ''
    if ($uninstaller -and (Test-Path -LiteralPath $uninstaller)) {
        Write-Output "  running $uninstaller"
        Start-Process -FilePath $uninstaller -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART' -Wait
        Start-Sleep -Seconds 2   # it finishes from a copy of itself in %TEMP%
    }
}
$installDirs += "$env:LOCALAPPDATA\Programs\GlideMix"
$installDirs += "$env:ProgramFiles\GlideMix"
foreach ($dir in ($installDirs | Select-Object -Unique)) {
    Send-ToRecycleBin $dir
}
Send-ToRecycleBin "$env:APPDATA\Microsoft\Windows\Start Menu\Programs\GlideMix.lnk"
Send-ToRecycleBin (Join-Path ([Environment]::GetFolderPath('Desktop')) 'GlideMix.lnk')

Write-Output "== Files"
Send-ToRecycleBin "$env:APPDATA\Glidemix"
Send-ToRecycleBin "$env:LOCALAPPDATA\Glidemix"
Send-ToRecycleBin "$env:TEMP\Glidemix"

Write-Output "== Settings"
# GlideMix keeps its settings in Glidemix.ini (above); the registry is only
# checked in case a Qt or wxWidgets default ever wrote there.
foreach ($key in @('HKCU\Software\Glidemix', 'HKCU\Software\glidemix.com')) {
    if (Test-Path -LiteralPath ("Registry::" + $key)) {
        $backup = Join-Path ([Environment]::GetFolderPath('Desktop')) ("glidemix-settings-" + (Get-Date -Format 'yyyyMMdd-HHmmss') + ".reg")
        & reg.exe export $key $backup /y | Out-Null
        Remove-Item -LiteralPath ("Registry::" + $key) -Recurse -Force
        Write-Output "  cleared $key (backup: $backup)"
    }
}

Write-Output "== Left over"
$left = @()
foreach ($root in @($env:APPDATA, $env:LOCALAPPDATA, "$env:LOCALAPPDATA\Programs", $env:ProgramFiles)) {
    if ($root -and (Test-Path -LiteralPath $root)) {
        $left += Get-ChildItem -LiteralPath $root -Filter '*glidemix*' -Force -ErrorAction SilentlyContinue |
                 Where-Object { $_.FullName -notmatch 'CrashDumps' } |
                 ForEach-Object { $_.FullName }
    }
}
if ($left.Count -gt 0) {
    $left | ForEach-Object { Write-Output "  $_" }
} else {
    Write-Output "  nothing"
}
