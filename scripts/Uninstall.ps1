$ErrorActionPreference = "Stop"
$installDirectory = Join-Path $env:LOCALAPPDATA "Programs\IME-AltSwap"

$running = Get-Process -Name "IME-AltSwap" -ErrorAction SilentlyContinue
if ($running) {
    $running | Stop-Process -Force
    $running | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
}
Remove-ItemProperty -Path "HKCU:\Software\Microsoft\Windows\CurrentVersion\Run" -Name "IME-AltSwap" -ErrorAction SilentlyContinue
Remove-Item -LiteralPath "HKCU:\Software\IME-AltSwap" -Recurse -Force -ErrorAction SilentlyContinue

if (Test-Path -LiteralPath $installDirectory) {
    Remove-Item -LiteralPath $installDirectory -Recurse -Force
}

Write-Output "IME AltSwap was removed."
