# Install Connie.vst3 to the Windows system VST3 folder (requires admin).
$ErrorActionPreference = "Stop"

$src = Join-Path $PSScriptRoot "build\vst3\Release\Connie.vst3"
$dst = Join-Path ${env:CommonProgramW6432} "VST3\Connie.vst3"

if (-not (Test-Path $src)) {
    Write-Error "Build output not found: $src`nRun: cmake --build build --config Release"
}

$install = @"
if (Test-Path '$dst') { Remove-Item '$dst' -Recurse -Force }
Copy-Item -Path '$src' -Destination '$dst' -Recurse -Force
Write-Host "Installed: $dst"
"@

$tmp = Join-Path $env:TEMP "connie-install-vst3.ps1"
Set-Content -Path $tmp -Value $install
Start-Process powershell -Verb RunAs -ArgumentList "-NoProfile -ExecutionPolicy Bypass -File `"$tmp`"" -Wait

if (Test-Path "$dst\Contents\x86_64-win\Connie.vst3") {
    Write-Host "OK. Rescan plug-ins in Ableton (Preferences > Plug-ins)."
} else {
    Write-Error "Install may have failed. Check $dst"
}
