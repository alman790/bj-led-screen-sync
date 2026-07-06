$ErrorActionPreference = "Stop"

$Version = (Get-Content VERSION.txt -ErrorAction SilentlyContinue)
if (-not $Version) { $Version = "0.1.0" }

nmake /f Makefile.windows

New-Item -ItemType Directory -Force -Path dist | Out-Null
Compress-Archive -Path build/BJLEDAmbilight.exe -DestinationPath "dist/BJLEDAmbilight-$Version-windows-x64.zip" -Force
Write-Host "Windows portable zip: dist/BJLEDAmbilight-$Version-windows-x64.zip"

if (Get-Command iscc -ErrorAction SilentlyContinue) {
    iscc installers/windows/BJLEDAmbilight.iss /DAppVersion=$Version
    Write-Host "Windows installer: dist/BJLEDAmbilight-$Version-windows-x64-setup.exe"
} else {
    Write-Host "Inno Setup not found; skipped setup installer"
}
