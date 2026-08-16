param(
    [string]$Executable = (Join-Path $PSScriptRoot "..\build\Release\IME-AltSwap.exe")
)

$ErrorActionPreference = "Stop"
$source = (Resolve-Path -LiteralPath $Executable).Path
$installDirectory = Join-Path $env:LOCALAPPDATA "Programs\IME-AltSwap"
$installedExecutable = Join-Path $installDirectory "IME-AltSwap.exe"

New-Item -ItemType Directory -Path $installDirectory -Force | Out-Null
$running = Get-Process -Name "IME-AltSwap" -ErrorAction SilentlyContinue
if ($running) {
    $running | Stop-Process -Force
    $running | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
}
Copy-Item -LiteralPath $source -Destination $installedExecutable -Force

$runKey = "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run"
$command = '"' + $installedExecutable + '" --startup'
New-ItemProperty -Path $runKey -Name "IME-AltSwap" -Value $command -PropertyType String -Force | Out-Null

$settingsKey = "HKCU:\Software\IME-AltSwap"
New-Item -Path $settingsKey -Force | Out-Null
New-ItemProperty -Path $settingsKey -Name "StartupPreferenceInitialized" -Value 1 -PropertyType DWord -Force | Out-Null

Start-Process -FilePath $installedExecutable -WindowStyle Hidden
Write-Output "Installed: $installedExecutable"
